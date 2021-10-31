#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "naomi/interrupts.h"
#include "naomi/timer.h"

// Saved state of SR and VBR when we initialize interrupts. We will use these
// to restore SR/VBR when de-initializing again.
static uint32_t saved_sr;
static uint32_t saved_vbr;

// Should match up with save and restore code in sh-crt0.s.
typedef struct
{
    // General purpose registers R0-R15 (R15 being the stack).
    uint32_t gp_regs[16];

    // Saved program counter where the interrupt occured.
    uint32_t pc;

    // Saved procedure return address.
    uint32_t pr;

    // Saved global base address.
    uint32_t gbr;

    // Saved vector base address.
    uint32_t vbr;

    // Saved Multiply-accumulate high/low registers.
    uint32_t mach;
    uint32_t macl;

    // Saved SR.
    uint32_t sr;

    // Saved floating point banked and regular registers.
    uint32_t frbank[16];
    uint32_t fr[16];

    // Saved floating point status and communication registers.
    uint32_t fpscr;
    uint32_t fpul;
} irq_state_t;

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

void _irq_general_exception()
{
    // TODO: Handle TRAPA instructions by passing to a user handler.

    // TODO: Handle other general purpose exceptions by displaying a
    // debug screen and freezing.
    stats.last_event = EXPEVT;
}

// Prototypes of functions we don't want in the public headers.
void _irq_set_vector_table();
void _timer_interrupt(int timer);
uint32_t _irq_enable();

void _irq_external_interrupt()
{
    stats.last_event = INTEVT;

    switch(INTEVT)
    {
        case IRQ_EVENT_TMU0:
            _timer_interrupt(0);
            break;
        case IRQ_EVENT_TMU1:
            _timer_interrupt(1);
            break;
        case IRQ_EVENT_TMU2:
            _timer_interrupt(2);
            break;
        default:
            // Empty handler.
            break;
    }
}

void _irq_handler(uint32_t source)
{
    stats.last_source = source;
    stats.num_interrupts ++;

    if (source == IRQ_SOURCE_GENERAL_EXCEPTION || source == IRQ_SOURCE_TLB_EXCEPTION)
    {
        // Regular exceptions as well as TLB miss exceptions.
        _irq_general_exception();
    }
    else if (source == IRQ_SOURCE_INTERRUPT)
    {
        // External interrupts.
        _irq_external_interrupt();
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

irq_stats_t irq_get_stats()
{
    irq_stats_t statscopy;

    uint32_t saved_interrupts = irq_disable();
    memcpy(&statscopy, &stats, sizeof(irq_stats_t));
    irq_restore(saved_interrupts);

    return statscopy;
}
