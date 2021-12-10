#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/signal.h>
#include "naomi/interrupt.h"
#include "naomi/video.h"
#include "naomi/console.h"
#include "naomi/thread.h"
#include "irqstate.h"
#include "holly.h"

// Saved state of SR and VBR when we initialize interrupts. We will use these
// to restore SR/VBR when de-initializing again.
static uint32_t saved_sr;
static uint32_t saved_vbr;

// Whether we're currently halted or not. This is for GDB debugging.
static int halted;

// Whether we're in the IRQ handler currently or not.
static int in_interrupt;

// Size we wish our stack to be.
#define IRQ_STACK_SIZE 16384

// Definitions from sh-crt0.s that we use here.
extern uint8_t *irq_stack;
extern irq_state_t *irq_state;
static irq_state_t *irq_freed_state = 0;

#define TRA *((volatile uint32_t *)0xFF000020)
#define EXPEVT *((volatile uint32_t *)0xFF000024)
#define INTEVT *((volatile uint32_t *)0xFF000028)

#define INTC_BASE_ADDRESS 0xFFD00000

#define INTC_IPRA *((volatile uint16_t *)(INTC_BASE_ADDRESS + 0x04))
#define INTC_IPRB *((volatile uint16_t *)(INTC_BASE_ADDRESS + 0x08))
#define INTC_IPRC *((volatile uint16_t *)(INTC_BASE_ADDRESS + 0x0C))
#define INTC_IPRD *((volatile uint16_t *)(INTC_BASE_ADDRESS + 0x10))

// Non-varargs version of debug print, so we can use our own static buffer for safety.
void __video_draw_debug_text( int x, int y, uint32_t color, const char * const msg );

// Static buffer for displaying exceptions, so we don't have to rely on messing with the
// stack of the running program or our own internal stack.
static char exception_buffer[1024];

// Statistics about the interrupt system.
static irq_stats_t stats;

// If we should refuse to allow debugging in exception loops.
static int disable_debugging;

// Prototype for passing the signal on to GDB if it connects.
void _gdb_set_haltreason(int reason);

// Prototypes for halting the system with a GDB breakpoint.
int _gdb_user_halt(irq_state_t *cur_state);
int _gdb_breakpoint_halt(irq_state_t *cur_state);

// Prototype to force thread system to disable preemption, used for safely
// re-enabling interrupts to capture registers on an abort call.
void _thread_disable_switching();

// Prototype for polling DIMM commands so we can keep the communication
// channel open even in a halted state. This is so GDB can connect and
// debug us properly.
int _dimm_command_handler(int halted, irq_state_t *cur_state);

// Prototypes of functions we don't want in the public headers.
void _irq_set_vector_table();
int _timer_interrupt(int timer);
uint32_t _irq_enable();
uint32_t _irq_read_sr();
uint32_t _irq_read_vbr();

void _irq_disable_debugging()
{
    // An exception occured in the debugger itself, so we're completely hosed.
    disable_debugging = 1;
}

