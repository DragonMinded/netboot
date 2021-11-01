#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "naomi/interrupt.h"
#include "naomi/thread.h"
#include "irqstate.h"

// Thread hasn't been started yet, or thread_stop() was called on thread.
#define THREAD_STATE_STOPPED 0

// Thread is running.
#define THREAD_STATE_RUNNING 1

// Thread is finished running, but hasn't been thread_join()ed yet.
#define THREAD_STATE_FINISHED 2

// Thread is finished running, and has been thread_join()ed on.
#define THREAD_STATE_ZOMBIE 3

// Thread is waiting for a resource.
#define THREAD_STATE_WAITING 4

typedef struct
{
    // Basic thread stuff.
    char name[64];
    uint32_t id;
    int priority;
    int state;

    // Any resources this thread is waiting on.
    void *waiting_semaphore;
    uint32_t waiting_thread;

    // The actual context of the thread, including all of the registers and such.
    int main_thread;
    irq_state_t *context;
    uint8_t *stack;
    void *retval;
} thread_t;

static thread_t *threads[MAX_THREADS];

thread_t *_thread_find_by_context(irq_state_t *context)
{
    for (unsigned int i = 0; i < MAX_THREADS; i++)
    {
        if (threads[i] != 0 && threads[i]->context == context)
        {
            return threads[i];
        }
    }

    return 0;
}

thread_t *_thread_find_by_id(uint32_t id)
{
    for (unsigned int i = 0; i < MAX_THREADS; i++)
    {
        if (threads[i] != 0 && threads[i]->id == id)
        {
            return threads[i];
        }
    }

    return 0;
}

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

typedef struct
{
    uint32_t max;
    uint32_t current;
} semaphore_t;

static semaphore_t *semaphores[MAX_SEMAPHORES];

semaphore_t *_semaphore_find(uint32_t semaphore)
{
    for (unsigned int i = 0; i < MAX_SEMAPHORES; i++)
    {
        if (semaphores[i] != 0 && semaphores[i] == (semaphore_t *)semaphore)
        {
            return semaphores[i];
        }
    }

    return 0;
}

void * _idle_thread(void *param)
{
    while ( 1 ) { thread_yield(); }

    return 0;
}

uint32_t thread_counter;

thread_t *_thread_create(char *name, int priority)
{
    uint32_t old_interrupts = irq_disable();
    thread_t *thread = 0;

    for (unsigned int i = 0; i < MAX_THREADS; i++)
    {
        if (threads[i] == 0)
        {
            thread = malloc(sizeof(thread_t));
            memset(thread, 0, sizeof(thread_t));

            thread->id = thread_counter++;
            thread->priority = priority;
            thread->state = THREAD_STATE_STOPPED;
            strncpy(thread->name, name, 63);

            threads[i] = thread;
            break;
        }
    }

    irq_restore(old_interrupts);

    return thread;
}

void _thread_destroy(thread_t *thread)
{
    if (thread->main_thread == 0)
    {
        if (thread->context)
        {
            _irq_free_state(thread->context);
            thread->context = 0;
        }
        if (thread->stack)
        {
            free(thread->stack);
            thread->stack = 0;
        }
    }
    free(thread);
}

void _thread_register_main(irq_state_t *state)
{
    uint32_t old_interrupts = irq_disable();

    thread_t *main_thread = _thread_create("main", 0);
    main_thread->stack = (uint8_t *)0x0E000000;
    main_thread->context = state;
    main_thread->state = THREAD_STATE_RUNNING;
    main_thread->main_thread = 1;

    irq_restore(old_interrupts);
}

#define THREAD_SCHEDULE_CURRENT 0
#define THREAD_SCHEDULE_OTHER 1
#define THREAD_SCHEDULE_ANY 2

