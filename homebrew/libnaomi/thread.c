#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "naomi/interrupts.h"
#include "naomi/thread.h"
#include "irqstate.h"

static uint32_t *global_counters[MAX_GLOBAL_COUNTERS];

uint32_t *_global_counter_find(uint32_t counter)
{
    for (unsigned int i = 0; i < MAX_GLOBAL_COUNTERS; i++)
    {
        if (global_counters[i] != 0 && global_counters[i] == (uint32_t *)counter)
        {
            return global_counters[i];
        }
    }

    return 0;
}

void _thread_init()
{
    memset(global_counters, 0, sizeof(uint32_t) * MAX_GLOBAL_COUNTERS);
}

void _thread_free()
{
    // Nothing for now!
}

irq_state_t *_syscall_timer(irq_state_t *current, int timer)
{
    return current;
}

irq_state_t *_syscall_trapa(irq_state_t *current, unsigned int which)
{
    switch (which)
    {
        case 0:
        {
            uint32_t *counter = _global_counter_find(current->gp_regs[4]);
            if (counter) { *counter = *counter + 1; }
            break;
        }
        case 1:
        {
            uint32_t *counter = _global_counter_find(current->gp_regs[4]);
            if (counter && *counter > 0) { *counter = *counter - 1; }
            break;
        }
        case 2:
        {
            uint32_t *counter = _global_counter_find(current->gp_regs[4]);
            if (counter)
            {
                current->gp_regs[0] = *counter;
            }
            else
            {
                current->gp_regs[0] = 0;
            }
            break;
        }
    }

    return current;
}

void *global_counter_init(uint32_t initial_value)
{
    uint32_t old_interrupts = irq_disable();
    uint32_t *counter = 0;

    for (unsigned int i = 0; i < MAX_GLOBAL_COUNTERS; i++)
    {
        if (global_counters[i] == 0)
        {
            counter = malloc(sizeof(uint32_t));
            *counter = initial_value;
            global_counters[i] = counter;
            break;
        }
    }

    irq_restore(old_interrupts);
    return counter;
}

void global_counter_increment(void *counter)
{
    asm("trapa #0");
}

void global_counter_decrement(void *counter)
{
    asm("trapa #1");
}

uint32_t global_counter_value(void *counter)
{
    register uint32_t syscall_return asm("r0");

    asm("trapa #2");

    return syscall_return;
}

void global_counter_free(void *counter)
{
    uint32_t old_interrupts = irq_disable();

    for (unsigned int i = 0; i < MAX_GLOBAL_COUNTERS; i++)
    {
        if (global_counters[i] != 0 && global_counters[i] == counter)
        {
            global_counters[i] = 0;
            free(counter);
            break;
        }
    }

    irq_restore(old_interrupts);
}