void _irq_display_exception(int signal, irq_state_t *cur_state, char *failure, int code)
{
    // Threads should already be disabled, but lets be sure.
    irq_disable();

    // Inform GDB why we halted.
    _gdb_set_haltreason(signal);

    // (Re-)init video to a known state to display the exception.
    video_init(VIDEO_COLOR_1555);
    console_set_visible(0);
    video_set_background_color(rgb(48, 0, 0));

    sprintf(
        exception_buffer,
        "EXCEPTION OCCURED: %s (%d)\n\n"
        "GP Regs:\n"
        "r0:  %08lx  r1:  %08lx  r2:  %08lx  r3:  %08lx\n"
        "r4:  %08lx  r5:  %08lx  r6:  %08lx  r7:  %08lx\n"
        "r8:  %08lx  r9:  %08lx  r10: %08lx  r11: %08lx\n"
        "r12: %08lx  r13: %08lx  r14: %08lx\n"
        "stack: %08lx  pc: %08lx",
        failure, code,
        cur_state->gp_regs[0], cur_state->gp_regs[1], cur_state->gp_regs[2], cur_state->gp_regs[3],
        cur_state->gp_regs[4], cur_state->gp_regs[5], cur_state->gp_regs[6], cur_state->gp_regs[7],
        cur_state->gp_regs[8], cur_state->gp_regs[9], cur_state->gp_regs[10], cur_state->gp_regs[11],
        cur_state->gp_regs[12], cur_state->gp_regs[13], cur_state->gp_regs[14],
        cur_state->gp_regs[15], cur_state->pc
    );

    __video_draw_debug_text(32, 32, rgb(255, 255, 255), exception_buffer);
    video_display_on_vblank();

    while( 1 )
    {
        if (!disable_debugging)
        {
            halted = _dimm_command_handler(halted, cur_state);
            if (halted == 0)
            {
                // User continued, not valid, so re-raise the exception.
                _gdb_set_haltreason(signal);
            }
        }
    }
}

// Syscall for capturing registers if we need it.
#define _irq_capture_regs() asm("trapa #254")

void _irq_display_invariant(char *msg, char *failure, ...)
{
    // Force thread system to only ever run us from now on.
    _thread_disable_switching();

    // Make sure that interrupts are already initialized. If not, we have no chance
    // of capturing proper stack traces.
    if (irq_state != 0)
    {
        // We only care to do anything below if we aren't in interrupt context. If
        // we are, we already have the correct stack trace in irq_state so we don't
        // need to do any gymnastics to get registers so GDB can perform backtraces.
        if (!in_interrupt)
        {
            if (_irq_is_disabled(_irq_get_sr()))
            {
                // Re-enable interrupts so that we can call a syscall to force the
                // current register block to be correct. This ensures that stack
                // traces are correct even when interrupts are disabled when we hit
                // an invariant call.
                _irq_enable();
            }

            // Capture registers for backtraces to make sense if we were called
            // in user context instead of interrupt context.
            _irq_capture_regs();
        }
    }

    // Threads should normally already be disabled, but lets be sure.
    irq_disable();

    // Inform GDB why we halted.
    _gdb_set_haltreason(SIGABRT);

    video_init(VIDEO_COLOR_1555);
    console_set_visible(0);
    video_set_background_color(rgb(48, 0, 0));

    sprintf(exception_buffer, "INVARIANT VIOLATION: %s", msg);
    __video_draw_debug_text(32, 32, rgb(255, 255, 255), exception_buffer);

    va_list args;
    va_start(args, failure);
    int length = vsnprintf(exception_buffer, 1023, failure, args);
    va_end(args);

    if (length > 0)
    {
        exception_buffer[length < 1023 ? length : 1023] = 0;
        __video_draw_debug_text(32, 32 + (8 * 2), rgb(255, 255, 255), exception_buffer);
    }

    video_display_on_vblank();

    while( 1 )
    {
        if (!disable_debugging)
        {
            // This is a bit of a hack to get us the last used IRQ state. If we
            // are in interrupt context, it will be accurate since we broke into
            // IRQ by setting the state. If we are outside of interrupt context,
            // it will be the last context switch which shold have happened at
            // the beginning of this function, so it will be accurate enough.
            halted = _dimm_command_handler(halted, irq_state);
            if (halted == 0)
            {
                // User continued, not valid, so re-raise the exception.
                _gdb_set_haltreason(SIGABRT);
            }
        }
    }
}

