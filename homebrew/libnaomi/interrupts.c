#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "naomi/interrupts.h"
#include "naomi/timer.h"
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

irq_state_t * _irq_general_exception(irq_state_t *cur_state)
{
    // TODO: Handle other general purpose exceptions by displaying a
    // debug screen and freezing.
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
            break;
        }
    }

    // Return updated IRQ state.
    return cur_state;
}

// Prototypes of functions we don't want in the public headers.
void _irq_set_vector_table();
void _timer_interrupt(int timer);
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
            _timer_interrupt(0);
            cur_state = _syscall_timer(cur_state, 0);
            break;
        }
        case IRQ_EVENT_TMU1:
        {
            _timer_interrupt(1);
            cur_state = _syscall_timer(cur_state, 1);
            break;
        }
        case IRQ_EVENT_TMU2:
        {
            _timer_interrupt(2);
            cur_state = _syscall_timer(cur_state, 2);
            break;
        }
        default:
        {
            // Empty handler.
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

irq_state_t *_irq_new_state(thread_func_t func, void *funcparam, void *stackptr, void *returnaddr)
{
    uint32_t old_interrupts = irq_disable();

    // Allocate space for our interrupt state.
    irq_state_t *new_state = malloc(sizeof(irq_state_t));
    memset(new_state, 0, sizeof(irq_state_t));

    // Now, set up the starting state.
    new_state->pc = (uint32_t)func;
    new_state->pr = (uint32_t)returnaddr;
    new_state->gp_regs[4] = (uint32_t)funcparam;
    new_state->gp_regs[15] = (uint32_t)stackptr;
    new_state->sr = _irq_read_sr() & 0xcfffff0f;
    new_state->vbr = _irq_read_vbr();
    new_state->fpscr = 0x40000;

    // Now, re-enable interrupts and return the state.
    irq_restore(old_interrupts);
    return new_state;
}

irq_stats_t irq_get_stats()
{
    irq_stats_t statscopy;

    uint32_t saved_interrupts = irq_disable();
    memcpy(&statscopy, &stats, sizeof(irq_stats_t));
    irq_restore(saved_interrupts);

    return statscopy;
}
