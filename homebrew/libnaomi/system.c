#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include "naomi/system.h"
#include "naomi/interrupt.h"
#include "naomi/thread.h"
#include "naomi/video.h"
#include "naomi/audio.h"
#include "irqinternal.h"
#include "irqstate.h"

#define QACR0 (*(uint32_t *)0xFF000038)
#define QACR1 (*(uint32_t *)0xFF00003C)

#define SYSCALL_VECTOR_BASE ((uint32_t *)0xAC018000)
#define SYSCALL_CALCULATE_EEPROM_SETTINGS 0x4
#define SYSCALL_READ_AND_PERFORM_DIMM_COMMAND 0x9
#define SYSCALL_ENTER_TEST_MODE 0x11
#define SYSCALL_POLL_HAS_DIMM_COMMAND 0x14

extern int main();
extern int test();

/* Global constructors/destructors */
typedef void (*func_ptr)(void);
extern uint32_t __ctors;
extern uint32_t __ctors_end;
extern uint32_t __dtors;
extern uint32_t __dtors_end;

/* libgcc floating point stuff */
extern void __set_fpscr (unsigned long);

/* Global hardware access mutexes. */
static mutex_t queue_mutex;

/* Provide a weakref to a default test sub for autoconf-purposes. */
int __test()
{
    return 0;
}

int test() __attribute__((weak, alias ("__test")));

// Prototype for passing the signal on to GDB if it connects.
void _gdb_set_haltreason(int reason);

// Prototype for polling DIMM commands so we can keep the communication
// channel open even in a halted state. This is so GDB can connect and
// debug us properly.
int _dimm_command_handler(int halted, irq_state_t *cur_state);

void _exit(int status)
{
    // TODO: Make sure if a user connects with GDB, the backtrace points at
    // our function call here.
    irq_state_t state;
    memset(&state, 0, sizeof(state));
    state.pc = (uint32_t)&_exit;

    // We don't have an OS to "go back to", so just infinite-loop.
    while( 1 )
    {
        int halted = _dimm_command_handler(halted, &state);
        if (halted == 0)
        {
            // User continued, not valid, so re-raise the exception.
            _gdb_set_haltreason(SIGTERM);
        }
    }
}

// Prototypes of functions that we don't want available in the public headers
void _irq_init();
void _irq_free();
void _maple_init();
void _maple_free();
void _timer_init();
void _timer_free();
void _thread_init();
void _thread_free();
void _romfs_init();
void _romfs_free();
void _posix_init();
void _posix_free();

void _startup()
{
    // Initialize things we promise are fully ready by the time main/test is called.
    _timer_init();
    _thread_init();
    _maple_init();
    _irq_init();
    _posix_init();
    _romfs_init();

    // Initialize mutexes for hardware that needs exclusive access.
    mutex_init(&queue_mutex);

    // Run init sections.
    uint32_t *ctor_ptr = &__ctors;
    while (ctor_ptr < &__ctors_end)
    {
        (*((func_ptr *)ctor_ptr))();
        ctor_ptr++;
    }
}

void _shutdown()
{
    // Free anything that was possibly initialized by the user.
    audio_free();
    video_free();

    // Free those things now that we're done. We should usually never get here
    // because it would be unusual to exit from main/test by returning.
    _romfs_free();
    _posix_free();
    _irq_free();
    _maple_free();
    _thread_free();
    _timer_free();

    // Run fini sections.
    uint32_t *dtor_ptr = &__dtors;
    while (dtor_ptr < &__dtors_end)
    {
        (*((func_ptr *)dtor_ptr))();
        dtor_ptr++;
    }
}

void _enter()
{
    // We set this to 1 or 0 depending on whether we are in test or normal
    // mode. Save this value locally since the register could change in
    // global constructors below.
    register uint32_t boot_mode asm("r3");
    uint32_t _boot_mode = boot_mode;

    // Set up system DMA to allow for things like Maple to operate. This
    // was kindly copied from the Mvc2 init code after bisecting to it
    // when determining how to initialize Maple.
    ((uint32_t *)0xFFA00020)[0] = 0;
    ((uint32_t *)0xFFA0002C)[0] = 0x1201;
    ((uint32_t *)0xFFA00040)[0] = 0x8201;
    while(((volatile uint32_t *)0xFFA00040)[0] != 0x8201)
    {
        // Spinloop!
        for(int i = 0; i < 0x10000; i++) { ; }
        ((uint32_t *)0xFFA00040)[0] = 0x8201;
    }

    // Set up floating point stuff. Requests round to nearest instead of round to zero,
    // denormalized numbers treated as zero.
    __set_fpscr(0x40000);

    // Start up the system kernel.
    _startup();

    // Execute main/test executable based on boot variable set in
    // sh-crt0.s which comes from the entrypoint used to start the code.
    int status;
    if(_boot_mode == 0)
    {
        status = main();
    }
    else
    {
        status = test();
    }

    // Shut everything down in reverse order.
    _shutdown();

    // Finally, exit from the program.
    _exit(status);
}

