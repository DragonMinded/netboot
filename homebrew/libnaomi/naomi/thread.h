#ifndef __THREAD_H
#define __THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef void * (*thread_func_t)(void *param);

// Thread-safe global counters. Guaranteed to be atomically incremented/decremented
// as well as never go below zero. Also guaranteed to be safe to call increment/decrement/value
// on a counter that was freed in another thread. Global counters are dynamically-created
// only.
#define MAX_GLOBAL_COUNTERS 64

void *global_counter_init(uint32_t initial_value);
void global_counter_increment(void *counter);
void global_counter_decrement(void *counter);
uint32_t global_counter_value(void *counter);
void global_counter_free(void *counter);

// Semaphores, with all of the standard expectations for them. Calling aquire on a
// semaphore will block until the semaphore is available. Calling release on a semaphore
// signals that we no longer need the resource. Semaphores cooperate with the thread
// scheduler so blocking on an acquire will schedule other threads to run, as will releasing
// a semaphore schedule any blocked threads to run.
#define MAX_SEMAPHORES 64

typedef struct
{
    uint32_t id;
} semaphore_t;

void semaphore_init(semaphore_t *semaphore, uint32_t count);
void semaphore_acquire(semaphore_t *semaphore);
void semaphore_release(semaphore_t *semaphore);
void semaphore_free(semaphore_t *semaphore);

// Mutexes, with all of the standard expectations for them. Calling try_lock on a
// mutex will return nonzero if the lock is successfully held, false otherwise.
// Mutexes do not cooperate with the thread scheduler, a try_lock will simply get
// the lock or fail and no context switch will be attempted on failure.
#define MAX_MUTEXES 64

typedef struct
{
    uint32_t id;
} mutex_t;

void mutex_init(mutex_t *mutex);
int mutex_try_lock(mutex_t *mutex);
void mutex_lock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);
void mutex_free(mutex_t *mutex);

// Simple threads. We do not enable the MMU nor do we have any other process isolation.
// Threads share the same global memory space and heap, although malloc/free are thread
// safe.
#define MAX_THREADS 64
#define THREAD_STACK_SIZE (128 * 1024)

typedef struct
{
    char name[64];
    int priority;
    int alive;
    int running;
} thread_info_t;

// Create or destroy a thread object. Threads start in the stopped state and should only
// ever be destroyed from the thread that they were created in. Never destroy a thread from
// within the thread itself. If you wish to wait for a thread to complete and get its return
// value, call thread_join() in the thread that you created the thread in. This will block
// until the thread is done, and return the value that the thread function returned.
uint32_t thread_create(char *name, thread_func_t function, void *param);
void *thread_join(uint32_t tid);
void thread_destroy(uint32_t tid);

#define MAX_PRIORITY 1000
#define MIN_PRIORITY -1000

// Various thread manipulation functions. Get information about a thread, start and stop a
// thread, change priority on a thread, etc. All of these are safe to call from within any
// thread including the thread in questions.
thread_info_t thread_info(uint32_t tid);
void thread_priority(uint32_t tid, int priority);
void thread_start(uint32_t tid);
void thread_stop(uint32_t tid);

// Yield to the thread scheduler, which can choose a new thread to schedule.
void thread_yield();

// Exit a thread early, returning return value. Identical to letting control reach the end
// of the thread function with a return statement.
void thread_exit(void *retval);

// Return the current thread's ID.
uint32_t thread_id();

#ifdef __cplusplus
}
#endif

#endif
