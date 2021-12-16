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
uint32_t _irq_enable();

// What interrupts we actually serviced in our handler.
#define HOLLY_SERVICED_DIMM_COMMS 0x00000001
#define HOLLY_SERVICED_VBLANK_IN 0x00000002
#define HOLLY_SERVICED_VBLANK_OUT 0x00000004
#define HOLLY_SERVICED_HBLANK 0x00000008
#define HOLLY_SERVICED_TSP_FINISHED 0x00000010
#define HOLLY_SERVICED_TA_LOAD_OPAQUE_FINISHED 0x00000020
#define HOLLY_SERVICED_TA_LOAD_TRANSPARENT_FINISHED 0x00000040
#define HOLLY_SERVICED_TA_LOAD_PUNCHTHRU_FINISHED 0x00000080

irq_state_t *_syscall_trapa(irq_state_t *state, unsigned int which);
irq_state_t *_syscall_timer(irq_state_t *state, int timer);
irq_state_t *_syscall_holly(irq_state_t *current, uint32_t serviced_holly_interrupts);

void _thread_create_idle();
void _thread_register_main(irq_state_t *state);
uint64_t _profile_get_current(uint32_t adjustments);

void _irq_display_exception(int signal, irq_state_t *cur_state, char *failure, int code);

// Prototype to force thread system to disable preemption, used for safely
// re-enabling interrupts to capture registers on an abort call.
void _thread_disable_switching();

// Definition of IRQ state for C code, its actually in sh-crt0.s
extern irq_state_t *irq_state;

// Definition of in/out of interrupt. This is not threadsafe and should
// only be checked by kernel code.
extern int _irq_in_interrupt;

// Syscall for capturing registers if we need it.
#define _irq_capture_regs_syscall() asm("trapa #254")

// Macro for capturing registers of current call frame. This isn't a function
// so that any stack trace captured using it doesn't include yet another unnecessary
// frame.
#define _irq_capture_regs(capture) \
do \
{ \
    /* Force thread system to only ever run us from now on. */ \
    _thread_disable_switching(); \
    \
    /* Make sure that interrupts are already initialized. If not, we have no chance
     * of capturing proper stack traces. */ \
    if (irq_state != 0) \
    { \
        /* We only care to do anything below if we aren't in interrupt context. If
         * we are, we already have the correct stack trace in irq_state so we don't
         * need to do any gymnastics to get registers so GDB can perform backtraces. */ \
        if (!_irq_in_interrupt) \
        { \
            if (_irq_is_disabled(_irq_get_sr())) \
            { \
                /* Re-enable interrupts so that we can call a syscall to force the
                 * current register block to be correct. This ensures that stack
                 * traces are correct even when interrupts are disabled when we hit
                 * an invariant call. */ \
                _irq_enable(); \
            } \
            \
            /* Capture registers for backtraces to make sense if we were called
             * in user context instead of interrupt context. */ \
            _irq_capture_regs_syscall(); \
        } \
        \
        /* Finally if we were requested to copy this out, do so now. */ \
        if ((unsigned int)capture != 0) \
        { \
            memcpy(capture, irq_state, sizeof(irq_state_t)); \
        } \
    } \
} while(0)

#include "irqinternal.h"

#endif