int hw_memset(void *addr, uint32_t value, unsigned int amount)
{
    // Very similar to a standard memset, but the address pointer must be aligned
    // to a 32 byte boundary, the amount must be a multiple of 32 bytes and the
    // value must be 32 bits. When used correctly this is about 3x faster than
    // a software memset.

    if ((((uint32_t)addr) & 0x1F) != 0)
    {
        _irq_display_invariant("invalid hw_memset location", "addr %08lx is not aligned to 32-byte boundary", (uint32_t)addr);
    }
    if ((amount & 0x1F) != 0)
    {
        _irq_display_invariant("invalid hw_memset amount", "amount %lu is not multiple of 32 bytes", amount);
    }

    int successful = 0;
    if (mutex_try_lock(&queue_mutex))
    {
        // Set the base queue address pointer to the queue location with address bits
        // 25:5. The bottom bits should be all 0s since hw_memset requires an alignment
        // to a 32 byte boundary. We will use both queue areas since SQ0/SQ1 specification
        // is the same bit as address bit 5. Technically this means the below queue setup
        // interleaves the data between the two queues, but it really does not matter what
        // order the hardware copies things.
        uint32_t *queue = (uint32_t *)(STORE_QUEUE_BASE | (((uint32_t)addr) & 0x03FFFFE0));
        uint32_t actual_copy_addr = (uint32_t)addr;
        uint32_t stop_copy_addr = actual_copy_addr + (amount & 0xFFFFFFE0);
        uint32_t stored_addr_bits = (actual_copy_addr >> 24) & 0x1C;

        // Set the top address bits (28:26) into the store queue address control registers.
        QACR0 = stored_addr_bits;
        QACR1 = stored_addr_bits;

        // Now, set up both store queues to contain the same value that we want to memset.
        // This is 8 32-bit values per store queue.
        queue[0] = value;
        queue[1] = value;
        queue[2] = value;
        queue[3] = value;
        queue[4] = value;
        queue[5] = value;
        queue[6] = value;
        queue[7] = value;
        queue[8] = value;
        queue[9] = value;
        queue[10] = value;
        queue[11] = value;
        queue[12] = value;
        queue[13] = value;
        queue[14] = value;
        queue[15] = value;

        // Now, trigger the hardware to copy the values from the queue to the address we
        // care about, triggering one 32-byte prefetch at a time.
        while (actual_copy_addr != stop_copy_addr)
        {
            // Make sure we don't wrap around our top address bits.
            if (((actual_copy_addr >> 24) & 0x1C) != stored_addr_bits)
            {
                // Re-init the top address control registers and the queue.
                stored_addr_bits = (actual_copy_addr >> 24) & 0x1C;
                QACR0 = stored_addr_bits;
                QACR1 = stored_addr_bits;

                // Now, set up both store queues to contain the same value that we want to memset.
                // This is 8 32-bit values per store queue.
                queue[0] = value;
                queue[1] = value;
                queue[2] = value;
                queue[3] = value;
                queue[4] = value;
                queue[5] = value;
                queue[6] = value;
                queue[7] = value;
                queue[8] = value;
                queue[9] = value;
                queue[10] = value;
                queue[11] = value;
                queue[12] = value;
                queue[13] = value;
                queue[14] = value;
                queue[15] = value;
            }

            // Perform the actual memset.
            __asm__("pref @%0" : : "r"(queue));
            queue += 8;
            actual_copy_addr += 32;
        }

        // Finally, attempt a new write to both queues in order to stall the CPU until the
        // last write is done.
        queue = (uint32_t *)STORE_QUEUE_BASE;
        queue[0] = 0;
        queue[8] = 0;

        // We held the lock and succeeded at memsetting.
        successful = 1;
        mutex_unlock(&queue_mutex);
    }

    return successful;
}

