#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "naomi/interrupts.h"

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
extern void irq_set_vector_table();

#define EXPEVT *((volatile uint32_t *)0xFF000024)
#define INTEVT *((volatile uint32_t *)0xFF000028)

static irq_stats_t stats;

void irq_general_exception()
{
    // TODO: Handle TRAPA instructions by passing to a user handler.

    // TODO: Handle other general purpose exceptions by displaying a
    // debug screen and freezing.
    stats.last_event = EXPEVT;
}

#define CODE_NMI 0x1C0

void irq_external_interrupt()
{
    stats.last_event = INTEVT;

    switch(INTEVT)
    {
        default:
            // Empty handler.
            break;
    }
}

void irq_handler(uint32_t source)
{
    stats.last_source = source;
    stats.num_interrupts ++;

    if (source == IRQ_SOURCE_GENERAL_EXCEPTION || source == IRQ_SOURCE_TLB_EXCEPTION)
    {
        // Regular exceptions as well as TLB miss exceptions.
        irq_general_exception();
    }
    else if (source == IRQ_SOURCE_INTERRUPT)
    {
        // External interrupts.
        irq_external_interrupt();
    }
}

void irq_init()
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
    irq_set_vector_table();

    // Now, enable interrupts for the whole system!
    irq_enable();
}

void irq_free()
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