irq_state_t * _irq_general_exception(irq_state_t *cur_state)
{
    stats.last_event = EXPEVT;

    switch(EXPEVT)
    {
        case IRQ_EVENT_TRAPA:
        {
            // TRAPA, AKA syscall exception.
            unsigned int which = ((TRA) >> 2) & 0xFF;
            if (which == 253)
            {
                // We should jump into GDB mode and halt, since we were requested
                // to do so by a previously placed down breakpoint.
                halted = _gdb_breakpoint_halt(cur_state);
            }
            else if (which == 254)
            {
                // We don't need to do anything here, this is just an interrupt
                // jump to capture registers of the calling process.
            }
            else if (which == 255)
            {
                // We should jump into GDB mode and halt, since we were requested
                // to do so by the breakpoint trap call.
                halted = _gdb_user_halt(cur_state);
            }
            else
            {
                // Handle syscall using our microkernel.
                cur_state = _syscall_trapa(cur_state, which);
            }
            break;
        }
        case IRQ_EVENT_MEMORY_READ_ERROR:
        {
            // Instruction address or memory read address error.
            _irq_display_exception(SIGSEGV, cur_state, "memory read address exception", EXPEVT);
            break;
        }
        case IRQ_EVENT_MEMORY_WRITE_ERROR:
        {
            // Memory write address error.
            _irq_display_exception(SIGSEGV, cur_state, "memory write address exception", EXPEVT);
            break;
        }
        case IRQ_EVENT_FPU_EXCEPTION:
        {
            // Floating point unit exception.
            _irq_display_exception(SIGFPE, cur_state, "floating point exception", EXPEVT);
            break;
        }
        case IRQ_EVENT_ILLEGAL_INSTRUCTION:
        {
            // Illegal instruction.
            _irq_display_exception(SIGILL, cur_state, "illegal instruction", EXPEVT);
            break;
        }
        case IRQ_EVENT_ILLEGAL_SLOT_INSTRUCTION:
        {
            // Illegal branch delay instruction.
            _irq_display_exception(SIGILL, cur_state, "illegal branch slot instruction", EXPEVT);
            break;
        }
        case IRQ_EVENT_NMI:
        {
            // Non-maskable interrupt, did we ask for this?
            _irq_display_exception(SIGINT, cur_state, "NMI interrupt fired", EXPEVT);
            break;
        }
        default:
        {
            // Empty handler, unknown exception.
            _irq_display_exception(SIGINT, cur_state, "uncaught general exception", EXPEVT);
            break;
        }
    }

    // Return updated IRQ state.
    return cur_state;
}

// Hardware drivers which need init/free after IRQ setup.
void _dimm_comms_init();
void _dimm_comms_free();
void _vblank_init();
void _vblank_free();

