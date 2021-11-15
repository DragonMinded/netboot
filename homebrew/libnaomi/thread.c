#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "naomi/interrupt.h"
#include "naomi/thread.h"
#include "naomi/timer.h"
#include "irqstate.h"

#define SEM_TYPE_MUTEX 1
#define SEM_TYPE_SEMAPHORE 2
#define MAX_SEM_AND_MUTEX (MAX_SEMAPHORES + MAX_MUTEXES)

typedef struct
{
    void *public;
    uint32_t id;
    unsigned int type;
    uint32_t max;
    uint32_t current;
    uint32_t irq_disabled;
} semaphore_internal_t;

static semaphore_internal_t *semaphores[MAX_SEM_AND_MUTEX];
static uint32_t semaphore_counter = MAX_SEM_AND_MUTEX;
static int global_semaphore_count = 0;
static int global_mutex_count = 0;

semaphore_internal_t *_semaphore_find(void * semaphore, unsigned int type)
{
    if (semaphore != 0)
    {
        unsigned int id = 0;
        if (type == SEM_TYPE_MUTEX)
        {
            id = ((mutex_t *)semaphore)->id;
        }
        else if (type == SEM_TYPE_SEMAPHORE)
        {
            id = ((semaphore_t *)semaphore)->id;
        }
        unsigned int slot = id % MAX_SEM_AND_MUTEX;

        if (semaphores[slot] != 0 && semaphores[slot]->id == id && semaphores[slot]->type == type)
        {
            return semaphores[slot];
        }
    }

    return 0;
}

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

    // State for priority bumping and real-time threads.
    int priority_bump;
    uint32_t running_time_inversion;
    unsigned int priority_reason;
    uint32_t inversion_timeout;

    // Thread statistics.
    uint64_t running_time;
    uint32_t running_time_recent;
    float cpu_percentage;

    // Any resources this thread is waiting on.
    semaphore_internal_t * waiting_semaphore;
    uint32_t waiting_thread;
    uint32_t waiting_timer;
    unsigned int waiting_interrupt;

    // The actual context of the thread, including all of the registers and such.
    irq_state_t *context;
    uint8_t *stack;
    void *retval;
} thread_t;

// Waiting interupt values.
#define WAITING_FOR_VBLANK_IN 1
#define WAITING_FOR_VBLANK_OUT 2
#define WAITING_FOR_HBLANK 3

// Priority for the idle thread. This is chosen to always be lower than the lowest
// priority possible to request, and never possible to bump past the minimum priority
// even with inversions.
#define IDLE_THREAD_PRIORITY -1000000

// Priority reason bitmask defines.
#define PRIORITY_WAKE_INVERSION 1
#define CRITICAL_WAKE_INVERSION 2

// Calculate stats every second.
#define STATS_DENOMINATOR 1000000

static uint32_t interruptions = 0;
static uint32_t last_second_interruptions = 0;
static uint64_t running_time_denominator = 0;
static uint64_t current_profile = 0;
static unsigned int highest_thread = 0;
static thread_t *threads[MAX_THREADS];

thread_t *_thread_find_by_id(uint32_t id)
{
    unsigned int slot = id % MAX_THREADS;
    if (threads[slot] != 0 && threads[slot]->id == id)
    {
        return threads[slot];
    }

    // Couldn't find it.
    return 0;
}

void _thread_check_waiting(thread_t *thread)
{
    // All conditions that should not be true if we want to put a thread into waiting.
    if (thread->state == THREAD_STATE_WAITING)
    {
        _irq_display_invariant("resource wait failure", "thread %lu is already waiting for another resource!", thread->id);
    }
    if (thread->waiting_semaphore != 0)
    {
        _irq_display_invariant("resource wait failure", "thread %lu is already waiting for a semaphore!", thread->id);
    }
    if (thread->waiting_thread != 0)
    {
        _irq_display_invariant("resource wait failure", "thread %lu is already waiting for another thread!", thread->id);
    }
    if (thread->waiting_timer != 0)
    {
        _irq_display_invariant("resource wait failure", "thread %lu is already waiting for a timer!", thread->id);
    }
    if (thread->waiting_interrupt != 0)
    {
        _irq_display_invariant("resource wait failure", "thread %lu is already waiting for a hardware interrupt!", thread->id);
    }
}

typedef struct
{
    uint32_t id;
    uint32_t current;
} global_counter_t;

static global_counter_t *global_counters[MAX_GLOBAL_COUNTERS];
static uint32_t global_counter_counter = MAX_GLOBAL_COUNTERS;

global_counter_t *_global_counter_find(uint32_t counterid)
{
    unsigned int slot = counterid % MAX_GLOBAL_COUNTERS;
    if (global_counters[slot] != 0 && global_counters[slot]->id == counterid)
    {
        return global_counters[slot];
    }
    else
    {
        return 0;
    }
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
        uint32_t thread_id = thread_counter + i;
        unsigned int slot = thread_id % MAX_THREADS;

        if (threads[slot] == 0)
        {
            thread = malloc(sizeof(thread_t));
            if (thread == 0)
            {
                _irq_display_invariant("memory failure", "could not get memory for new thread!");
            }
            memset(thread, 0, sizeof(thread_t));

            thread->id = thread_id;
            thread->priority = priority;
            thread->state = THREAD_STATE_STOPPED;
            strncpy(thread->name, name, 63);

            threads[slot] = thread;
            highest_thread = (slot + 1) > highest_thread ? (slot + 1) : highest_thread;
            thread_counter = thread_id + 1;
            break;
        }
    }

    irq_restore(old_interrupts);

    return thread;
}

