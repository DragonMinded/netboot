#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include "naomi/cart.h"
#include "naomi/thread.h"
#include "irqstate.h"

#define MAX_PACKET_SIZE 512

static uint32_t response_address = 0;

#define OPERATION_REGISTERS 0
#define OPERATION_CONTINUE 1
#define MAX_OPERATIONS 2

static long int threadids[MAX_OPERATIONS] = { 0 };

#define TRAP_SIGNAL 5

static int haltreason = TRAP_SIGNAL;

int _gdb_has_response()
{
    return response_address != 0;
}

int _gdb_check_address(uint32_t address)
{
    uint8_t crc = ~(((address & 0xFF) + ((address >> 8) & 0xFF) + ((address >> 16) & 0xFF)) & 0xFF);
    return ((address >> 24) & 0xFF) == (crc & 0xFF);
}

uint32_t _gdb_make_address()
{
    uint32_t crc = ~(((response_address & 0xFF) + ((response_address >> 8) & 0xFF) + ((response_address >> 16) & 0xFF)) & 0xFF);
    uint32_t response = ((crc << 24) & 0xFF000000) | (response_address & 0x00FFFFFF);
    response_address = 0;
    return response;
}

void _gdb_make_valid_response(uint32_t address, char *response, ...)
{
    // Remember where we put this response.
    response_address = address;

    // Write out that it's valid.
    uint32_t valid = 0xFFFFFFFF;
    cart_write(address, &valid, 4);

    // Perform varargs against the response.
    static char buffer[MAX_PACKET_SIZE + 1];
    va_list args;
    va_start(args, response);
    int length = vsnprintf(buffer, MAX_PACKET_SIZE, response, args);
    va_end(args);

    if (length >= 0)
    {
        if (length > MAX_PACKET_SIZE)
        {
            length = MAX_PACKET_SIZE;
        }
        buffer[length] = 0;
    }

    // Write out how big it is.
    uint32_t size = strlen(buffer);
    cart_write(address + 4, &size, 4);

    if (size > 0)
    {
        if (size & 1)
        {
            // Safe to write an extra byte since it is always a null character.
            cart_write(address + 8, buffer, size + 1);
        }
        else
        {
            // Write out the actual data
            cart_write(address + 8, buffer, size);
        }
    }
}

void _gdb_make_invalid_response(uint32_t address)
{
    // Remember where we put this response.
    response_address = address;

    // Write out that it's invalid.
    uint32_t valid = 0x0;
    cart_write(address, &valid, 4);

    // Write out how big it is.
    uint32_t size = 0;
    cart_write(address + 4, &size, 4);
}

uint32_t bs(uint32_t val)
{
    return ((val >> 24) & 0x000000FF) | ((val >> 8) & 0x0000FF00) | ((val << 8) & 0x00FF0000) | ((val << 24) & 0xFF000000);
}

// Prototypes from the threading library for working out threads.
uint32_t _thread_current_id(irq_state_t *cur_state);