uint32_t _holly_interrupt(irq_state_t *cur_state)
{
    // Interrupts we care about that we actually got this round.
    uint32_t serviced = 0;

    // Internal interrupt handler.
    {
        uint32_t requested = HOLLY_INTERNAL_IRQ_STATUS;
        uint32_t handled = 0;

        // First, check for any error status.
        if (requested & HOLLY_INTERNAL_INTERRUPT_CHECK_ERROR)
        {
            // If we were interested in ignoring certain errors, we could do so here
            // by setting the corresponding error bit to 1 inside HOLLY_ERROR_IRQ_STATUS
            // to clear the error and move on.
            _irq_display_exception(SIGINT, cur_state, "holly error interrupt fired", HOLLY_ERROR_IRQ_STATUS);
        }

        // Now, ignore any external interrupt set bits, since we will be checking
        // that as well in the next section of code.
        if (requested & HOLLY_INTERNAL_INTERRUPT_CHECK_EXTERNAL)
        {
            handled |= HOLLY_INTERNAL_INTERRUPT_CHECK_EXTERNAL;
        }

        // For some reason, even though we don't ask for it, HOLLY gives us IRQ finished
        // events for anything we tickle in the system, so we just reset those if we
        // encounter any of them.
        if (requested & HOLLY_INTERNAL_INTERRUPT_MAPLE_DMA_FINISHED)
        {
            // Request to clear the interrupt.
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_MAPLE_DMA_FINISHED;
            handled |= HOLLY_INTERNAL_INTERRUPT_MAPLE_DMA_FINISHED;
        }
        if (requested & HOLLY_INTERNAL_INTERRUPT_TSP_RENDER_FINISHED)
        {
            // Request to clear the interrupt.
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_TSP_RENDER_FINISHED;
            handled |= HOLLY_INTERNAL_INTERRUPT_TSP_RENDER_FINISHED;

            // Signal to the thread library to wake any waiting threads.
            serviced |= HOLLY_SERVICED_TSP_FINISHED;
        }
        if (requested & HOLLY_INTERNAL_INTERRUPT_ISP_RENDER_FINISHED)
        {
            // Request to clear the interrupt.
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_ISP_RENDER_FINISHED;
            handled |= HOLLY_INTERNAL_INTERRUPT_ISP_RENDER_FINISHED;
        }
        if (requested & HOLLY_INTERNAL_INTERRUPT_VIDEO_RENDER_FINISHED)
        {
            // Request to clear the interrupt.
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_VIDEO_RENDER_FINISHED;
            handled |= HOLLY_INTERNAL_INTERRUPT_VIDEO_RENDER_FINISHED;
        }
        if (requested & HOLLY_INTERNAL_INTERRUPT_TRANSFER_OPAQUE_FINISHED)
        {
            // Request to clear the interrupt.
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_TRANSFER_OPAQUE_FINISHED;
            handled |= HOLLY_INTERNAL_INTERRUPT_TRANSFER_OPAQUE_FINISHED;

            // Notify the thread system to wake any waiting threads.
            serviced |= HOLLY_SERVICED_TA_LOAD_OPAQUE_FINISHED;
        }
        if (requested & HOLLY_INTERNAL_INTERRUPT_TRANSFER_OPAQUE_MODIFIER_FINISHED)
        {
            // Request to clear the interrupt.
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_TRANSFER_OPAQUE_MODIFIER_FINISHED;
            handled |= HOLLY_INTERNAL_INTERRUPT_TRANSFER_OPAQUE_MODIFIER_FINISHED;
        }
        if (requested & HOLLY_INTERNAL_INTERRUPT_TRANSFER_TRANSPARENT_FINISHED)
        {
            // Request to clear the interrupt.
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_TRANSFER_TRANSPARENT_FINISHED;
            handled |= HOLLY_INTERNAL_INTERRUPT_TRANSFER_TRANSPARENT_FINISHED;

            // Notify the thread system to wake any waiting threads.
            serviced |= HOLLY_SERVICED_TA_LOAD_TRANSPARENT_FINISHED;
        }
        if (requested & HOLLY_INTERNAL_INTERRUPT_TRANSFER_TRANSPARENT_MODIFIER_FINISHED)
        {
            // Request to clear the interrupt.
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_TRANSFER_TRANSPARENT_MODIFIER_FINISHED;
            handled |= HOLLY_INTERNAL_INTERRUPT_TRANSFER_TRANSPARENT_MODIFIER_FINISHED;
        }
        if (requested & HOLLY_INTERNAL_INTERRUPT_TRANSFER_PUNCHTHRU_FINISHED)
        {
            // Request to clear the interrupt.
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_TRANSFER_PUNCHTHRU_FINISHED;
            handled |= HOLLY_INTERNAL_INTERRUPT_TRANSFER_PUNCHTHRU_FINISHED;

            // Notify the thread system to wake any waiting threads.
            serviced |= HOLLY_SERVICED_TA_LOAD_PUNCHTHRU_FINISHED;
        }
        if (requested & HOLLY_INTERNAL_INTERRUPT_TRANSFER_YUV_FINISHED)
        {
            // Request to clear the interrupt.
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_TRANSFER_YUV_FINISHED;
            handled |= HOLLY_INTERNAL_INTERRUPT_TRANSFER_YUV_FINISHED;
        }
        if (requested & HOLLY_INTERNAL_INTERRUPT_MAPLE_VBLANK_FINISHED)
        {
            // Request to clear the interrupt.
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_MAPLE_VBLANK_FINISHED;
            handled |= HOLLY_INTERNAL_INTERRUPT_MAPLE_VBLANK_FINISHED;
        }
        if (requested & HOLLY_INTERNAL_INTERRUPT_AICA_DMA_FINISHED)
        {
            // Request to clear the interrupt.
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_AICA_DMA_FINISHED;
            handled |= HOLLY_INTERNAL_INTERRUPT_AICA_DMA_FINISHED;
        }

        // Handle vblank in/out by making a request to the scheduler to wake
        // any threads waiting for this.
        if (requested & HOLLY_INTERNAL_INTERRUPT_VBLANK_IN)
        {
            // Request to clear the interrupt.
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_VBLANK_IN;
            handled |= HOLLY_INTERNAL_INTERRUPT_VBLANK_IN;

            // Signal to thread scheduler to wake any waiting threads.
            serviced |= HOLLY_SERVICED_VBLANK_IN;
        }
        if (requested & HOLLY_INTERNAL_INTERRUPT_VBLANK_OUT)
        {
            // Request to clear the interrupt.
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_VBLANK_OUT;
            handled |= HOLLY_INTERNAL_INTERRUPT_VBLANK_OUT;

            // Signal to thread scheduler to wake any waiting threads.
            serviced |= HOLLY_SERVICED_VBLANK_OUT;
        }
        if (requested & HOLLY_INTERNAL_INTERRUPT_HBLANK)
        {
            // Request to clear the interrupt.
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_HBLANK;
            handled |= HOLLY_INTERNAL_INTERRUPT_HBLANK;

            // Signal to thread scheduler to wake any waiting threads.
            serviced |= HOLLY_SERVICED_HBLANK;
        }

        uint32_t left = requested & (~handled);
        if (left)
        {
            _irq_display_invariant("uncaught holly internal interrupt", "pending irq status %08lx", left);
        }
    }

    // External interrupt handler.
    {
        uint32_t requested = HOLLY_EXTERNAL_IRQ_STATUS;
        uint32_t handled = 0;

        if ((requested & HOLLY_EXTERNAL_INTERRUPT_DIMM_COMMS) != 0)
        {
            halted = _dimm_command_handler(halted, cur_state);
            handled |= HOLLY_EXTERNAL_INTERRUPT_DIMM_COMMS;
            serviced |= HOLLY_SERVICED_DIMM_COMMS;
        }

        uint32_t left = requested & (~handled);
        if (left)
        {
            _irq_display_invariant("uncaught holly external interrupt", "pending irq status %08lx", left);
        }
    }

    return serviced;
}

