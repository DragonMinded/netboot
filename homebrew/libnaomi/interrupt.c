#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "naomi/interrupt.h"
#include "naomi/video.h"
#include "naomi/console.h"
#include "naomi/thread.h"
#include "irqstate.h"

// Saved state of SR and VBR when we initialize interrupts. We will use these
// to restore SR/VBR when de-initializing again.
static uint32_t saved_sr;
static uint32_t saved_vbr;

// Size we wish our stack to be.
#define IRQ_STACK_SIZE 16384

// Definitions from sh-crt0.s that we use here.
extern uint8_t *irq_stack;
extern irq_state_t *irq_state;

#define TRA *((volatile uint32_t *)0xFF000020)
#define EXPEVT *((volatile uint32_t *)0xFF000024)
#define INTEVT *((volatile uint32_t *)0xFF000028)

#define INTC_BASE_ADDRESS 0xFFD00000

#define INTC_IPRA *((volatile uint16_t *)(INTC_BASE_ADDRESS + 0x04))
#define INTC_IPRB *((volatile uint16_t *)(INTC_BASE_ADDRESS + 0x08))
#define INTC_IPRC *((volatile uint16_t *)(INTC_BASE_ADDRESS + 0x0C))
#define INTC_IPRD *((volatile uint16_t *)(INTC_BASE_ADDRESS + 0x10))

static irq_stats_t stats;
static char exception_buffer[1024];

void _irq_display_exception(irq_state_t *cur_state, char *failure, int code)
{
    // Threads should already be disabled, but lets be sure.
    uint32_t old_interrupts = irq_disable();

    video_init_simple();
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

    video_draw_debug_text(32, 32, rgb(255, 255, 255), exception_buffer);
    video_display_on_vblank();

    while( 1 ) { ; }

    irq_restore(old_interrupts);
}

void _irq_display_invariant(char *msg, char *failure, ...)
{
    // Threads should already be disabled, but lets be sure.
    uint32_t old_interrupts = irq_disable();

    video_init_simple();
    console_set_visible(0);
    video_set_background_color(rgb(48, 0, 0));

    sprintf(exception_buffer, "INVARIANT VIOLATION: %s", msg);
    video_draw_debug_text(32, 32, rgb(255, 255, 255), exception_buffer);

    va_list args;
    va_start(args, failure);
    int length = vsnprintf(exception_buffer, 1023, failure, args);
    va_end(args);

    if (length > 0)
    {
        exception_buffer[length < 1023 ? length : 1023] = 0;
        video_draw_debug_text(32, 32 + (8 * 2), rgb(255, 255, 255), exception_buffer);
    }

    video_display_on_vblank();

    while( 1 ) { ; }

    irq_restore(old_interrupts);
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
            cur_state = _syscall_trapa(cur_state, which);

            break;
        }
        default:
        {
            // Empty handler.
            _irq_display_exception(cur_state, "uncaught general exception", EXPEVT);
            break;
        }
    }

    // Return updated IRQ state.
    return cur_state;
}

// Prototypes of functions we don't want in the public headers.
void _irq_set_vector_table();
int _timer_interrupt(int timer);
uint32_t _irq_enable();
uint32_t _irq_read_sr();
uint32_t _irq_read_vbr();

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
        default:
        {
            // Empty handler.
            _irq_display_exception(cur_state, "uncaught external interrupt", INTEVT);
            break;
        }
    }

    // Return updated IRQ state.
    return cur_state;
}

void _irq_handler(uint32_t source)
{
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

    // Initialize our stats.
    stats.last_source = 0;
    stats.last_event = 0;
    stats.num_interrupts = 0;

    // Allocate space for our interrupt state.
    irq_state = malloc(sizeof(irq_state_t));
    memset(irq_state, 0, sizeof(irq_state_t));

    // Register the default state with threads since the current
    // running code at the time of init becomes a "thread" as such.
    _thread_register_main(irq_state);

    // Allocate space for our interrupt handler stack.
    irq_stack = malloc(IRQ_STACK_SIZE);
    irq_stack += IRQ_STACK_SIZE;

    // Finally, set the VBR to our vector table.
    _irq_set_vector_table();

    // Allow timer interrupts, ignore RTC interrupts.
    INTC_IPRA = 0xFFF0;

    // Ignore WDT and SCIF2 interrupts.
    INTC_IPRB = 0x0000;

    // Ignore SCIF1 and UDI interrupts.
    INTC_IPRC = 0x0000;

    // Ignore IRL0-IRL3 interrupts.
    INTC_IPRD = 0x0000;

    // Now, enable interrupts for the whole system!
    _irq_enable();
}

void _irq_free()
{
    // TODO: This should only ever be called from the main thread. We should
    // verify that the current irq_state is the main thread with the threads
    // module, and if not display an error message to the screen.

    // Restore SR and VBR to their pre-init state.
    __asm__(
        "mov.l  %0,r0\n"
        "ldc    r0,sr" : : "m"(saved_sr)
    );
    __asm__(
        "mov.l  %0,r0\n"
        "ldc    r0,vbr" : : "m"(saved_vbr)
    );

    // Now, get rid of our interrupt stack.
    irq_stack -= IRQ_STACK_SIZE;
    free(irq_stack);
    irq_stack = 0;

    // Now, get rid of our interrupt state.
    free(irq_state);
    irq_state = 0;
}

irq_state_t *_irq_new_state(thread_func_t func, void *funcparam, void *stackptr)
{
    uint32_t old_interrupts = irq_disable();

    // Allocate space for our interrupt state.
    irq_state_t *new_state = malloc(sizeof(irq_state_t));
    memset(new_state, 0, sizeof(irq_state_t));

    // Now, set up the starting state.
    new_state->pc = (uint32_t)func;
    new_state->gp_regs[4] = (uint32_t)funcparam;
    new_state->gp_regs[15] = (uint32_t)stackptr;
    new_state->sr = _irq_read_sr() & 0xcfffff0f;
    new_state->vbr = _irq_read_vbr();
    new_state->fpscr = 0x40000;

    // Now, re-enable interrupts and return the state.
    irq_restore(old_interrupts);
    return new_state;
}

void _irq_free_state(irq_state_t *state)
{
    if (state == irq_state)
    {
        // We tried to free our current state. That can't happen since
        // we don't have anything else to go back to.
        _irq_display_invariant("irq failure", "tried to free our own state");
    }

    free(state);
}

irq_stats_t irq_stats()
{
    irq_stats_t statscopy;

    uint32_t saved_interrupts = irq_disable();
    memcpy(&statscopy, &stats, sizeof(irq_stats_t));
    irq_restore(saved_interrupts);

    return statscopy;
}

int _irq_was_disabled(uint32_t sr)
{
    return (sr & 0x10000000) != 0 ? 1 : 0;
}