irq_state_t *_thread_schedule(irq_state_t *state, int request)
{
    thread_t *current_thread = _thread_find_by_context(state);

    if (current_thread == 0)
    {
        // Should never happen.
        _irq_display_exception(state, "cannot locate current thread to schedule", -1);
        return state;
    }

    // Schedule a new thread at this point.
    if (request == THREAD_SCHEDULE_CURRENT)
    {
        // See if the current thread is applicable to run.
        if (current_thread->state == THREAD_STATE_RUNNING)
        {
            // It is, just return it.
            return current_thread->context;
        }
    }

    // Set the max priority to the idle thread, so if we don't find any
    // applicable threads then we will choose the idle thread instead.
    int priority = INT_MIN;

    // Go through and find the highest priority that is schedulable.
    for (unsigned int i = 0; i < MAX_THREADS; i++)
    {
        if (threads[i] == 0)
        {
            // Not a real thread.
            continue;
        }

        if (threads[i]->state != THREAD_STATE_RUNNING)
        {
            // This thread isn't runnable.
            continue;
        }

        if (request == THREAD_SCHEDULE_OTHER && threads[i] == current_thread)
        {
            // Don't include this thread, we specifically requested going to the next thread.
            continue;
        }

        // Bump the max priority based on this schedulable thread.
        priority = priority > threads[i]->priority ? priority : threads[i]->priority;
    }

    // Now, round robin within the priority band.
    int found = 0;
    for (unsigned int i = 0; i < MAX_THREADS; i++)
    {
        if (threads[i] == 0)
        {
            // Not a real thread.
            continue;
        }

        if (threads[i]->state != THREAD_STATE_RUNNING)
        {
            // This thread isn't runnable.
            continue;
        }

        if (threads[i]->priority != priority)
        {
            // Don't care, not the band we're after.
            continue;
        }

        if (found)
        {
            // Okay, we found our current thread last iteration, so this is
            // the next applicable thread in a round-robin scheduler.
            return threads[i]->context;
        }

        if (threads[i] == current_thread)
        {
            // We found our thread, return the next thread on the next iteration.
            found = 1;
        }
    }

    // If we got here, then the next available thread is before our current thread.
    // Just run again and select the first applicable thread. This has the chance
    // of selecting ourselves if there is no applicable other thread, even if the
    // request is THREAD_SCHEDULE_OTHER. That should only happen when it is the idle
    // thread, however, since at any other moment we would have chosen a different
    // priority band.
    for (unsigned int i = 0; i < MAX_THREADS; i++)
    {
        if (threads[i] == 0)
        {
            // Not a real thread.
            continue;
        }

        if (threads[i]->state != THREAD_STATE_RUNNING)
        {
            // This thread isn't runnable.
            continue;
        }

        if (threads[i]->priority != priority)
        {
            // Don't care, not the band we're after.
            continue;
        }

        // Okay, we found an applicable thread, return it as the scheduled thread.
        return threads[i]->context;
    }

    // We should never ever get here, but if so just return the current state.
    _irq_display_exception(state, "cannot locate new thread to schedule", -1);
    return state;
}

void _thread_init()
{
    thread_counter = 1;
    memset(global_counters, 0, sizeof(uint32_t *) * MAX_GLOBAL_COUNTERS);
    memset(semaphores, 0, sizeof(semaphore_t *) * MAX_SEMAPHORES);
    memset(threads, 0, sizeof(thread_t *) * MAX_THREADS);

    // Create an idle thread.
    thread_t *idle_thread = _thread_create("idle", INT_MIN);
    idle_thread->stack = malloc(64);
    idle_thread->context = _irq_new_state(_idle_thread, 0, idle_thread->stack + 64);
    idle_thread->state = THREAD_STATE_RUNNING;
}

void _thread_free()
{
    uint32_t old_interrupts = irq_disable();

    for (unsigned int i = 0; i < MAX_GLOBAL_COUNTERS; i++)
    {
        if (global_counters[i] != 0)
        {
            free(global_counters[i]);
            global_counters[i] = 0;
        }
    }

    for (unsigned int i = 0; i < MAX_SEMAPHORES; i++)
    {
        if (semaphores[i] != 0)
        {
            free(semaphores[i]);
            semaphores[i] = 0;
        }
    }

    for (unsigned int i = 0; i < MAX_THREADS; i++)
    {
        if (threads[i] != 0)
        {
            _thread_destroy(threads[i]);
            threads[i] = 0;
        }
    }

    irq_restore(old_interrupts);
}

#define WAKE_TYPE_THREADID 1

void _thread_wake_others(thread_t *thread, int wake_type)
{
    if (thread == 0)
    {
        // Shouldn't be possible, but lets not crash;
        return;
    }

    if (wake_type == WAKE_TYPE_THREADID && thread->state != THREAD_STATE_FINISHED)
    {
        // We want to wake up on this thread being joinable, but the thread isn't finished.
        return;
    }

    for (unsigned int i = 0; i < MAX_THREADS; i++)
    {
        if (threads[i] == 0)
        {
            // Not a real thread.
            continue;
        }

        if (wake_type == WAKE_TYPE_THREADID)
        {
            // If the thread in question is finished, see if its ID matches any
            // other threads waiting for it.
            if (threads[i]->waiting_thread == thread->id && threads[i]->state == THREAD_STATE_WAITING)
            {
                // Yup, the other thread was waiting on this one! Wake it up,
                // set it as not waiting for this thread, set its return value
                // for the thread_join() syscall to the thread's retval, and
                // set the current thread to a zombie since it's been waited on.
                threads[i]->waiting_thread = 0;
                threads[i]->state = THREAD_STATE_RUNNING;
                threads[i]->context->gp_regs[0] = (uint32_t )thread->retval;
                thread->state = THREAD_STATE_ZOMBIE;
            }
        }
    }
}