irq_state_t * _irq_external_interrupt(irq_state_t *cur_state)
{
    stats.last_event = INTEVT;

    switch(INTEVT)
    {
        case IRQ_EVENT_TMU0:
        {
            int ret = _timer_interrupt(0);
            cur_state = _syscall_timer(cur_state, ret);
            break;
        }
        case IRQ_EVENT_TMU1:
        {
            int ret = _timer_interrupt(1);
            cur_state = _syscall_timer(cur_state, ret);
            break;
        }
        case IRQ_EVENT_TMU2:
        {
            int ret = _timer_interrupt(2);
            cur_state = _syscall_timer(cur_state, ret);
            break;
        }
        case IRQ_EVENT_HOLLY_LEVEL2:
        case IRQ_EVENT_HOLLY_LEVEL4:
        case IRQ_EVENT_HOLLY_LEVEL6:
        {
            uint32_t serviced = _holly_interrupt(cur_state);
            cur_state = _syscall_holly(cur_state, serviced);
            break;
        }
        default:
        {
            // Empty handler.
            _irq_display_exception(SIGINT, cur_state, "uncaught external interrupt", INTEVT);
            break;
        }
    }

    // Return updated IRQ state.
    return cur_state;
}

void _irq_handler(uint32_t source)
{
    // Mark that we're in interrupt context.
    in_interrupt = 1;

    // Keep track of stats for debugging purposes.
    stats.last_source = source;
    stats.num_interrupts ++;

    if (source == IRQ_SOURCE_GENERAL_EXCEPTION || source == IRQ_SOURCE_TLB_EXCEPTION)
    {
        // Regular exceptions as well as TLB miss exceptions.
        irq_state = _irq_general_exception(irq_state);
    }
    else if (source == IRQ_SOURCE_INTERRUPT)
    {
        // External interrupts.
        irq_state = _irq_external_interrupt(irq_state);
    }

    // Now, loop forever if we were requested to.
    while (halted)
    {
        halted = _dimm_command_handler(halted, irq_state);
    }

    // No longer need to mark this.
    in_interrupt = 0;
}