void _thread_destroy(thread_t *thread)
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
    free(thread);
}

void _thread_register_main(irq_state_t *state)
{
    uint32_t old_interrupts = irq_disable();

    thread_t *main_thread = _thread_create("main", 0);
    main_thread->context = state;
    main_thread->state = THREAD_STATE_RUNNING;
    state->threadptr = main_thread;

    irq_restore(old_interrupts);
}

void _thread_create_idle()
{
    // Create an idle thread.
    thread_t *idle_thread = _thread_create("idle", IDLE_THREAD_PRIORITY);
    idle_thread->stack = malloc(64);
    if (idle_thread->stack == 0)
    {
        _irq_display_invariant("memory failure", "could not get memory for idle thread!");
    }
    idle_thread->context = _irq_new_state(_idle_thread, 0, idle_thread->stack + 64, idle_thread);
    idle_thread->state = THREAD_STATE_RUNNING;
}

uint32_t _thread_time_elapsed()
{
    if (current_profile != 0)
    {
        return _profile_get_current(0) - current_profile;
    }
    else
    {
        return 0;
    }
}

int _thread_enable_inversion(thread_t *thread, uint32_t reason, unsigned int amount)
{
    if (thread->priority_reason == 0)
    {
        thread->priority_bump = amount;
        thread->priority_reason = reason;
        thread->running_time_inversion = 0;
        thread->inversion_timeout = PRIORITY_INVERSION_TIME + _thread_time_elapsed();

        return 1;
    }

    return 0;
}

int _thread_disable_inversion(thread_t *thread, uint32_t reason, unsigned int amount)
{
    if (thread->priority_reason == reason)
    {
        thread->priority_bump = 0;
        thread->priority_reason = 0;
        thread->running_time_inversion = 0;
        return 1;
    }

    return 0;
}

// The amount of priority we add for an 'inversion' to give a task high-pri status.
// This is picked to bump any thread within the range of allowed priorities past
// the highest priority that's possible without inversion.
#define PRIORITY_INVERSION_AMOUNT 3000

// Similar rationale as above, but also ensuring that it takes priority over regular
// priority events as well.
#define CRITICAL_INVERSION_AMOUNT 13000

int _thread_enable_priority(thread_t *thread)
{
    return _thread_enable_inversion(thread, PRIORITY_WAKE_INVERSION, PRIORITY_INVERSION_AMOUNT);
}

int _thread_disable_priority(thread_t *thread)
{
    return _thread_disable_inversion(thread, PRIORITY_WAKE_INVERSION, PRIORITY_INVERSION_AMOUNT);
}

int _thread_enable_critical(thread_t *thread)
{
    if (thread->priority_reason == PRIORITY_WAKE_INVERSION)
    {
        // Disable the lower priority and bump higher.
        _thread_disable_priority(thread);
    }
    return _thread_enable_inversion(thread, CRITICAL_WAKE_INVERSION, PRIORITY_INVERSION_AMOUNT);
}

int _thread_disable_critical(thread_t *thread)
{
    return _thread_disable_inversion(thread, CRITICAL_WAKE_INVERSION, PRIORITY_INVERSION_AMOUNT);
}

int _thread_check_and_disable_timed_bumps(thread_t *thread)
{
    int retval = 0;

    if (thread->priority_reason == PRIORITY_WAKE_INVERSION && thread->running_time_inversion >= thread->inversion_timeout)
    {
        retval |= _thread_disable_priority(thread);
    }
    if (thread->priority_reason == CRITICAL_WAKE_INVERSION && thread->running_time_inversion >= thread->inversion_timeout)
    {
        retval |= _thread_disable_critical(thread);
    }

    return retval;
}

typedef struct
{
    uint32_t last_thread;
} last_thread_t;

static last_thread_t *round_robin = 0;
static unsigned int round_robin_count = 0;

last_thread_t *last_thread_for_band(int priority)
{
    if (priority < MIN_PRIORITY)
    {
        // Idle thread priority band.
        return &round_robin[0];
    }
    if (priority > MAX_PRIORITY)
    {
        // Bumped priority band.
        return &round_robin[1];
    }

    // Normal band.
    return &round_robin[(priority - MIN_PRIORITY) + 2];
}

// Prefer the current thread, unless it is not runnable.
#define THREAD_SCHEDULE_CURRENT 0

// Prefer another thread, and schedule ourselves if we have no other choice.
#define THREAD_SCHEDULE_OTHER 1

// Prefer any thread in our own priority band.
#define THREAD_SCHEDULE_ANY 2