void _gdb_handle_command(uint32_t address, irq_state_t *cur_state)
{
    static char cmdbuf[MAX_PACKET_SIZE];
    uint32_t size = 0;

    memset(cmdbuf, 0, MAX_PACKET_SIZE);
    cart_read(&size, address, 4);
    if (size > 0)
    {
        // Make sure we read even numbers only.
        cart_read(cmdbuf, address + 4, (size + 1) & 0xFFFFFFFE);
        if (size & 1)
        {
            cmdbuf[size] = 0;
        }
    }

    switch(cmdbuf[0])
    {
        case 'H':
        {
            // Grab the thread ID itself.
            long int threadid = strtol(&cmdbuf[2], NULL, 16);
            int recognized = 0;

            // Set thread for subsequent operations.
            switch(cmdbuf[1])
            {
                case 'g':
                {
                    threadids[OPERATION_REGISTERS] = threadid;
                    recognized = 1;
                    break;
                }
                case 'c':
                {
                    threadids[OPERATION_CONTINUE] = threadid;
                    recognized = 1;
                    break;
                }
            }

            if (recognized)
            {
                // Return that we set the value up.
                _gdb_make_valid_response(address + MAX_PACKET_SIZE, "OK");
                return;
            }
            else
            {
                // Return EINVAL since we don't recognize this.
                _gdb_make_valid_response(address + MAX_PACKET_SIZE, "E%02X", EINVAL);
                return;
            }

            break;
        }
        case 'q':
        {
            // Various queries.
            if (strcmp(cmdbuf, "qTStatus") == 0)
            {
                // We don't support trace experiments.
                _gdb_make_valid_response(address + MAX_PACKET_SIZE, "T0");
                return;
            }
            if (strcmp(cmdbuf, "qTfV") == 0)
            {
                // We don't support trace experiments.
                _gdb_make_valid_response(address + MAX_PACKET_SIZE, "");
                return;
            }
            if (strcmp(cmdbuf, "qTfP") == 0)
            {
                // We don't support trace experiments.
                _gdb_make_valid_response(address + MAX_PACKET_SIZE, "");
                return;
            }
            if (strcmp(cmdbuf, "qfThreadInfo") == 0)
            {
                // Gather information about all threads on target.
                char threadbuf[MAX_PACKET_SIZE + 1];
                strcpy(threadbuf, "m");

                task_scheduler_info_t info;
                task_scheduler_info(&info);

                for (unsigned int i = 0; i < info.num_threads; i++)
                {
                    if (i > 0)
                    {
                        strcat(threadbuf, ",");
                    }

                    sprintf(threadbuf + strlen(threadbuf), "%lX", info.thread_ids[i]);
                }

                _gdb_make_valid_response(address + MAX_PACKET_SIZE, threadbuf);
                return;
            }
            if (strcmp(cmdbuf, "qsThreadInfo") == 0)
            {
                // Technically we should continue sending threads, but I'm lazy
                // and haven't accounted for us overrunning our buffer above.
                _gdb_make_valid_response(address + MAX_PACKET_SIZE, "l");
                return;
            }
            if (strcmp(cmdbuf, "qAttached") == 0)
            {
                // We attached to an existing process.
                _gdb_make_valid_response(address + MAX_PACKET_SIZE, "1");
                return;
            }
            if (strcmp(cmdbuf, "qC") == 0)
            {
                // Current thread ID.
                _gdb_make_valid_response(address + MAX_PACKET_SIZE, "QC%lX", _thread_current_id(cur_state));
                return;
            }
            if (strcmp(cmdbuf, "qOffsets") == 0)
            {
                // Relocation offsets, we don't relocate.
                _gdb_make_valid_response(address + MAX_PACKET_SIZE, "Text=0;Data=0;Bss=0");
                return;
            }

            break;
        }
        case 'g':
        {
            // Read registers.
            long int threadid = threadids[OPERATION_REGISTERS];

            if (threadid == -1)
            {
                // We don't support getting registers from *ALL* threads.
                _gdb_make_valid_response(address + MAX_PACKET_SIZE, "E%02X", EINVAL);
                return;
            }
            else if (threadid == 0)
            {
                // Read from current thread.
                char regbuf[MAX_PACKET_SIZE + 1];
                regbuf[0] = 0;

                // Copy registers into the response in the right order according to regformats/reg-sh.dat.
                for (unsigned int i = 0; i < 16; i++)
                {
                    sprintf(regbuf + strlen(regbuf), "%08lX", bs(cur_state->gp_regs[i]));
                }

                // Copy special registers.
                sprintf(regbuf + strlen(regbuf), "%08lX", bs(cur_state->pc));
                sprintf(regbuf + strlen(regbuf), "%08lX", bs(cur_state->pr));
                sprintf(regbuf + strlen(regbuf), "%08lX", bs(cur_state->gbr));
                sprintf(regbuf + strlen(regbuf), "%08lX", bs(cur_state->vbr));
                sprintf(regbuf + strlen(regbuf), "%08lX", bs(cur_state->mach));
                sprintf(regbuf + strlen(regbuf), "%08lX", bs(cur_state->sr));
                sprintf(regbuf + strlen(regbuf), "%08lX", bs(cur_state->fpul));
                sprintf(regbuf + strlen(regbuf), "%08lX", bs(cur_state->fpscr));

                // Copy floating point registers.
                for (unsigned int i = 0; i < 16; i++)
                {
                    sprintf(regbuf + strlen(regbuf), "%08lX", bs(cur_state->fr[i]));
                }

                _gdb_make_valid_response(address + MAX_PACKET_SIZE, regbuf);
                return;
            }
            else
            {
                // Read from specific thread. Not currently supported.
                _gdb_make_valid_response(address + MAX_PACKET_SIZE, "E%02X", EINVAL);
                return;
            }

            break;
        }
        case 'm':
        {
            // Read memory.
            char *commaloc = 0;
            long int memloc = strtol(&cmdbuf[1], &commaloc, 16);

            if (commaloc == 0)
            {
                _gdb_make_valid_response(address + MAX_PACKET_SIZE, "E%02X", EINVAL);
                return;
            }
            if (commaloc[0] != ',')
            {
                _gdb_make_valid_response(address + MAX_PACKET_SIZE, "E%02X", EINVAL);
                return;
            }

            long int memsize = strtol(&commaloc[1], NULL, 16);
            char membuf[MAX_PACKET_SIZE + 1];
            membuf[0] = 0;

            if (memsize >= (MAX_PACKET_SIZE / 2))
            {
                _gdb_make_valid_response(address + MAX_PACKET_SIZE, "E%02X", ENOMEM);
                return;
            }

            for (long int i = 0; i < memsize; i++)
            {
                sprintf(membuf + strlen(membuf), "%02X", *((uint8_t *)(memloc + i)));
            }

            _gdb_make_valid_response(address + MAX_PACKET_SIZE, membuf);
            return;
        }
        case '?':
        {
            // Query why we were stopped.
            _gdb_make_valid_response(address + MAX_PACKET_SIZE, "S%02X", haltreason);
            return;
        }
    }

    // Unrecognized packet, so send a negative response.
    _gdb_make_invalid_response(address + MAX_PACKET_SIZE);
}