void _irq_init()
{
    // Save SR and VBR so we can restore them if we ever free.
    __asm__(
        "stc    sr,r0\n"
        "mov.l  r0,%0" : : "m"(saved_sr)
    );
    __asm__(
        "stc    vbr,r0\n"
        "mov.l  r0,%0" : : "m"(saved_vbr)
    );

    // Now, make sure interrupts are disabled.
    irq_disable();

    // Make sure we aren't halted.
    halted = 0;
    in_interrupt = 0;
    disable_debugging = 0;

    // Initialize our stats.
    stats.last_source = 0;
    stats.last_event = 0;
    stats.num_interrupts = 0;

    // Allocate space for our interrupt state.
    irq_state = malloc(sizeof(irq_state_t));
    if (irq_state == 0)
    {
        _irq_display_invariant("memory failure", "could not get memory for base task IRQ state!");
    }
    memset(irq_state, 0, sizeof(irq_state_t));

    // Register the default state with threads since the current
    // running code at the time of init becomes a "thread" as such.
    _thread_register_main(irq_state);

    // Allocate space for our interrupt handler stack.
    irq_stack = malloc(IRQ_STACK_SIZE);
    if (irq_stack == 0)
    {
        _irq_display_invariant("memory failure", "could not get memory for IRQ handler stack!");
    }
    irq_stack += IRQ_STACK_SIZE;

    // Finally, set the VBR to our vector table.
    _irq_set_vector_table();

    // Disable all HOLLY internal interrupts unless explicitly enabled.
    // Also cancel any pending interrupts.
    HOLLY_INTERNAL_IRQ_2_MASK = 0;
    HOLLY_INTERNAL_IRQ_4_MASK = 0;
    HOLLY_INTERNAL_IRQ_6_MASK = 0;
    HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_IRQ_STATUS;

    // Disable all HOLLY external interrupts unless explicitly enabled.
    // We can't cancel any pending interrupts because that's done by
    // interacting with the external hardware generating the interurpt.
    HOLLY_EXTERNAL_IRQ_2_MASK = 0;
    HOLLY_EXTERNAL_IRQ_4_MASK = 0;
    HOLLY_EXTERNAL_IRQ_6_MASK = 0;

    // Kill any pending HOLLY errors.
    HOLLY_ERROR_IRQ_STATUS = HOLLY_ERROR_IRQ_STATUS;

    // Disable all HOLLY external error interrupts unless later specifically
    // enabled. This will mean that individual hardware drivers must enable
    // errors they care about.
    HOLLY_ERROR_IRQ_2_MASK = 0;
    HOLLY_ERROR_IRQ_4_MASK = 0;
    HOLLY_ERROR_IRQ_6_MASK = 0;

    // Allow timer interrupts, ignore RTC interrupts.
    INTC_IPRA = 0xFFF0;

    // Ignore WDT and SCIF2 interrupts.
    INTC_IPRB = 0x0000;

    // Ignore SCIF1 and UDI interrupts.
    INTC_IPRC = 0x0000;

    // Allow IRL1-2 interrupts so we can receive interrupts from HOLLY.
    INTC_IPRD = 0x0FF0;

    // Keep track of the state that we free ourselves so that thread.c doesn't
    // double-free it.
    irq_freed_state = 0;

    // Now, enable interrupts for the whole system!
    _irq_enable();

    // Finally, create an idle thread. We can only do this here because
    // if we do it anywhere else the idle thread will get the wrong VBR
    // and SR and the first time we enter it we will freeze forever.
    _thread_create_idle();

    // Now, set up hardware that needs interrupts from HOLLY
    _vblank_init();
    _dimm_comms_init();
}