irq_state_t *_thread_schedule(irq_state_t *state, int request)
{
    thread_t *current_thread = (thread_t *)state->threadptr;

    if (current_thread == 0)
    {
        // Should never happen.
        _irq_display_invariant("scheduling failure", "cannot locate current thread to schedule");
        return state;
    }

    // Schedule a new thread at this point.
    if (request == THREAD_SCHEDULE_CURRENT)
    {
        // See if the current thread is applicable to run. Never reschedule the
        // idle thread, however, unless there is truly nothing else to reschedule.
        if (current_thread->state == THREAD_STATE_RUNNING && current_thread->priority != IDLE_THREAD_PRIORITY)
        {
            // It is, just return it.
            return current_thread->context;
        }
    }

    // Set the max priority to the idle thread, so if we don't find any
    // applicable threads then we will choose the idle thread instead.
    int priority = IDLE_THREAD_PRIORITY;
    int self_priority = IDLE_THREAD_PRIORITY;

    // Go through and find the highest priority that is schedulable.
    for (unsigned int i = 0; i < highest_thread; i++)
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
            self_priority = self_priority > (threads[i]->priority + threads[i]->priority_bump) ? self_priority : (threads[i]->priority + threads[i]->priority_bump);
            continue;
        }

        // Bump the max priority based on this schedulable thread.
        priority = priority > (threads[i]->priority + threads[i]->priority_bump) ? priority : (threads[i]->priority + threads[i]->priority_bump);
    }

    if (priority == IDLE_THREAD_PRIORITY)
    {
        // We couldn't schedule any thread. However, if we requested to schedule another
        // thread aside from ourselves but we were the only one available, take that choice.
        priority = self_priority;
    }

    // Now, round robin within the priority band.
    last_thread_t *last_thread = last_thread_for_band(priority);
    if (last_thread->last_thread == 0 && priority == (current_thread->priority + current_thread->priority_bump))
    {
        // Short circuit to getting this set up.
        last_thread->last_thread = current_thread->id;
    }

    // Go through the loop the first time, seeing if we can locate the current thread, and
    // if so, choose the next thread after it. If this fails we will go through the loop
    // again and choose the very first applicable thread.
    int found = 0;
    thread_t *first_thread = 0;
    for (unsigned int i = 0; i < highest_thread; i++)
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

        if ((threads[i]->priority + threads[i]->priority_bump) != priority)
        {
            // Don't care, not the band we're after.
            continue;
        }

        if (first_thread == 0)
        {
            // Remember this just in case we don't find a thread.
            first_thread = threads[i];
        }

        if (found)
        {
            // Okay, we found our current thread last iteration, so this is
            // the next applicable thread in a round-robin scheduler.
            last_thread->last_thread = threads[i]->id;
            return threads[i]->context;
        }

        if (threads[i]->id == last_thread->last_thread)
        {
            // We found our thread, return the next thread on the next iteration.
            found = 1;
        }
    }

    // If we got here, then the next available thread is before our current thread.
    // We already saved the first applicable thread in the previous loop, so we can
    // use that here as long as it is not null. This has the chance of selecting
    // ourselves if there is no applicable other thread, even if the request is
    // THREAD_SCHEDULE_OTHER. That should only happen when it is the idle thread,
    // however, since at any other moment we would have chosen a different priority band.
    if (first_thread)
    {
        // Okay, we found an applicable thread, return it as the scheduled thread.
        last_thread->last_thread = first_thread->id;
        return first_thread->context;
    }

    // We should never ever get here, so display a failure message.
    _irq_display_invariant("scheduling failure", "cannot locate new thread to schedule");
    return state;
}