irq_state_t *_syscall_timer(irq_state_t *current, int timer)
{
    int schedule = THREAD_SCHEDULE_CURRENT;

    if (timer < 0)
    {
        // Periodic preemption timer.
        schedule = THREAD_SCHEDULE_ANY;
    }
    else if (timer == 0)
    {
        // Just a regular timer interrupt, we don't care about it.
    }
    else
    {
        // Timer ID passed in should match a requested callback
        // we made for waking up threads. We need to wake up any
        // threads that were sleeping and should now be awake.
        schedule = THREAD_SCHEDULE_OTHER;
    }

    return _thread_schedule(current, schedule);
}

irq_state_t *_syscall_trapa(irq_state_t *current, unsigned int which)
{
    int schedule = THREAD_SCHEDULE_CURRENT;

    switch (which)
    {
        case 0:
        {
            // global_counter_increment
            uint32_t *counter = _global_counter_find(current->gp_regs[4]);
            if (counter) { *counter = *counter + 1; }
            break;
        }
        case 1:
        {
            // global_counter_decrement
            uint32_t *counter = _global_counter_find(current->gp_regs[4]);
            if (counter && *counter > 0) { *counter = *counter - 1; }
            break;
        }
        case 2:
        {
            // global_counter_value
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
        case 3:
        {
            // thread_yield
            schedule = THREAD_SCHEDULE_OTHER;
            break;
        }
        case 4:
        {
            // thread_start
            thread_t *thread = _thread_find_by_id(current->gp_regs[4]);
            if (thread && thread->state == THREAD_STATE_STOPPED)
            {
                thread->state = THREAD_STATE_RUNNING;
            }

            schedule = THREAD_SCHEDULE_ANY;
            break;
        }
        case 5:
        {
            // thread_stop
            thread_t *thread = _thread_find_by_id(current->gp_regs[4]);
            if (thread && thread->state == THREAD_STATE_RUNNING)
            {
                thread->state = THREAD_STATE_STOPPED;
            }

            schedule = THREAD_SCHEDULE_ANY;
            break;
        }
        case 6:
        {
            // thread_priority
            thread_t *thread = _thread_find_by_id(current->gp_regs[4]);
            if (thread)
            {
                int priority = current->gp_regs[5];
                if (priority > MAX_PRIORITY)
                {
                    priority = MAX_PRIORITY;
                }
                if (priority < MIN_PRIORITY)
                {
                    priority = MIN_PRIORITY;
                }
                thread->priority = priority;
            }

            schedule = THREAD_SCHEDULE_ANY;
            break;
        }
        case 7:
        {
            // thread_id
            thread_t *thread = _thread_find_by_context(current);
            if (thread)
            {
                current->gp_regs[0] = thread->id;
            }
            else
            {
                _irq_display_exception(current, "cannot locate thread object", which);
            }
            break;
        }
        case 8:
        {
            // thread_join
            thread_t *myself = _thread_find_by_context(current);
            thread_t *other = _thread_find_by_id(current->gp_regs[4]);
            if (!myself)
            {
                // Literally should never happen.
                _irq_display_exception(current, "cannot locate thread object", which);
            }
            else
            {
                if (other)
                {
                    // Figure out if this thread is already done.
                    switch(other->state)
                    {
                        case THREAD_STATE_STOPPED:
                        case THREAD_STATE_RUNNING:
                        case THREAD_STATE_WAITING:
                        {
                            // Need to stick this thread into waiting until
                            // the other thread is finished.
                            myself->state = THREAD_STATE_WAITING;
                            myself->waiting_thread = other->id;
                            schedule = THREAD_SCHEDULE_OTHER;
                            break;
                        }
                        case THREAD_STATE_FINISHED:
                        {
                            // Thread is already done! We can return immediately.
                            current->gp_regs[0] = (uint32_t )other->retval;
                            other->state = THREAD_STATE_ZOMBIE;
                            break;
                        }
                        case THREAD_STATE_ZOMBIE:
                        {
                            // Thread was already waited on!
                            current->gp_regs[0] = 0;
                            break;
                        }
                    }
                }
                else
                {
                    // Thread doesn't exist, so return nothing from join.
                    current->gp_regs[0] = 0;
                }
            }
            break;
        }
        case 9:
        {
            // thread_exit
            thread_t *thread = _thread_find_by_context(current);
            if (thread)
            {
                thread->state = THREAD_STATE_FINISHED;
                thread->retval = (void *)current->gp_regs[4];
            }
            else
            {
                _irq_display_exception(current, "cannot locate thread object", which);
            }

            // Wake up any other threads that were waiting on this thread for a join.
            _thread_wake_others(thread, WAKE_TYPE_THREADID);
            schedule = THREAD_SCHEDULE_OTHER;
            break;
        }
        default:
        {
            _irq_display_exception(current, "unrecognized syscall", which);
            break;
        }
    }

    return _thread_schedule(current, schedule);
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
    register void *syscall_param0 asm("r4") = counter;
    asm("trapa #0" : : "r" (syscall_param0));
}

void global_counter_decrement(void *counter)
{
    register void *syscall_param0 asm("r4") = counter;
    asm("trapa #1" : : "r" (syscall_param0));
}

uint32_t global_counter_value(void *counter)
{
    register void *syscall_param0 asm("r4") = counter;
    register uint32_t syscall_return asm("r0");
    asm("trapa #2" : "=r" (syscall_return) : "r" (syscall_param0));
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

typedef struct
{
    void *param;
    thread_func_t function;
} thread_run_ctx_t;

void * _thread_run(void *param)
{
    // Grab all of our operating parameters from the param.
    thread_run_ctx_t *ctx = param;
    thread_func_t func = ctx->function;
    void * funcparam = ctx->param;

    // Free the context, we no longer need it.
    free(ctx);

    // Actually run the thread function, returning the result.
    thread_exit(func(funcparam));

    // We should never reach here if thread_exit() does it's job.
    return 0;
}

uint32_t thread_create(char *name, thread_func_t function, void *param)
{
    // Create a new thread.
    thread_t *thread = _thread_create(name, 0);

    // Create a thread run context so we can return from the thread.
    thread_run_ctx_t *ctx = malloc(sizeof(thread_run_ctx_t));
    ctx->function = function;
    ctx->param = param;

    // Set up the thread to be runnable.
    thread->stack = malloc(THREAD_STACK_SIZE);
    thread->context = _irq_new_state(_thread_run, ctx, thread->stack + THREAD_STACK_SIZE);

    // Return the thread ID.
    return thread->id;
}

void thread_destroy(uint32_t tid)
{
    uint32_t old_interrupts = irq_disable();

    for (unsigned int i = 0; i < MAX_THREADS; i++)
    {
        if (threads[i] != 0 && threads[i]->id == tid)
        {
            _thread_destroy(threads[i]);
            threads[i] = 0;
            break;
        }
    }

    irq_restore(old_interrupts);
}

void thread_start(uint32_t tid)
{
    register uint32_t syscall_param0 asm("r4") = tid;
    asm("trapa #4" : : "r" (syscall_param0));
}

void thread_stop(uint32_t tid)
{
    register uint32_t syscall_param0 asm("r4") = tid;
    asm("trapa #5" : : "r" (syscall_param0));
}

void thread_priority(uint32_t tid, int priority)
{
    register uint32_t syscall_param0 asm("r4") = tid;
    register int syscall_param1 asm("r5") = priority;
    asm("trapa #6" : : "r" (syscall_param0), "r" (syscall_param1));
}

thread_info_t thread_info(uint32_t tid)
{
    thread_info_t info;
    memset(&info, 0, sizeof(thread_info_t));

    uint32_t old_interrupts = irq_disable();
    thread_t *thread = _thread_find_by_id(tid);
    if (thread)
    {
        memcpy(info.name, thread->name, 64);
        info.priority = thread->priority;
        if (thread->state == THREAD_STATE_STOPPED || thread->state == THREAD_STATE_RUNNING || thread->state == THREAD_STATE_WAITING)
        {
            info.alive = 1;
        }
        else
        {
            info.alive = 0;
        }
        info.running = thread->state == THREAD_STATE_RUNNING ? 1 : 0;
    }

    irq_restore(old_interrupts);

    return info;
}

void thread_yield()
{
    asm("trapa #3");
}

uint32_t thread_id()
{
    register uint32_t syscall_return asm("r0");
    asm("trapa #7" : "=r" (syscall_return));
    return syscall_return;
}

void * thread_join(uint32_t tid)
{
    register uint32_t syscall_param0 asm("r4") = tid;
    register void * syscall_return asm("r0");
    asm("trapa #8" : "=r" (syscall_return) : "r" (syscall_param0));
    return syscall_return;
}

void thread_exit(void *retval)
{
    register void * syscall_param0 asm("r4") = retval;
    asm("trapa #9" : : "r" (syscall_param0));
}