void _irq_free()
{
    // Restore SR and VBR to their pre-init state.
    __asm__(
        "mov.l  %0,r0\n"
        "ldc    r0,sr" : : "m"(saved_sr)
    );
    __asm__(
        "mov.l  %0,r0\n"
        "ldc    r0,vbr" : : "m"(saved_vbr)
    );

    // Tear down hardware that needed interrupts from HOLLY.
    _dimm_comms_free();
    _vblank_free();

    // Disable any masks that we previously had set, acknowledge all IRQs.
    HOLLY_INTERNAL_IRQ_2_MASK = 0;
    HOLLY_INTERNAL_IRQ_4_MASK = 0;
    HOLLY_INTERNAL_IRQ_6_MASK = 0;
    HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_IRQ_STATUS;
    HOLLY_EXTERNAL_IRQ_2_MASK = 0;
    HOLLY_EXTERNAL_IRQ_4_MASK = 0;
    HOLLY_EXTERNAL_IRQ_6_MASK = 0;
    HOLLY_ERROR_IRQ_STATUS = HOLLY_ERROR_IRQ_STATUS;
    HOLLY_ERROR_IRQ_2_MASK = 0;
    HOLLY_ERROR_IRQ_4_MASK = 0;
    HOLLY_ERROR_IRQ_6_MASK = 0;
    INTC_IPRA = 0x0000;
    INTC_IPRB = 0x0000;
    INTC_IPRC = 0x0000;
    INTC_IPRD = 0x0000;

    // Now, get rid of our interrupt stack.
    irq_stack -= IRQ_STACK_SIZE;
    free(irq_stack);
    irq_stack = 0;

    // Keep track of the state that we free ourselves so that thread.c doesn't
    // double-free it.
    irq_freed_state = irq_state;

    // Now, get rid of our interrupt state.
    free(irq_state);
    irq_state = 0;
}

irq_state_t *_irq_new_state(thread_func_t func, void *funcparam, void *stackptr, void *threadptr)
{
    uint32_t old_interrupts = irq_disable();

    // Allocate space for our interrupt state.
    irq_state_t *new_state = malloc(sizeof(irq_state_t));
    if (new_state == 0)
    {
        _irq_display_invariant("memory failure", "could not get memory for new task IRQ state!");
    }
    memset(new_state, 0, sizeof(irq_state_t));

    // Now, set up the starting state.
    new_state->pc = (uint32_t)func;
    new_state->gp_regs[4] = (uint32_t)funcparam;
    new_state->gp_regs[15] = (uint32_t)stackptr;
    new_state->sr = _irq_read_sr() & 0xcfffff0f;
    new_state->vbr = _irq_read_vbr();
    new_state->fpscr = 0x40000;
    new_state->threadptr = threadptr;

    // Now, re-enable interrupts and return the state.
    irq_restore(old_interrupts);
    return new_state;
}

void _irq_free_state(irq_state_t *state)
{
    if (state != irq_state && state != irq_freed_state)
    {
        free(state);
    }
}

irq_stats_t irq_stats()
{
    irq_stats_t statscopy;

    uint32_t saved_interrupts = irq_disable();
    memcpy(&statscopy, &stats, sizeof(irq_stats_t));
    irq_restore(saved_interrupts);

    return statscopy;
}

int _irq_is_disabled(uint32_t sr)
{
    return (sr & 0x10000000) != 0 ? 1 : 0;
}