void _thread_init()
{
    uint32_t old_interrupts = irq_disable();

    thread_counter = MAX_THREADS;
    global_counter_counter = MAX_GLOBAL_COUNTERS;
    semaphore_counter = MAX_SEM_AND_MUTEX;
    global_semaphore_count = 0;
    global_mutex_count = 0;
    current_profile = 0;
    running_time_denominator = 0;
    interruptions = 0;
    last_second_interruptions = 0;
    highest_thread = 0;
    memset(global_counters, 0, sizeof(uint32_t *) * MAX_GLOBAL_COUNTERS);
    memset(semaphores, 0, sizeof(semaphore_internal_t *) * MAX_SEM_AND_MUTEX);
    memset(threads, 0, sizeof(thread_t *) * MAX_THREADS);

    // Set up per-priority round robin positions by adding the bumped
    // priority as well as the idle thread priority, and making sure
    // that there's enough for MIN_PRIORITY all the way up to MAX_PRIORITY
    // inclusive.
    round_robin = malloc(sizeof(last_thread_t) * ((MAX_PRIORITY - MIN_PRIORITY) + 3));
    if (round_robin == 0)
    {
        _irq_display_invariant("memory failure", "could not get memory for round robin scheduling!");
    }
    round_robin_count = (MAX_PRIORITY - MIN_PRIORITY) + 3;
    memset(round_robin, 0, sizeof(last_thread_t) * round_robin_count);

    irq_restore(old_interrupts);
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

    for (unsigned int i = 0; i < MAX_SEM_AND_MUTEX; i++)
    {
        if (semaphores[i] != 0)
        {
            if (semaphores[i]->type == SEM_TYPE_MUTEX)
            {
                ((mutex_t *)semaphores[i]->public)->id = 0;
            }
            else if (semaphores[i]->type == SEM_TYPE_SEMAPHORE)
            {
                ((semaphore_t *)semaphores[i]->public)->id = 0;
            }
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

    if (round_robin)
    {
        free(round_robin);
        round_robin = 0;
    }
    round_robin_count = 0;
    global_semaphore_count = 0;
    global_mutex_count = 0;

    irq_restore(old_interrupts);
}

void task_scheduler_info(task_scheduler_info_t *info)
{
    uint32_t old_interrupts = irq_disable();

    if (info)
    {
        info->cpu_percentage = 1.0;
        info->num_threads = 0;
        info->interruptions = last_second_interruptions;

        for (unsigned int i = 0; i < highest_thread; i++)
        {
            if (threads[i] != 0)
            {
                info->thread_ids[info->num_threads++] = threads[i]->id;
                info->cpu_percentage -= threads[i]->cpu_percentage;
            }
        }

        if (info->num_threads < MAX_THREADS)
        {
            memset(&info->thread_ids[info->num_threads], 0, sizeof(uint32_t) * (MAX_THREADS - info->num_threads));
        }
    }

    irq_restore(old_interrupts);
}

void _thread_wake_waiting_threadid(thread_t *thread)
{
    if (thread == 0)
    {
        // Shouldn't be possible, but lets not crash.
        _irq_display_invariant("wake failure", "target thread is NULL");
        return;
    }

    if (thread->state != THREAD_STATE_FINISHED)
    {
        // We want to wake up on this thread being joinable, but the thread isn't finished.
        // This also shouldn't be possible, but let's be careful.
        _irq_display_invariant("wake failure", "target thread is not finished");
        return;
    }

    for (unsigned int i = 0; i < highest_thread; i++)
    {
        if (threads[i] == 0)
        {
            // Not a real thread.
            continue;
        }

        if (threads[i]->state != THREAD_STATE_WAITING)
        {
            // Not waiting on any resources.
            continue;
        }

        // If the thread in question is finished, see if its ID matches any
        // other threads waiting for it.
        if (threads[i]->waiting_thread == thread->id)
        {
            // Yup, the other thread was waiting on this one! Wake it up,
            // set it as not waiting for this thread, set its return value
            // for the thread_join() syscall to the thread's retval, and
            // set the current thread to a zombie since it's been waited on.
            threads[i]->waiting_thread = 0;
            threads[i]->state = THREAD_STATE_RUNNING;
            if (thread->state == THREAD_STATE_ZOMBIE)
            {
                // Already outputted the result to another join.
                threads[i]->context->gp_regs[0] = 0;
            }
            else
            {
                threads[i]->context->gp_regs[0] = (uint32_t )thread->retval;
                thread->state = THREAD_STATE_ZOMBIE;
            }
        }
    }
}

int _thread_wake_waiting_semaphore(semaphore_internal_t *semaphore)
{
    if (semaphore == 0)
    {
        // Shouldn't be possible, but lets not crash.
        _irq_display_invariant("wake failure", "target semaphore is NULL");
        return 0;
    }

    int scheduled = 0;
    for (unsigned int i = 0; i < highest_thread; i++)
    {
        if (threads[i] == 0)
        {
            // Not a real thread.
            continue;
        }

        if (threads[i]->state != THREAD_STATE_WAITING)
        {
            // Not waiting on any resources.
            continue;
        }

        // See if the semaphore matches any other threads waiting on it.
        if (threads[i]->waiting_semaphore == semaphore)
        {
            // Yup, the other thread was waiting on this semaphore! Wake it up,
            // and set it as not waiting for this semaphore anymore.
            threads[i]->waiting_semaphore = 0;
            threads[i]->state = THREAD_STATE_RUNNING;
            scheduled = 1;

            // Now, since this was an acquire, we need to bookkeep the current
            // semaphore.
            semaphore->current--;
            if (semaphore->current == 0)
            {
                // No more slots to acquire, exit before we wake up any
                // additional threads.
                break;
            }
        }
    }

    return scheduled;
}

uint32_t _thread_wake_waiting_timer()
{
    // Calculate the time since we did our last adjustments.
    uint64_t new_profile = _profile_get_current(0);
    uint32_t time_elapsed = 0;
    if (current_profile != 0)
    {
        time_elapsed = new_profile - current_profile;
    }
    current_profile = new_profile;

    if (time_elapsed == 0)
    {
        // Nothing to do here!
        return 0;
    }

    for (unsigned int i = 0; i < highest_thread; i++)
    {
        if (threads[i] == 0)
        {
            // Not a real thread.
            continue;
        }

        if (threads[i]->state != THREAD_STATE_WAITING)
        {
            // Not waiting on any resources.
            continue;
        }

        if (threads[i]->waiting_timer == 0)
        {
            // Not waiting on a timer.
            continue;
        }

        if (threads[i]->waiting_timer <= time_elapsed)
        {
            // We hit our timeout, this thread is now wakeable!
            threads[i]->waiting_timer = 0;
            threads[i]->state = THREAD_STATE_RUNNING;
            _thread_enable_priority(threads[i]);
        }
        else
        {
            // This thread hasn't hit our timeout yet.
            threads[i]->waiting_timer -= time_elapsed;
        }
    }

    return time_elapsed;
}

int _thread_wake_waiting_irq(unsigned int which)
{
    if (which == 0)
    {
        // Nothing to do here!
        return 0;
    }

    int scheduled = 0;
    for (unsigned int i = 0; i < highest_thread; i++)
    {
        if (threads[i] == 0)
        {
            // Not a real thread.
            continue;
        }

        if (threads[i]->state != THREAD_STATE_WAITING)
        {
            // Not waiting on any resources.
            continue;
        }

        if (threads[i]->waiting_interrupt == which)
        {
            // This thread is waiting on the resource that just became available!
            threads[i]->waiting_interrupt = 0;
            threads[i]->state = THREAD_STATE_RUNNING;
            _thread_enable_critical(threads[i]);
            scheduled = 1;
        }
    }

    return scheduled;
}

void _thread_calc_stats(irq_state_t *current, uint32_t elapsed, uint32_t overhead)
{
    if (elapsed == 0)
    {
        // Nothing to calculate?
        return;
    }

    if (!current)
    {
        // Should never happen.
        _irq_display_invariant("stats failure", "current irq state is NULL");
    }

    thread_t *current_thread = (thread_t *)current->threadptr;

    if (current_thread == 0)
    {
        // Should never happen.
        _irq_display_invariant("stats failure", "cannot locate current thread");
    }

    // First, make sure we know the total running time.
    running_time_denominator += elapsed;

    // Now, go through and find all running threads and see if its time to calculate
    // percentages.
    for (unsigned int i = 0; i < highest_thread; i++)
    {
        if (threads[i] == 0)
        {
            // Not a real thread.
            continue;
        }

        if (threads[i] == current_thread && threads[i]->state == THREAD_STATE_RUNNING)
        {
            // We spent the last elapsed us on this thread.
            threads[i]->running_time += elapsed;
            threads[i]->running_time_recent += (elapsed - overhead);

            // Also keep track of how long this thread has been at priority.
            if (threads[i]->priority_reason > 0)
            {
                threads[i]->running_time_inversion += (elapsed - overhead);
                _thread_check_and_disable_timed_bumps(threads[i]);
            }
        }

        if (running_time_denominator >= STATS_DENOMINATOR)
        {
            // Calculate CPU percentage based on recent running time.
            threads[i]->cpu_percentage = (float)threads[i]->running_time_recent / (float)running_time_denominator;
            threads[i]->running_time_recent = 0;
        }
    }

    if (running_time_denominator >= STATS_DENOMINATOR)
    {
        last_second_interruptions = interruptions;
        interruptions = 0;

        running_time_denominator = 0;
    }
}

irq_state_t *_syscall_timer(irq_state_t *current, int timer)
{
    if (timer < 0)
    {
        // Periodic preemption timer.
        uint64_t start = _profile_get_current(0);
        uint32_t elapsed = _thread_wake_waiting_timer();
        interruptions ++;

        // Calculate stats, less the overhead from the above section of code.
        uint64_t calc = _profile_get_current(0);
        _thread_calc_stats(current, elapsed, calc - start);
        current = _thread_schedule(current, THREAD_SCHEDULE_ANY);

        // Make sure to adjust the denominator by the overhead after calculating stats.
        uint64_t done = _profile_get_current(0);
        running_time_denominator += done - calc;
    }

    return current;
}

irq_state_t *_syscall_holly(irq_state_t *current, uint32_t serviced_holly_interrupts)
{
    uint64_t start = _profile_get_current(0);
    int should_schedule = 0;

    if (serviced_holly_interrupts & HOLLY_SERVICED_VBLANK_IN)
    {
        // Wake any threads waiting for vblank in interrupt.
        should_schedule = should_schedule | _thread_wake_waiting_irq(WAITING_FOR_VBLANK_IN);
    }
    if (serviced_holly_interrupts & HOLLY_SERVICED_VBLANK_OUT)
    {
        // Wake any threads waiting for vblank out interrupt.
        should_schedule = should_schedule | _thread_wake_waiting_irq(WAITING_FOR_VBLANK_OUT);
    }
    if (serviced_holly_interrupts & HOLLY_SERVICED_HBLANK)
    {
        // Wake any threads waiting for hblank interrupt.
        should_schedule = should_schedule | _thread_wake_waiting_irq(WAITING_FOR_HBLANK);
    }

    if (should_schedule)
    {
        // Wake any threads, find out how long we've been running the current thread.
        uint32_t elapsed = _thread_wake_waiting_timer();
        interruptions ++;

        // Calculate stats, less the overhead from the above section of code.
        uint64_t calc = _profile_get_current(0);
        _thread_calc_stats(current, elapsed, calc - start);
        current = _thread_schedule(current, THREAD_SCHEDULE_ANY);

        // Make sure to adjust the denominator by the overhead after calculating stats.
        uint64_t done = _profile_get_current(0);
        running_time_denominator += done - calc;
    }

    return current;
}

irq_state_t *_syscall_trapa(irq_state_t *current, unsigned int which)
{
    uint64_t start = _profile_get_current(0);
    int schedule = THREAD_SCHEDULE_CURRENT;

    switch (which)
    {
        case 0:
        {
            // global_counter_increment
            global_counter_t *counter = _global_counter_find(current->gp_regs[4]);
            if (counter) { counter->current = counter->current + 1; }
            break;
        }
        case 1:
        {
            // global_counter_decrement
            global_counter_t *counter = _global_counter_find(current->gp_regs[4]);
            if (counter && counter->current > 0) { counter->current = counter->current - 1; }
            break;
        }
        case 2:
        {
            // global_counter_value
            global_counter_t *counter = _global_counter_find(current->gp_regs[4]);
            if (counter)
            {
                current->gp_regs[0] = counter->current;
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
            thread_t *thread = (thread_t *)current->threadptr;
            if (thread)
            {
                // Give up some of our inversion status because we chose to yield.
                int was_priority = _thread_disable_priority(thread);
                was_priority |= _thread_disable_critical(thread);

                if (!was_priority)
                {
                    // Schedule another thread other than ourselves, unless we have no other choice.
                    schedule = THREAD_SCHEDULE_OTHER;
                }
            }
            else
            {
                _irq_display_exception(current, "cannot locate thread object", which);
            }
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
            thread_t *thread = (thread_t *)current->threadptr;
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
            thread_t *myself = (thread_t *)current->threadptr;
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
                            _thread_check_waiting(myself);
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
            thread_t *thread = (thread_t *)current->threadptr;
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
            _thread_wake_waiting_threadid(thread);
            schedule = THREAD_SCHEDULE_OTHER;
            break;
        }
        case 10:
        {
            // semaphore_acquire, mutex_lock
            semaphore_t *handle = (semaphore_t *)current->gp_regs[4];
            semaphore_internal_t *semaphore = _semaphore_find(handle, current->gp_regs[5]);
            if (semaphore)
            {
                if (semaphore->current > 0)
                {
                    // Safely can acquire this.
                    semaphore->current -= 1;
                    semaphore->irq_disabled = 0;
                }
                else
                {
                    thread_t *thread = (thread_t *)current->threadptr;

                    if (thread)
                    {
                        // Semaphore is used up, park ourselves until its ready.
                        _thread_check_waiting(thread);
                        thread->state = THREAD_STATE_WAITING;
                        thread->waiting_semaphore = semaphore;
                        schedule = THREAD_SCHEDULE_OTHER;
                    }
                    else
                    {
                        // Should never happen.
                        _irq_display_exception(current, "cannot locate thread object", which);
                    }
                }
            }
            else
            {
                // This semaphore is dead, so we have no choice but to fail.
                uint32_t id = handle ? handle->id : 0;
                char *msg = current->gp_regs[5] == SEM_TYPE_SEMAPHORE ?
                    "attempt acquire uninitialized semaphore" :
                    "attempt acquire uninitialized mutex";
                _irq_display_exception(current, msg, id);
            }

            break;
        }
        case 11:
        {
            // semaphore_release, mutex_unlock
            semaphore_t *handle = (semaphore_t *)current->gp_regs[4];
            semaphore_internal_t *semaphore = _semaphore_find(handle, current->gp_regs[5]);
            if (semaphore)
            {
                // Safely restore this.
                semaphore->current += 1;

                if (semaphore->current > semaphore->max)
                {
                    uint32_t id = handle ? handle->id : 0;
                    char *msg = current->gp_regs[5] == SEM_TYPE_SEMAPHORE ?
                        "attempt release unowned semaphore" :
                        "attempt release unowned mutex";
                    _irq_display_exception(current, msg, id);
                }

                // Wake up any other threads that were waiting on this thread for a join.
                if (_thread_wake_waiting_semaphore(semaphore))
                {
                    schedule = THREAD_SCHEDULE_OTHER;
                }
            }
            else
            {
                // This semaphore is dead, so we have no choice but to fail.
                uint32_t id = handle ? handle->id : 0;
                char *msg = current->gp_regs[5] == SEM_TYPE_SEMAPHORE ?
                    "attempt release uninitialized semaphore" :
                    "attempt release uninitialized mutex";
                _irq_display_exception(current, msg, id);
            }

            break;
        }
        case 12:
        {
            // thread_sleep.
            thread_t *thread = (thread_t *)current->threadptr;
            if (thread)
            {
                // Put the thread to sleep, waiting for the number of us requested.
                // Adjust that number based on how close to the periodic interrupt
                // we are, since when it fires it will not necessarily have lasted
                // the right amount of time for this particular timer.
                _thread_check_waiting(thread);
                thread->state = THREAD_STATE_WAITING;
                thread->waiting_timer = current->gp_regs[4] + _thread_time_elapsed();
                schedule = THREAD_SCHEDULE_OTHER;
            }
            else
            {
                // Should never happen.
                _irq_display_exception(current, "cannot locate thread object", which);
            }
            break;
        }
        case 13:
        {
            // thread_wait_vblank_in, thread_wait_vblank_out, thread_wait_hblank.
            thread_t *thread = (thread_t *)current->threadptr;
            if (thread)
            {
                // Put the thread to sleep, waiting for a specific interrupt.
                _thread_check_waiting(thread);
                thread->state = THREAD_STATE_WAITING;
                thread->waiting_interrupt = current->gp_regs[4];
                schedule = THREAD_SCHEDULE_OTHER;
            }
            else
            {
                // Should never happen.
                _irq_display_exception(current, "cannot locate thread object", which);
            }
            break;
        }
        default:
        {
            _irq_display_exception(current, "unrecognized syscall", which);
            break;
        }
    }

    // Figure out how long the current thread was on the CPU for.
    uint32_t elapsed = _thread_wake_waiting_timer();
    interruptions ++;

    // Add the current running time to the current process to keep track of stats.
    uint64_t calc = _profile_get_current(0);
    _thread_calc_stats(current, elapsed, calc - start);
    current = _thread_schedule(current, schedule);

    // Make sure to adjust the denominator by the overhead after calculating stats.
    uint64_t done = _profile_get_current(0);
    running_time_denominator += done - calc;
    return current;
}

void *global_counter_init(uint32_t initial_value)
{
    uint32_t old_interrupts = irq_disable();
    void *retval = 0;

    for (unsigned int i = 0; i < MAX_GLOBAL_COUNTERS; i++)
    {
        uint32_t counterid = global_counter_counter + i;
        unsigned int slot = counterid % MAX_GLOBAL_COUNTERS;

        if (global_counters[slot] == 0)
        {
            // Create counter.
            global_counter_t *counter = malloc(sizeof(global_counter_t));
            if (counter == 0)
            {
                _irq_display_invariant("memory failure", "could not get memory for new counter!");
            }

            // Set up the ID and initial value.
            counter->id = counterid;
            counter->current = initial_value;
            global_counter_counter = counterid + 1;

            // Put it in our registry.
            global_counters[slot] = counter;

            // Return it.
            retval = (void *)counter->id;
            break;
        }
    }

    irq_restore(old_interrupts);
    return retval;
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

    uint32_t id = (uint32_t)counter;
    unsigned int slot = id % MAX_GLOBAL_COUNTERS;
    if (global_counters[slot] != 0 && global_counters[slot]->id == id)
    {
        free(global_counters[slot]);
        global_counters[slot] = 0;
    }

    irq_restore(old_interrupts);
}

void semaphore_init(semaphore_t *semaphore, uint32_t initial_value)
{
    uint32_t old_interrupts = irq_disable();

    if (semaphore)
    {
        // Enforce maximum, since we combine semaphores and mutexes.
        if (global_semaphore_count >= MAX_SEMAPHORES)
        {
            return;
        }

        for (unsigned int i = 0; i < MAX_SEM_AND_MUTEX; i++)
        {
            uint32_t semaphore_id = semaphore_counter + i;
            unsigned int slot = semaphore_id % MAX_SEM_AND_MUTEX;

            if (semaphores[slot] == 0)
            {
                // Create semaphore.
                semaphore_internal_t *internal = malloc(sizeof(semaphore_internal_t));
                if (internal == 0)
                {
                    _irq_display_invariant("memory failure", "could not get memory for new semaphore!");
                }

                // Set up the pointer and initial value.
                internal->public = semaphore;
                internal->id = semaphore_id;
                internal->type = SEM_TYPE_SEMAPHORE;
                internal->max = initial_value;
                internal->current = initial_value;

                // Put it in our registry.
                semaphores[slot] = internal;

                // Assign an ID to this semaphore so we can look it up again later.
                semaphore->id = semaphore_id;
                semaphore_counter = semaphore_id + 1;
                global_semaphore_count ++;
                break;
            }
        }
    }

    irq_restore(old_interrupts);
}

void semaphore_acquire(semaphore_t * semaphore)
{
    register semaphore_t * syscall_param0 asm("r4") = semaphore;
    register unsigned int syscall_param1 asm("r5") = SEM_TYPE_SEMAPHORE;
    asm("trapa #10" : : "r" (syscall_param0), "r" (syscall_param1));
}

void semaphore_release(semaphore_t * semaphore)
{
    register semaphore_t * syscall_param0 asm("r4") = semaphore;
    register unsigned int syscall_param1 asm("r5") = SEM_TYPE_SEMAPHORE;
    asm("trapa #11" : : "r" (syscall_param0), "r" (syscall_param1));
}

void semaphore_free(semaphore_t *semaphore)
{
    uint32_t old_interrupts = irq_disable();

    if (semaphore)
    {
        int slot = semaphore->id % MAX_SEM_AND_MUTEX;
        if (semaphores[slot] != 0 && semaphores[slot]->id == semaphore->id && semaphores[slot]->type == SEM_TYPE_SEMAPHORE)
        {
            free(semaphores[slot]);
            semaphores[slot] = 0;
            semaphore->id = 0;
            global_semaphore_count --;
        }
    }

    irq_restore(old_interrupts);
}

void mutex_init(mutex_t *mutex)
{
    uint32_t old_interrupts = irq_disable();

    if (mutex)
    {
        // Enforce maximum, since we combine semaphores and mutexes.
        if (global_mutex_count >= MAX_MUTEXES)
        {
            return;
        }

        for (unsigned int i = 0; i < MAX_SEM_AND_MUTEX; i++)
        {
            uint32_t mutex_id = semaphore_counter + i;
            unsigned int slot = mutex_id % MAX_SEM_AND_MUTEX;

            if (semaphores[slot] == 0)
            {
                // Create mutex.
                semaphore_internal_t *internal = malloc(sizeof(semaphore_internal_t));
                if (internal == 0)
                {
                    _irq_display_invariant("memory failure", "could not get memory for new mutex!");
                }

                // Set up the pointer and initial value.
                internal->public = mutex;
                internal->id = mutex_id;
                internal->type = SEM_TYPE_MUTEX;
                internal->max = 1;
                internal->current = 1;

                // Put it in our registry.
                semaphores[slot] = internal;

                // Assign an ID to this semaphore so we can look it up again later.
                mutex->id = mutex_id;
                semaphore_counter = mutex_id + 1;
                global_mutex_count ++;
                break;
            }
        }
    }

    irq_restore(old_interrupts);
}

int mutex_try_lock(mutex_t *mutex)
{
    // This doesn't use a syscall since we don't want to context switch as this function
    // supports being called with interrupts disabled, such as in HW drivers.
    uint32_t old_interrupts = irq_disable();
    int acquired = 0;

    if (mutex)
    {
        int slot = mutex->id % MAX_SEM_AND_MUTEX;
        if (semaphores[slot] != 0 && semaphores[slot]->id == mutex->id && semaphores[slot]->type == SEM_TYPE_MUTEX)
        {
            // This is the right mutex. See if we can acquire it.
            if (semaphores[slot]->current > 0)
            {
                acquired = 1;
                semaphores[slot]->current --;

                // Keep track of whether this was acquired with interrupts disabled or not.
                // This is because if it was, the subsequent unlock must be done without
                // syscalls as well.
                semaphores[slot]->irq_disabled = _irq_was_disabled(old_interrupts);
            }
        }
    }

    irq_restore(old_interrupts);
    return acquired;
}

void mutex_lock(mutex_t * mutex)
{
    register mutex_t * syscall_param0 asm("r4") = mutex;
    register unsigned int syscall_param1 asm("r5") = SEM_TYPE_MUTEX;
    asm("trapa #10" : : "r" (syscall_param0), "r" (syscall_param1));
}

void mutex_unlock(mutex_t * mutex)
{
    // If we locked a mutex without threads/interrupts enabled, we need to similarly
    // unlock it without a syscall. That's safe to do so, since no other thread could
    // have gotten to the mutex as threads were disabled.
    uint32_t old_interrupts = irq_disable();

    if (mutex)
    {
        int slot = mutex->id % MAX_SEM_AND_MUTEX;
        if (semaphores[slot] != 0 && semaphores[slot]->id == mutex->id && semaphores[slot]->type == SEM_TYPE_MUTEX && semaphores[slot]->irq_disabled)
        {
            // Unlock the mutex, exit without doing a syscall.
            semaphores[slot]->current ++;
            semaphores[slot]->irq_disabled = 0;

            irq_restore(old_interrupts);
            return;
        }
    }

    // This was locked normally, unlock using a syscall to wake any other threads.
    irq_restore(old_interrupts);
    register mutex_t * syscall_param0 asm("r4") = mutex;
    register unsigned int syscall_param1 asm("r5") = SEM_TYPE_MUTEX;
    asm("trapa #11" : : "r" (syscall_param0), "r" (syscall_param1));
}

void mutex_free(mutex_t *mutex)
{
    uint32_t old_interrupts = irq_disable();

    if (mutex)
    {
        int slot = mutex->id % MAX_SEM_AND_MUTEX;
        if (semaphores[slot] != 0 && semaphores[slot]->id == mutex->id && semaphores[slot]->type == SEM_TYPE_MUTEX)
        {
            free(semaphores[slot]);
            semaphores[slot] = 0;
            mutex->id = 0;
            global_mutex_count --;
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
    _irq_display_invariant("run failure", "thread exit syscall failed");
    return 0;
}

uint32_t thread_create(char *name, thread_func_t function, void *param)
{
    // Create a new thread.
    thread_t *thread = _thread_create(name, 0);

    // Create a thread run context so we can return from the thread.
    thread_run_ctx_t *ctx = malloc(sizeof(thread_run_ctx_t));
    if (ctx == 0)
    {
        _irq_display_invariant("memory failure", "could not get memory for new thread context!");
    }
    ctx->function = function;
    ctx->param = param;

    // Set up the thread to be runnable.
    thread->stack = malloc(THREAD_STACK_SIZE);
    if (thread->stack == 0)
    {
        _irq_display_invariant("memory failure", "could not get memory for new thread stack!");
    }
    thread->context = _irq_new_state(_thread_run, ctx, thread->stack + THREAD_STACK_SIZE, thread);

    // Return the thread ID.
    return thread->id;
}

void thread_destroy(uint32_t tid)
{
    uint32_t old_interrupts = irq_disable();

    unsigned int slot = tid % MAX_THREADS;
    if (threads[slot] != 0 && threads[slot]->id == tid)
    {
        _thread_destroy(threads[slot]);
        threads[slot] = 0;
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

void thread_info(uint32_t tid, thread_info_t *info)
{
    uint32_t old_interrupts = irq_disable();

    thread_t *thread = _thread_find_by_id(tid);
    if (thread && info)
    {
        // Basic stats
        memcpy(info->name, thread->name, 64);
        info->priority = thread->priority;

        // Calculate whether it is alive or not.
        if (thread->state == THREAD_STATE_STOPPED || thread->state == THREAD_STATE_RUNNING || thread->state == THREAD_STATE_WAITING)
        {
            info->alive = 1;
        }
        else
        {
            info->alive = 0;
        }
        info->running = thread->state == THREAD_STATE_RUNNING ? 1 : 0;

        // CPU stats.
        info->running_time = thread->running_time;
        info->cpu_percentage = thread->cpu_percentage;
    }

    irq_restore(old_interrupts);
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

void thread_sleep(uint32_t us)
{
    register uint32_t syscall_param0 asm("r4") = us;
    asm("trapa #12" : : "r" (syscall_param0));
}

void thread_wait_vblank_in()
{
    register uint32_t syscall_param0 asm("r4") = WAITING_FOR_VBLANK_IN;
    asm("trapa #13" : : "r" (syscall_param0));
}

void thread_wait_vblank_out()
{
    register uint32_t syscall_param0 asm("r4") = WAITING_FOR_VBLANK_OUT;
    asm("trapa #13" : : "r" (syscall_param0));
}

void thread_wait_hblank()
{
    register uint32_t syscall_param0 asm("r4") = WAITING_FOR_HBLANK;
    asm("trapa #13" : : "r" (syscall_param0));
}
