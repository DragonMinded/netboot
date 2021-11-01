#ifndef __IRQSTATE_H
#define __IRQSTATE_H

#include <stdint.h>

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

irq_state_t *_irq_new_state(thread_func_t func, void *funcparam, void *stackptr);
void _irq_free_state(irq_state_t *state);

irq_state_t *_syscall_trapa(irq_state_t *state, unsigned int which);
irq_state_t *_syscall_timer(irq_state_t *state, int timer);

void _thread_register_main(irq_state_t *state);

void _irq_display_exception(irq_state_t *cur_state, char *failure, int code);
void _irq_display_invariant(char *msg, char *failure, ...);

#endif