int hw_memcpy(void *dest, void *src, unsigned int amount)
{
    // Very similar to a standard memcpy, but the destination pointer must be aligned
    // to a 32 byte boundary, the amount must be a multiple of 32 bytes and the source
    // pointer must be aligned to a 4 byte boundary.

    if ((((uint32_t)dest) & 0x1F) != 0)
    {
        _irq_display_invariant("invalid hw_memcpy location", "dest %08lx is not aligned to 32-byte boundary", (uint32_t)dest);
    }
    if ((((uint32_t)src) & 0x3) != 0)
    {
        _irq_display_invariant("invalid hw_memcpy location", "src %08lx is not aligned to 32-byte boundary", (uint32_t)src);
    }
    if ((amount & 0x1F) != 0)
    {
        _irq_display_invariant("invalid hw_memcpy amount", "amount %lu is not multiple of 32 bytes", amount);
    }

    int successful = 0;
    if (mutex_try_lock(&queue_mutex))
    {

        // Set the base queue address pointer to the queue location with destination bits
        // 25:5. The bottom bits should be all 0s since hw_memset requires an alignment
        // to a 32 byte boundary. We will use both queue areas since SQ0/SQ1 specification
        // is the same bit as destination bit 5. Technically this means the below queue setup
        // interleaves the data between the two queues, but it really does not matter what
        // order the hardware copies things.
        uint32_t *srcptr = (uint32_t *)src;
        uint32_t *queue = (uint32_t *)(STORE_QUEUE_BASE | (((uint32_t)dest) & 0x03FFFFE0));
        uint32_t actual_copy_dest = (uint32_t)dest;
        uint32_t stop_copy_dest = actual_copy_dest + (amount & 0xFFFFFFE0);
        uint32_t stored_dest_bits = (actual_copy_dest >> 24) & 0x1C;

        // Set the top address bits (28:26) into the store queue address control registers.
        QACR0 = stored_dest_bits;
        QACR1 = stored_dest_bits;

        // Now, trigger the hardware to copy the values from the queue to the address we
        // care about, triggering one 32-byte prefetch at a time.
        while (actual_copy_dest != stop_copy_dest)
        {
            // Make sure we don't wrap around if we were near a memory border.
            if (((actual_copy_dest >> 24) & 0x1C) != stored_dest_bits)
            {
                // Re-init the top address control registers and the queue.
                stored_dest_bits = (actual_copy_dest >> 24) & 0x1C;
                QACR0 = stored_dest_bits;
                QACR1 = stored_dest_bits;
            }

            // First, prefetch the bytes we will need in the next cycle.
            __asm__("pref @%0" : : "r"(srcptr + 8));

            // Now, load the destination queue with the next 32 bytes from the source.
            queue[0] = *srcptr++;
            queue[1] = *srcptr++;
            queue[2] = *srcptr++;
            queue[3] = *srcptr++;
            queue[4] = *srcptr++;
            queue[5] = *srcptr++;
            queue[6] = *srcptr++;
            queue[7] = *srcptr++;

            // Finally, trigger the store of this data
            __asm__("pref @%0" : : "r"(queue));
            queue += 8;
            actual_copy_dest += 32;
        }

        // Finally, attempt a new write to both queues in order to stall the CPU until the
        // last write is done.
        queue = (uint32_t *)STORE_QUEUE_BASE;
        queue[0] = 0;
        queue[8] = 0;

        // We held the lock and succeeded at memsetting.
        successful = 1;
        mutex_unlock(&queue_mutex);
    }

    return successful;
}

void call_unmanaged(void (*call)())
{
    // Initiate kernel shutdown.
    _shutdown();

    // Call the unmanaged function.
    call();

    // Finally, exit from the program if it ever returns.
    _exit(0);
}

void enter_test_mode()
{
    // Look up the address of the enter test mode syscall.
    uint32_t test_mode_syscall = SYSCALL_VECTOR_BASE[SYSCALL_ENTER_TEST_MODE] | UNCACHED_MIRROR;

    // Call it.
    call_unmanaged((void (*)())test_mode_syscall);
}

void restart_game()
{
    // Look up the entrypoint and jump to that.
    call_unmanaged((void (*)())START_ADDR);
}

unsigned int utf8_strlen(const char * const str)
{
    uint8_t *data = (uint8_t *)str;
    unsigned int max = strlen(str);
    unsigned int len = 0;
    unsigned int pos = 0;

    while (pos < max)
    {
        if ((data[pos] & 0x80) == 0)
        {
            len ++;
            pos ++;
        }
        else if((data[pos] & 0xE0) == 0xC0)
        {
            len ++;
            pos += 2;
        }
        else if((data[pos] & 0xF0) == 0xE0)
        {
            len ++;
            pos += 3;
        }
        else if((data[pos] & 0xF1) == 0xF0)
        {
            len ++;
            pos += 4;
        }
        else
        {
            // Error!
            return 0;
        }
    }

    return len;
}

uint32_t *utf8_convert(const char * const str)
{
    // First make some room for the output.
    unsigned int length = utf8_strlen(str);
    uint32_t *chars = malloc(sizeof(uint32_t) * (length + 1));
    if (chars == 0)
    {
        return 0;
    }
    memset(chars, 0, sizeof(uint32_t) * (length + 1));

    // Now, convert characters one at a time.
    uint8_t *data = (uint8_t *)str;
    unsigned int inpos = 0;
    unsigned int outpos = 0;

    while (outpos < length)
    {
        if ((data[inpos] & 0x80) == 0)
        {
            chars[outpos] = data[inpos] & 0x7F;

            inpos ++;
            outpos ++;
        }
        else if((data[inpos] & 0xE0) == 0xC0)
        {
            chars[outpos] = ((data[inpos] & 0x1F) << 6) | (data[inpos + 1] & 0x3F);

            inpos += 2;
            outpos ++;
        }
        else if((data[inpos] & 0xF0) == 0xE0)
        {
            chars[outpos] = ((data[inpos] & 0x0F) << 12) | ((data[inpos + 1] & 0x3F) << 6) | (data[inpos + 2] & 0x3F);

            inpos += 3;
            outpos ++;
        }
        else if((data[inpos] & 0xF1) == 0xF0)
        {
            chars[outpos] = ((data[inpos] & 0x03) << 18) | ((data[inpos + 1] & 0x3F) << 12) | ((data[inpos + 2] & 0x3F) << 6) | (data[inpos + 3] & 0x3F);

            inpos += 4;
            outpos ++;
        }
        else
        {
            // Error!
            break;
        }
    }

    return chars;
}
