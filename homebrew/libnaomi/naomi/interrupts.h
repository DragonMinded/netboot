#ifndef __INTERRUPTS_H
#define __INTERRUPTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Initialize or de-initialize the interrupt subsystem. Do not call these
// directly as they are handled by the C runtime for you.
void irq_init();
void irq_free();

// Ensure interrupts are disabled, returning the old SR. Do not re-enable
// interrupts after calling this as you could already be in a disabled state.
// Instead, call irq_restore() with the return value from irq_disable().
uint32_t irq_disable();

// Ensure interrupts are enabled. You should not call this function directly
// as it is dangerous. Instead, let the C runtime handle it for you.
uint32_t irq_enable();

// Restore interrupts, after calling irq_disable().
void irq_restore(uint32_t oldstate);

// Statistics about interrupts on the system.
typedef struct
{
    // The last interrupt source.
    uint32_t last_source;

    // The last interrupt event.
    uint32_t last_event;

    // The number of interrupts the system has seen.
    uint32_t num_interrupts;
} irq_stats_t;

#define IRQ_SOURCE_GENERAL_EXCEPTION 0x100
#define IRQ_SOURCE_TLB_EXCEPTION 0x400
#define IRQ_SOURCE_INTERRUPT 0x600

#define IRQ_EVENT_TRAPA 0x160
#define IRQ_EVENT_NMI 0x1C0

irq_stats_t irq_get_stats();

#ifdef __cplusplus
}
#endif

#endif
