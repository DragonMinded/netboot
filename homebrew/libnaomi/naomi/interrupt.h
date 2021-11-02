#ifndef __INTERRUPT_H
#define __INTERRUPT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Ensure interrupts are disabled, returning the old SR. When you are done with
// the code that needs exclusive HW access, restore interrupts with irq_restore().
uint32_t irq_disable();

// Restore interrupts, after calling irq_disable().
void irq_restore(uint32_t oldstate);

// Run an enclosed statement atomically.
#define ATOMIC(stmt) \
do { \
    uint32_t old_ints = irq_disable(); \
    stmt; \
    irq_restore(old_ints); \
} while( 0 )

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
#define IRQ_EVENT_HOLLY_LEVEL6 0x0320
#define IRQ_EVENT_HOLLY_LEVEL4 0x0360
#define IRQ_EVENT_HOLLY_LEVEL2 0x03A0
#define IRQ_EVENT_TMU0 0x400
#define IRQ_EVENT_TMU1 0x420
#define IRQ_EVENT_TMU2 0x440

irq_stats_t irq_stats();

#ifdef __cplusplus
}
#endif

#endif
