#ifndef __IRQSTATE_H
#define __IRQSTATE_H

#include <stdint.h>

#define MICROSECONDS_IN_ONE_SECOND 1000000
#define PREEMPTION_HZ 1000

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

    // Pointer to the thread that owns us, potentially.
    void *threadptr;
} irq_state_t;

irq_state_t *_irq_new_state(thread_func_t func, void *funcparam, void *stackptr, void *threadptr);
void _irq_free_state(irq_state_t *state);

// What interrupts we actually serviced in our handler.
#define HOLLY_SERVICED_DIMM_COMMS 0x00000001
#define HOLLY_SERVICED_VBLANK_IN 0x00000002
#define HOLLY_SERVICED_VBLANK_OUT 0x00000004
#define HOLLY_SERVICED_HBLANK 0x00000008

irq_state_t *_syscall_trapa(irq_state_t *state, unsigned int which);
irq_state_t *_syscall_timer(irq_state_t *state, int timer);
irq_state_t *_syscall_holly(irq_state_t *current, uint32_t serviced_holly_interrupts);

void _thread_create_idle();
void _thread_register_main(irq_state_t *state);
uint64_t _profile_get_current(uint32_t adjustments);

void _irq_display_exception(irq_state_t *cur_state, char *failure, int code);

#include "irqinternal.h"

#endif
