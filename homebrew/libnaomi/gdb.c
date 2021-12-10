#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <sys/signal.h>
#include "naomi/cart.h"
#include "naomi/thread.h"
#include "naomi/gdb.h"
#include "naomi/system.h"
#include "irqstate.h"

// Maximum size of packet between host and target.
#define MAX_PACKET_SIZE 512

// The location of our host/target communications buffer, will be set by the
// host upon first successfully received packet.
static uint32_t buffer_offset = 0;

// Any pending GDB response that we wish to send to the host.
static char response_packet[MAX_PACKET_SIZE + 1];
static int response_length = 0;

// Various operations that can be targetted at a particular thread.
#define OPERATION_REGISTERS 0
#define OPERATION_CONTINUE 1
#define MAX_OPERATIONS 2

// Which current thread particular operations are targetted at if they are executed.
static long int threadids[MAX_OPERATIONS] = { 0 };

// The reason we are currently stopped. If we aren't stopped, this should be SIGTRAP
// to signify that when GDB connects for the first time that we halted due to it.
static int haltreason = SIGTRAP;

// Whether we're in single-stepping mode or not.
static int stepped = 0;

// Is there a response available to send to the host?
int _gdb_has_response()
{
    return response_length > 0;
}

// Check the poll address that was given to us and verify that it is indeed a cartridge offset.
int _gdb_check_address(uint32_t address)
{
    uint8_t crc = ~(((address & 0xFF) + ((address >> 8) & 0xFF) + ((address >> 16) & 0xFF)) & 0xFF);
    return ((address >> 24) & 0xFF) == (crc & 0xFF);
}

uint32_t _gdb_handle_response()
{
    if (response_length == 0)
    {
        // No response, why are we being called?
        return 0;
    }
    if (buffer_offset == 0)
    {
        // No buffer, we haven't been contacted by the host yet.
        return 0;
    }

    // Format the response location and such, making sure to account for the
    // size of the header in the request.
    uint32_t response_address = buffer_offset + MAX_PACKET_SIZE + 4;
    uint32_t crc = ~(((response_address & 0xFF) + ((response_address >> 8) & 0xFF) + ((response_address >> 16) & 0xFF)) & 0xFF);
    uint32_t response = ((crc << 24) & 0xFF000000) | (response_address & 0x00FFFFFF);

    // Copy the response over.
    if (response_length & 1)
    {
        response_packet[response_length] = 0;
        cart_write(response_address, response_packet, response_length + 1);
    }
    else
    {
        cart_write(response_address, response_packet, response_length);
    }

    // Wipe out the packet now that we sent it.
    response_length = 0;

    // Return where the packet is so the host can find it.
    return response;
}

void _gdb_send_valid_response(char *response, ...)
{
    // Write out that it's valid.
    uint32_t valid = 0xFFFFFFFF;
    memcpy(&response_packet[0], &valid, 4);

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
    memcpy(&response_packet[4], &size, 4);

    // Write out the packet data itself.
    memcpy(&response_packet[8], buffer, size);

    // Mark that we have a packet.
    response_length = size + 8;
}

void _gdb_send_invalid_response()
{
    // Write out that it's invalid.
    uint32_t valid = 0x0;
    memcpy(&response_packet[0], &valid, 4);

    // Write out how big it is.
    uint32_t size = 0;
    memcpy(&response_packet[4], &size, 4);

    // Mark that we have a packet.
    response_length = 8;
}

void _gdb_send_acknowledge_response()
{
    // Write out that it's valid.
    uint32_t valid = 0xFFFFFFFF;
    memcpy(&response_packet[0], &valid, 4);

    // Write out that we just want a positive ack.
    uint32_t size = 0xFFFFFFFF;
    memcpy(&response_packet[4], &size, 4);

    // Mark that we have a packet.
    response_length = 8;
}

void _gdb_set_haltreason(int reason)
{
    // Set the halt reason for future queries.
    haltreason = reason;

    // If we have a buffer, that means GDB is listening, so we want to send it
    // a packet now that says why we halted. If not, then it will find out when
    // it connects to us later.
    if (buffer_offset)
    {
        _gdb_send_valid_response("S%02X", haltreason);
    }
}

// Constants borrowed kindly from existing sh stubs all over the net.
#define COND_BR_MASK            0xff00
#define UCOND_DBR_MASK          0xe000
#define UCOND_RBR_MASK          0xf0df

#define COND_DISP               0x00ff
#define UCOND_DISP              0x0fff
#define UCOND_REG               0x0f00

#define BF_INSTR                0x8b00
#define BT_INSTR                0x8900
#define BFS_INSTR               0x8f00
#define BTS_INSTR               0x8d00
#define BRA_INSTR               0xa000
#define BRAF_INSTR              0x0023
#define BSRF_INSTR              0x0003
#define BSR_INSTR               0xb000
#define JMP_INSTR               0x402b
#define JSR_INSTR               0x400b
#define RTS_INSTR               0x000b
#define RTE_INSTR               0x002b

#define T_BIT_MASK              0x0001

// TRAPA 253
#define SSTEP_INSTR             0xC3FD

typedef struct
{
    short *pc_offset;
    short instruction;
} step_data_t;

// Single step breakpoint.
static step_data_t single_step = { 0 };

// User-set breakpoints.
#define MAX_BREAKPOINTS 16
static step_data_t breakpoints[MAX_BREAKPOINTS] = { 0 };

void _gdb_activate_single_step(irq_state_t *cur_state)
{
    short *instrMem = (short *)cur_state->pc;
    unsigned short opcode = *instrMem;
    stepped = 1;

    if ((opcode & COND_BR_MASK) == BT_INSTR)
    {
        // BT instruction, look up the displacement if the SR register true bit is set.
        if (cur_state->sr & T_BIT_MASK)
        {
            int displacement = (opcode & COND_DISP) << 1;
            if (displacement & 0x80)
            {
                displacement |= 0xffffff00;
            }

            // Remember PC points to second instruction after PC of branch so add 4.
            instrMem = (short *)(cur_state->pc + displacement + 4);
        }
        else
        {
            instrMem += 1;
        }
    }
    else if ((opcode & COND_BR_MASK) == BF_INSTR)
    {
        // BT instruction, look up the displacement if the SR register true bit is not set.
        if (cur_state->sr & T_BIT_MASK)
        {
            instrMem += 1;
        }
        else
        {
            int displacement = (opcode & COND_DISP) << 1;
            if (displacement & 0x80)
            {
                displacement |= 0xffffff00;
            }

            // Remember PC points to second instruction after PC of branch so add 4.
            instrMem = (short *)(cur_state->pc + displacement + 4);
        }
    }
    else if ((opcode & COND_BR_MASK) == BTS_INSTR)
    {
        // BTS instruction, look up the displacement if the SR register true bit is set.
        // This has a branch delay slot where we cannot have a breakpoint, so handle that.
        if (cur_state->sr & T_BIT_MASK)
        {
            int displacement = (opcode & COND_DISP) << 1;
            if (displacement & 0x80)
            {
                displacement |= 0xffffff00;
            }

            // Remember PC points to second instruction after PC of branch so add 4.
            instrMem = (short *)(cur_state->pc + displacement + 4);
        }
        else
        {
            /* We should not place breakpoints at the slot */
            instrMem += 2;
        }
    }
    else if ((opcode & COND_BR_MASK) == BFS_INSTR)
    {
        // BFS instruction, look up the displacement if the SR register true bit is set.
        // This has a branch delay slot where we cannot have a breakpoint, so handle that.
        if (cur_state->sr & T_BIT_MASK)
        {
            /* We should not place trapa at the slot */
            instrMem += 2;
        }
        else
        {
            int displacement = (opcode & COND_DISP) << 1;
            if (displacement & 0x80)
            {
                displacement |= 0xffffff00;
            }

            // Remember PC points to second instruction after PC of branch so add 4.
            instrMem = (short *)(cur_state->pc + displacement + 4);
        }
    }
    else if ((opcode & UCOND_DBR_MASK) == BRA_INSTR)
    {
        // BRA/BSR instructions.
        int displacement = (opcode & UCOND_DISP) << 1;
        if (displacement & 0x0800)
        {
            displacement |= 0xfffff000;
        }

        // Remember PC points to second instruction after PC of branch so add 4.
        instrMem = (short *)(cur_state->pc + displacement + 4);
    }
    else if ((opcode & UCOND_RBR_MASK) == JSR_INSTR)
    {
        // JMP/JSR, need to look up the register.
        int reg = (char) ((opcode & UCOND_REG) >> 8);
        instrMem = (short *)(cur_state->gp_regs[reg]);
    }
    else if ((opcode & UCOND_RBR_MASK) == BSRF_INSTR) /* BRAF/BSRF */
    {
        // BRAF/BSRF, need to look up registers and PC.
        int reg = (char) ((opcode & UCOND_REG) >> 8);
        instrMem = (short *)(cur_state->gp_regs[reg] + (cur_state->pc & 0xfffffffc) + 4);
    }
    else if (opcode == RTS_INSTR)
    {
        // Return from subroutine, just grab the PR register.
        instrMem = (short *)cur_state->pr;
    }
    else if (opcode == RTE_INSTR)
    {
        // Return from interrupt context, just grab R15. We should, however,
        // never get to this point because we should never be debugging the kernel.
        instrMem = (short *)cur_state->gp_regs[15];
    }
    else
    {
        // Every other instruction should continue on to the next PC. */
        instrMem += 1;
    }

    // Remember the old instruction so we can replace it after we halt on it.
    single_step.pc_offset = instrMem;
    single_step.instruction = *instrMem;

    // Set the current instruction at this memory location to a TRAPA which will cause a breakpoint.
    *instrMem = SSTEP_INSTR;

    // Flush instruction cache so that we don't end up infinite-looping.
    icache_flush_range(instrMem, 2);
}

// Forward definition to disable calling back into the debugger for cases where
// the debugger itself is hosed.
void _irq_disable_debugging();

void _gdb_deactivate_single_step(irq_state_t *cur_state)
{
    if (stepped)
    {
        // Look up old address, rewrite the original instruction.
        short *instrMem = single_step.pc_offset;
        *instrMem = single_step.instruction;
        icache_flush_range(instrMem, 2);

        // Invariant check to catch cache or other subtle issues.
        if (cur_state->pc != (uint32_t)single_step.pc_offset)
        {
            // If this happens, that means we got a "trapa 253" exception, but it
            // wasn't on the line we just modified. That means we have some sort
            // of rogue trap instruction. With incorrect caching this most likely
            // happens when the ICACHE doesn't get flushed when changing an instruction
            // back. The danger of not catching this is that we end up infinite looping
            // trying to single-step.
            _irq_disable_debugging();
            _irq_display_invariant("step failure", "resume address %08lX != modification address %08lX", cur_state->pc, single_step.pc_offset);
        }

        // Clear original instruction so we never match with it again.
        single_step.pc_offset = 0;
        single_step.instruction = 0;
    }

    // Disable single-stepping.
    stepped = 0;
}

uint32_t bs(uint32_t val)
{
    return ((val >> 24) & 0x000000FF) | ((val >> 8) & 0x0000FF00) | ((val << 8) & 0x00FF0000) | ((val << 24) & 0xFF000000);
}

unsigned int _gdb_hex2int(char **buffer, int size)
{
    unsigned int number = 0;

    while(size > 0 && **buffer != 0)
    {
        // Grab the next digit, advance the buffer itself.
        char digit = **buffer;
        (*buffer)++;
        size--;

        // Calculate the number itself.
        unsigned int newval = 0;

        if (digit >= '0' && digit <= '9')
        {
            newval = digit - '0';
        }
        else if (digit >= 'a' && digit <= 'f')
        {
            newval = (digit - 'a') + 0xa;
        }
        else if (digit >= 'A' && digit <= 'F')
        {
            newval = (digit - 'A') + 0xA;
        }

        // Add the new value in
        number = (number << 4) | newval;
    }

    // Return the final number.
    return number;
}

unsigned int _gdb_hex2intdefault(char **buffer, int size, unsigned int def)
{
    if (**buffer == 0)
    {
        return def;
    }
    else
    {
        return _gdb_hex2int(buffer, size);
    }
}

// Prototypes from the threading library for working out threads.
uint32_t _thread_current_id(irq_state_t *cur_state);
irq_state_t *_thread_get_regs(uint32_t threadid);

int _gdb_handle_command(uint32_t address, irq_state_t *cur_state)
{
    // First, read the command itself.
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

    // Now, remember where our buffer is for responses.
    buffer_offset = address;

    // Now, handle the command itself, returning whether we should remain halted or not.
    switch(cmdbuf[0])
    {
        case 'H':
        {
            // Grab the thread ID itself.
            unsigned int threadid = strtoul(&cmdbuf[2], NULL, 16);
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
                _gdb_send_valid_response("OK");
                return 1;
            }
            else
            {
                // Return EINVAL since we don't recognize this.
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }

            break;
        }
        case 'v':
        {
            // Various actions.
            if (strcmp(cmdbuf, "vCont?") == 0)
            {
                // Query to get what supported continue actions exist.
                _gdb_send_valid_response("vCont;c;s");
                return 1;
            }

            // All v packets that are unrecognized should, by spec, return "".
            _gdb_send_valid_response("");
            return 1;
        }
        case 'c':
        {
            // Continue thread.
            unsigned int newpc = 0;
            int newpc_set = 0;
            if (strlen(cmdbuf) > 1)
            {
                // Optional continue from PC address.
                newpc = strtoul(&cmdbuf[1], NULL, 16);
                newpc_set = 1;
            }

            long int threadid = threadids[OPERATION_CONTINUE];
            irq_state_t *state = 0;

            if (threadid == -1)
            {
                // We don't support getting registers from *ALL* threads.
                state = 0;
            }
            else if (threadid == 0)
            {
                // Read from current thread.
                state = cur_state;
            }
            else
            {
                // Read from particular thread.
                state = _thread_get_regs(threadid);
            }

            if (state == 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }
            else
            {
                // Make sure that, unless an exception occurs, the next halt reason
                // will be a user trap exception.
                haltreason = SIGTRAP;
                if (newpc_set)
                {
                    state->pc = newpc;
                }

                // Wake up and continue processing, no longer halt in GDB. Don't send a packet.
                _gdb_send_acknowledge_response();
                return 0;
            }

            break;
        }
        case 'C':
        {
            // We ignore the signal, but we need to check and see if there's an address
            // to set the PC to.
            unsigned int newpc = 0;
            int newpc_set = 0;
            char *semiloc = 0;
            strtoul(&cmdbuf[1], &semiloc, 16);

            if (semiloc == 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }
            if (semiloc[0] == ';')
            {
                // This includes an address. Continue from that PC address.
                newpc = strtoul(&semiloc[1], NULL, 16);
                newpc_set = 1;
            }
            else if (semiloc[0] != 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }

            // Continue thread with signal, ignore the signal.
            long int threadid = threadids[OPERATION_CONTINUE];
            irq_state_t *state = 0;

            if (threadid == -1)
            {
                // We don't support getting registers from *ALL* threads.
                state = 0;
            }
            else if (threadid == 0)
            {
                // Read from current thread.
                state = cur_state;
            }
            else
            {
                // Read from particular thread.
                state = _thread_get_regs(threadid);
            }

            if (state == 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }
            else
            {
                // Make sure that, unless an exception occurs, the next halt reason
                // will be a user trap exception.
                haltreason = SIGTRAP;
                if (newpc_set)
                {
                    state->pc = newpc;
                }

                // Wake up and continue processing, no longer halt in GDB. Don't send a packet.
                _gdb_send_acknowledge_response();
                return 0;
            }

            break;
        }
        case 's':
        {
            // Single-step thread.
            unsigned int newpc = 0;
            int newpc_set = 0;
            if (strlen(cmdbuf) > 1)
            {
                // Optional continue from PC address.
                newpc = strtoul(&cmdbuf[1], NULL, 16);
                newpc_set = 1;
            }

            long int threadid = threadids[OPERATION_CONTINUE];
            irq_state_t *state = 0;

            if (threadid == -1)
            {
                // We don't support getting registers from *ALL* threads.
                state = 0;
            }
            else if (threadid == 0)
            {
                // Read from current thread.
                state = cur_state;
            }
            else
            {
                // Read from particular thread.
                state = _thread_get_regs(threadid);
            }

            if (state == 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }
            else
            {
                // Make sure that, unless an exception occurs, the next halt reason
                // will be a user trap exception.
                haltreason = SIGTRAP;
                if (newpc_set)
                {
                    state->pc = newpc;
                }

                // Wake up and continue processing, no longer halt in GDB. Don't even
                // send a response, since we are going to end up back here real quick
                // and we want the controlling host to poll us until we hit the next
                // breakpoint and send the halt reason.
                _gdb_activate_single_step(state);
                return 0;
            }

            break;
        }
        case 'S':
        {
            // We ignore the signal, but we need to check and see if there's an address
            // to set the PC to.
            unsigned int newpc = 0;
            int newpc_set = 0;
            char *semiloc = 0;
            strtoul(&cmdbuf[1], &semiloc, 16);

            if (semiloc == 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }
            if (semiloc[0] == ';')
            {
                // This includes an address. Continue from that PC address.
                newpc = strtoul(&semiloc[1], NULL, 16);
                newpc_set = 1;
            }
            else if (semiloc[0] != 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }

            // Signle step thread with signal, ignore the signal.
            long int threadid = threadids[OPERATION_CONTINUE];
            irq_state_t *state = 0;

            if (threadid == -1)
            {
                // We don't support getting registers from *ALL* threads.
                state = 0;
            }
            else if (threadid == 0)
            {
                // Read from current thread.
                state = cur_state;
            }
            else
            {
                // Read from particular thread.
                state = _thread_get_regs(threadid);
            }

            if (state == 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }
            else
            {
                // Make sure that, unless an exception occurs, the next halt reason
                // will be a user trap exception.
                haltreason = SIGTRAP;
                if (newpc_set)
                {
                    state->pc = newpc;
                }

                // Wake up and continue processing, no longer halt in GDB. Don't even
                // send a response, since we are going to end up back here real quick
                // and we want the controlling host to poll us until we hit the next
                // breakpoint and send the halt reason.
                _gdb_activate_single_step(state);
                return 0;
            }

            break;
        }
        case 'D':
        {
            // Detach from debugging.
            _gdb_send_acknowledge_response();
            return 0;
        }
        case 'q':
        {
            // Various queries.
            if (strcmp(cmdbuf, "qTStatus") == 0)
            {
                // We don't support trace experiments.
                _gdb_send_valid_response("T0");
                return 1;
            }
            if (strcmp(cmdbuf, "qTfV") == 0)
            {
                // We don't support trace experiments.
                _gdb_send_valid_response("");
                return 1;
            }
            if (strcmp(cmdbuf, "qTfP") == 0)
            {
                // We don't support trace experiments.
                _gdb_send_valid_response("");
                return 1;
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

                _gdb_send_valid_response(threadbuf);
                return 1;
            }
            if (strcmp(cmdbuf, "qsThreadInfo") == 0)
            {
                // Technically we should continue sending threads, but I'm lazy
                // and haven't accounted for us overrunning our buffer above.
                _gdb_send_valid_response("l");
                return 1;
            }
            if (strncmp(cmdbuf, "qThreadExtraInfo,", 17) == 0)
            {
                // Query extra info about a particular thread.
                unsigned int threadid = strtoul(&cmdbuf[17], NULL, 16);
                thread_info_t info;
                thread_info(threadid, &info);

                char infobuf[(MAX_PACKET_SIZE / 2) + 1];
                sprintf(infobuf, "Name: %s, Alive: %s, Running: %s", info.name, info.alive ? "yes" : "no", info.running ? "yes" : "no");

                char outbuf[MAX_PACKET_SIZE + 1];
                outbuf[0] = 0;

                for (int i = 0; i < strlen(infobuf); i++)
                {
                    sprintf(outbuf + strlen(outbuf), "%02X", infobuf[i]);
                }
                _gdb_send_valid_response(outbuf);
                return 1;
            }
            if (strcmp(cmdbuf, "qAttached") == 0)
            {
                // We attached to an existing process.
                _gdb_send_valid_response("1");
                return 1;
            }
            if (strcmp(cmdbuf, "qC") == 0)
            {
                // Current thread ID.
                _gdb_send_valid_response("QC%lX", _thread_current_id(cur_state));
                return 1;
            }
            if (strcmp(cmdbuf, "qOffsets") == 0)
            {
                // Relocation offsets, we don't relocate.
                _gdb_send_valid_response("Text=0;Data=0;Bss=0");
                return 1;
            }

            break;
        }
        case 'g':
        {
            // Read registers.
            long int threadid = threadids[OPERATION_REGISTERS];
            irq_state_t *state = 0;

            if (threadid == -1)
            {
                // We don't support getting registers from *ALL* threads.
                state = 0;
            }
            else if (threadid == 0)
            {
                // Read from current thread.
                state = cur_state;
            }
            else
            {
                // Read from particular thread.
                state = _thread_get_regs(threadid);
            }

            if (state == 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }
            else
            {
                char regbuf[MAX_PACKET_SIZE + 1];
                regbuf[0] = 0;

                // Copy registers into the response in the right order according to regformats/reg-sh.dat.
                for (unsigned int i = 0; i < 16; i++)
                {
                    // Registers 0-15 in GDB mapping language.
                    sprintf(regbuf + strlen(regbuf), "%08lX", bs(state->gp_regs[i]));
                }

                // Copy special registers (registers 16-24 in GDB mapping language).
                sprintf(regbuf + strlen(regbuf), "%08lX", bs(state->pc));
                sprintf(regbuf + strlen(regbuf), "%08lX", bs(state->pr));
                sprintf(regbuf + strlen(regbuf), "%08lX", bs(state->gbr));
                sprintf(regbuf + strlen(regbuf), "%08lX", bs(state->vbr));
                sprintf(regbuf + strlen(regbuf), "%08lX", bs(state->mach));
                sprintf(regbuf + strlen(regbuf), "%08lX", bs(state->macl));
                sprintf(regbuf + strlen(regbuf), "%08lX", bs(state->sr));
                sprintf(regbuf + strlen(regbuf), "%08lX", bs(state->fpul));
                sprintf(regbuf + strlen(regbuf), "%08lX", bs(state->fpscr));

                // Copy floating point registers.
                for (unsigned int i = 0; i < 16; i++)
                {
                    // Registers 25-40 in GDB mapping language.
                    sprintf(regbuf + strlen(regbuf), "%08lX", bs(state->fr[i]));
                }

                // According to sh-tdep.h, 41 is SSR and 42 is SPC which are really only
                // available inside interrupt context. We don't allow debugging inside
                // the kernel, so we don't return these registers.
                sprintf(regbuf + strlen(regbuf), "xxxxxxxx");
                sprintf(regbuf + strlen(regbuf), "xxxxxxxx");

                // 43-50 are bank 0 general purpose registers R0-R7, we don't support
                // changing banks, so we ignore those.
                for (unsigned int i = 0; i < 8; i++)
                {
                    sprintf(regbuf + strlen(regbuf), "xxxxxxxx");
                }

                // 51-58 are bank 1 general purpose registers R0-R7, we don't support
                // changing banks, so we ignore those.
                for (unsigned int i = 0; i < 8; i++)
                {
                    sprintf(regbuf + strlen(regbuf), "xxxxxxxx");
                }

                _gdb_send_valid_response(regbuf);
                return 1;
            }

            break;
        }
        case 'G':
        {
            // Write registers.
            long int threadid = threadids[OPERATION_REGISTERS];
            irq_state_t *state = 0;

            if (threadid == -1)
            {
                // We don't support getting registers from *ALL* threads.
                state = 0;
            }
            else if (threadid == 0)
            {
                // Read from current thread.
                state = cur_state;
            }
            else
            {
                // Read from particular thread.
                state = _thread_get_regs(threadid);
            }

            if (state == 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }
            else
            {
                // Hex data follows, we must decode each register in order.
                char *dataloc = &cmdbuf[1];

                // Copy registers into the response in the right order according to regformats/reg-sh.dat.
                for (unsigned int i = 0; i < 16; i++)
                {
                    // Registers 0-15 in GDB mapping language.
                    state->gp_regs[i] = bs(_gdb_hex2intdefault(&dataloc, 8, state->gp_regs[i]));
                }

                // Copy special registers (registers 16-24 in GDB mapping language).
                state->pc = bs(_gdb_hex2intdefault(&dataloc, 8, state->pc));
                state->pr = bs(_gdb_hex2intdefault(&dataloc, 8, state->pr));
                state->gbr = bs(_gdb_hex2intdefault(&dataloc, 8, state->gbr));
                state->vbr = bs(_gdb_hex2intdefault(&dataloc, 8, state->vbr));
                state->mach = bs(_gdb_hex2intdefault(&dataloc, 8, state->mach));
                state->macl = bs(_gdb_hex2intdefault(&dataloc, 8, state->macl));
                state->sr = bs(_gdb_hex2intdefault(&dataloc, 8, state->sr));
                state->fpul = bs(_gdb_hex2intdefault(&dataloc, 8, state->fpul));
                state->fpscr = bs(_gdb_hex2intdefault(&dataloc, 8, state->fpscr));

                // Copy floating point registers.
                for (unsigned int i = 0; i < 16; i++)
                {
                    // Registers 25-40 in GDB mapping language.
                    state->fr[i] = bs(_gdb_hex2intdefault(&dataloc, 8, state->fr[i]));
                }

                // The rest of the registers we don't support writing, so don't even bother iterating
                // through the rest of the data if it exists.
                _gdb_send_valid_response("OK");
                return 1;
            }

            break;
        }
        case 'p':
        {
            // Read single register.
            long int threadid = threadids[OPERATION_REGISTERS];
            irq_state_t *state = 0;

            if (threadid == -1)
            {
                // We don't support getting registers from *ALL* threads.
                state = 0;
            }
            else if (threadid == 0)
            {
                // Read from current thread.
                state = cur_state;
            }
            else
            {
                // Read from particular thread.
                state = _thread_get_regs(threadid);
            }

            if (state == 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }
            else
            {
                unsigned int whichreg = strtoul(&cmdbuf[1], NULL, 16);
                switch(whichreg)
                {
                    case 0:
                    case 1:
                    case 2:
                    case 3:
                    case 4:
                    case 5:
                    case 6:
                    case 7:
                    case 8:
                    case 9:
                    case 10:
                    case 11:
                    case 12:
                    case 13:
                    case 14:
                    case 15:
                    {
                        // GP register.
                        _gdb_send_valid_response("%08lX", bs(state->gp_regs[whichreg]));
                        return 1;
                    }
                    case 16:
                    {
                        // PC register.
                        _gdb_send_valid_response("%08lX", bs(state->pc));
                        return 1;
                    }
                    case 17:
                    {
                        // PR register.
                        _gdb_send_valid_response("%08lX", bs(state->pr));
                        return 1;
                    }
                    case 18:
                    {
                        // GBR register.
                        _gdb_send_valid_response("%08lX", bs(state->gbr));
                        return 1;
                    }
                    case 19:
                    {
                        // VBR register.
                        _gdb_send_valid_response("%08lX", bs(state->vbr));
                        return 1;
                    }
                    case 20:
                    {
                        // MACH register.
                        _gdb_send_valid_response("%08lX", bs(state->mach));
                        return 1;
                    }
                    case 21:
                    {
                        // MACL register.
                        _gdb_send_valid_response("%08lX", bs(state->macl));
                        return 1;
                    }
                    case 22:
                    {
                        // SR register.
                        _gdb_send_valid_response("%08lX", bs(state->sr));
                        return 1;
                    }
                    case 23:
                    {
                        // FPUL register.
                        _gdb_send_valid_response("%08lX", bs(state->fpul));
                        return 1;
                    }
                    case 24:
                    {
                        // FPSCR register.
                        _gdb_send_valid_response("%08lX", bs(state->fpscr));
                        return 1;
                    }
                    case 25:
                    case 26:
                    case 27:
                    case 28:
                    case 29:
                    case 30:
                    case 31:
                    case 32:
                    case 33:
                    case 34:
                    case 35:
                    case 36:
                    case 37:
                    case 38:
                    case 39:
                    case 40:
                    {
                        // FPU register.
                        _gdb_send_valid_response("%08lX", bs(state->fr[whichreg - 25]));
                        return 1;
                    }
                    case 41:
                    case 42:
                    {
                        // SSR/SPC register, we don't capture this.
                        _gdb_send_valid_response("xxxxxxxx");
                        return 1;
                    }
                    case 43:
                    case 44:
                    case 45:
                    case 46:
                    case 47:
                    case 48:
                    case 49:
                    case 50:
                    {
                        // Register Bank 0 R0-R7, we don't capture these.
                        _gdb_send_valid_response("xxxxxxxx");
                        return 1;
                    }
                    case 51:
                    case 52:
                    case 53:
                    case 54:
                    case 55:
                    case 56:
                    case 57:
                    case 58:
                    {
                        // Register Bank 1 R0-R7, we don't capture these.
                        _gdb_send_valid_response("xxxxxxxx");
                        return 1;
                    }
                    default:
                    {
                        // Unrecognized register.
                        _gdb_send_valid_response("E%02X", EINVAL);
                        return 1;
                    }
                }
            }

            break;
        }
        case 'P':
        {
            // Write single register.
            long int threadid = threadids[OPERATION_REGISTERS];
            irq_state_t *state = 0;

            if (threadid == -1)
            {
                // We don't support putting registers to *ALL* threads.
                state = 0;
            }
            else if (threadid == 0)
            {
                // Write to current thread.
                state = cur_state;
            }
            else
            {
                // Write to particular thread.
                state = _thread_get_regs(threadid);
            }

            if (state == 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }
            else
            {
                char *equalloc = 0;
                unsigned int whichreg = strtoul(&cmdbuf[1], &equalloc, 16);

                if (equalloc == 0)
                {
                    _gdb_send_valid_response("E%02X", EINVAL);
                    return 1;
                }
                if (equalloc[0] != '=')
                {
                    _gdb_send_valid_response("E%02X", EINVAL);
                    return 1;
                }

                unsigned int regval = bs(strtoul(&equalloc[1], NULL, 16));

                switch(whichreg)
                {
                    case 0:
                    case 1:
                    case 2:
                    case 3:
                    case 4:
                    case 5:
                    case 6:
                    case 7:
                    case 8:
                    case 9:
                    case 10:
                    case 11:
                    case 12:
                    case 13:
                    case 14:
                    case 15:
                    {
                        // GP register.
                        state->gp_regs[whichreg] = regval;
                        _gdb_send_valid_response("OK");
                        return 1;
                    }
                    case 16:
                    {
                        // PC register.
                        state->pc = regval;
                        _gdb_send_valid_response("OK");
                        return 1;
                    }
                    case 17:
                    {
                        // PR register.
                        state->pr = regval;
                        _gdb_send_valid_response("OK");
                        return 1;
                    }
                    case 18:
                    {
                        // GBR register.
                        state->gbr = regval;
                        _gdb_send_valid_response("OK");
                        return 1;
                    }
                    case 19:
                    {
                        // VBR register.
                        state->vbr = regval;
                        _gdb_send_valid_response("OK");
                        return 1;
                    }
                    case 20:
                    {
                        // MACH register.
                        state->mach = regval;
                        _gdb_send_valid_response("OK");
                        return 1;
                    }
                    case 21:
                    {
                        // MACL register.
                        state->macl = regval;
                        _gdb_send_valid_response("OK");
                        return 1;
                    }
                    case 22:
                    {
                        // SR register.
                        state->sr = regval;
                        _gdb_send_valid_response("OK");
                        return 1;
                    }
                    case 23:
                    {
                        // FPUL register.
                        state->fpul = regval;
                        _gdb_send_valid_response("OK");
                        return 1;
                    }
                    case 24:
                    {
                        // FPSCR register.
                        state->fpscr = regval;
                        _gdb_send_valid_response("OK");
                        return 1;
                    }
                    case 25:
                    case 26:
                    case 27:
                    case 28:
                    case 29:
                    case 30:
                    case 31:
                    case 32:
                    case 33:
                    case 34:
                    case 35:
                    case 36:
                    case 37:
                    case 38:
                    case 39:
                    case 40:
                    {
                        // FPU register.
                        state->fr[whichreg - 25] = regval;
                        _gdb_send_valid_response("OK");
                        return 1;
                    }
                    case 41:
                    case 42:
                    {
                        // SSR/SPC register, we don't save this.
                        _gdb_send_valid_response("E%02X", EINVAL);
                        return 1;
                    }
                    case 43:
                    case 44:
                    case 45:
                    case 46:
                    case 47:
                    case 48:
                    case 49:
                    case 50:
                    {
                        // Register Bank 0 R0-R7, we don't save these.
                        _gdb_send_valid_response("E%02X", EINVAL);
                        return 1;
                    }
                    case 51:
                    case 52:
                    case 53:
                    case 54:
                    case 55:
                    case 56:
                    case 57:
                    case 58:
                    {
                        // Register Bank 1 R0-R7, we don't save these.
                        _gdb_send_valid_response("E%02X", EINVAL);
                        return 1;
                    }
                    default:
                    {
                        // Unrecognized register.
                        _gdb_send_valid_response("E%02X", EINVAL);
                        return 1;
                    }
                }
            }

            break;
        }
        case 'm':
        {
            // Read memory.
            char *commaloc = 0;
            unsigned int memloc = strtoul(&cmdbuf[1], &commaloc, 16);

            if (commaloc == 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }
            if (commaloc[0] != ',')
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }

            unsigned int memsize = strtoul(&commaloc[1], NULL, 16);
            char membuf[MAX_PACKET_SIZE + 1];
            membuf[0] = 0;

            if (memsize > (MAX_PACKET_SIZE / 2))
            {
                _gdb_send_valid_response("E%02X", ENOMEM);
                return 1;
            }

            for (long int i = 0; i < memsize; i++)
            {
                sprintf(membuf + strlen(membuf), "%02X", *((uint8_t *)(memloc + i)));
            }

            _gdb_send_valid_response(membuf);
            return 1;
        }
        case 'M':
        {
            // Write memory.
            char *commaloc = 0;
            unsigned int memloc = strtoul(&cmdbuf[1], &commaloc, 16);

            if (commaloc == 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }
            if (commaloc[0] != ',')
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }

            char *colonloc = 0;
            unsigned int memsize = strtoul(&commaloc[1], &colonloc, 16);

            if (colonloc == 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }
            if (colonloc[0] != ':')
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }

            char *dataloc = (char *)&colonloc[1];
            for (long int i = 0; i < memsize; i++)
            {
                *((uint8_t *)(memloc + i)) = _gdb_hex2int(&dataloc, 2);
            }

            icache_flush_range((void *)memloc, memsize);

            _gdb_send_valid_response("OK");
            return 1;
        }
        case 'T':
        {
            // Thread alive query.
            unsigned int threadid = strtoul(&cmdbuf[1], 0, 16);
            irq_state_t *state = _thread_get_regs(threadid);

            if (state != 0)
            {
                _gdb_send_valid_response("OK");
                return 1;
            }
            else
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }

            break;
        }
        case 'X':
        {
            // Write memory (binary data provided).
            char *commaloc = 0;
            unsigned int memloc = strtoul(&cmdbuf[1], &commaloc, 16);

            if (commaloc == 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }
            if (commaloc[0] != ',')
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }

            char *colonloc = 0;
            unsigned int memsize = strtoul(&commaloc[1], &colonloc, 16);

            if (colonloc == 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }
            if (colonloc[0] != ':')
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }

            uint8_t *dataloc = (uint8_t *)&colonloc[1];

            for (long int i = 0; i < memsize; i++)
            {
                *((uint8_t *)(memloc + i)) = dataloc[i];
            }

            icache_flush_range((void *)memloc, memsize);

            _gdb_send_valid_response("OK");
            return 1;
        }
        case '?':
        {
            // Query why we were stopped.
            _gdb_send_valid_response("S%02X", haltreason);
            return 1;
        }
        case 'k':
        {
            // Kill the current running process. Note that this will jump
            // to the entrypoint and act like the game was just booted.
            restart_game();
            break;
        }
        case 'Z':
        {
            // Insert some sort of breakpoint.
            char *commaloc = 0;
            unsigned int bptype = strtoul(&cmdbuf[1], &commaloc, 16);

            if (commaloc == 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }
            if (commaloc[0] != ',')
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }

            unsigned int memloc = strtoul(&commaloc[1], &commaloc, 16);

            if (commaloc == 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }
            if (commaloc[0] != ',')
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }

            unsigned int memsize = strtoul(&commaloc[1], NULL, 16);

            switch(bptype)
            {
                case 0:
                {
                    // Software breakpoint.
                    if (memsize != 2)
                    {
                        _gdb_send_valid_response("E%02X", EINVAL);
                        return 1;
                    }

                    // Replace the instruction at memloc with a TRAPA 253.
                    short *instLoc = (short *)memloc;
                    int found = 0;
                    for (int i = 0; i < MAX_BREAKPOINTS; i++)
                    {
                        if (breakpoints[i].pc_offset == 0)
                        {
                            // This should be used to save the memory.
                            breakpoints[i].pc_offset = instLoc;
                            breakpoints[i].instruction = *instLoc;
                            *instLoc = SSTEP_INSTR;
                            icache_flush_range(instLoc, 2);

                            found = 1;
                            break;
                        }
                    }

                    if (found)
                    {
                        _gdb_send_valid_response("OK");
                        return 1;
                    }
                    else
                    {
                        _gdb_send_valid_response("E%02X", ENOMEM);
                        return 1;
                    }

                    break;
                }
                default:
                {
                    // Unrecognized or unsupported breakpoint type.
                    _gdb_send_valid_response("");
                    return 1;
                }
            }

            break;
        }
        case 'z':
        {
            // Remove some sort of breakpoint.
            char *commaloc = 0;
            unsigned int bptype = strtoul(&cmdbuf[1], &commaloc, 16);

            if (commaloc == 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }
            if (commaloc[0] != ',')
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }

            unsigned int memloc = strtoul(&commaloc[1], &commaloc, 16);

            if (commaloc == 0)
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }
            if (commaloc[0] != ',')
            {
                _gdb_send_valid_response("E%02X", EINVAL);
                return 1;
            }

            unsigned int memsize = strtoul(&commaloc[1], NULL, 16);

            switch(bptype)
            {
                case 0:
                {
                    // Software breakpoint.
                    if (memsize != 2)
                    {
                        _gdb_send_valid_response("E%02X", EINVAL);
                        return 1;
                    }

                    // Replace the instruction at memloc with a TRAPA 253.
                    short *instLoc = (short *)memloc;
                    int found = 0;
                    for (int i = 0; i < MAX_BREAKPOINTS; i++)
                    {
                        if (breakpoints[i].pc_offset == instLoc)
                        {
                            *instLoc = breakpoints[i].instruction;
                            icache_flush_range(instLoc, 2);

                            breakpoints[i].pc_offset = 0;
                            breakpoints[i].instruction = 0;
                            found = 1;
                            break;
                        }
                    }

                    if (found)
                    {
                        _gdb_send_valid_response("OK");
                        return 1;
                    }
                    else
                    {
                        _gdb_send_valid_response("E%02X", EINVAL);
                        return 1;
                    }

                    break;
                }
                default:
                {
                    // Unrecognized or unsupported breakpoint type.
                    _gdb_send_valid_response("");
                    return 1;
                }
            }

            break;
        }
    }

    // Unrecognized packet, so send a negative response.
    _gdb_send_invalid_response();

    // Return whether we should be in halting mode or not.
    return 1;
}

int _gdb_breakpoint_halt(irq_state_t *cur_state)
{
    // We hit our breakpoint that we laid down, so we need to reverse out
    // in order to re-run the instruction next time we are not halted.
    cur_state->pc -= 2;

    // Undo single-stepping, replacing the instruction with the original
    // so that we can re-execute it.
    _gdb_deactivate_single_step(cur_state);

    // Inform the host that we halted.
    haltreason = SIGTRAP;
    _gdb_send_valid_response("S%02X", haltreason);
    return 1;
}

int _gdb_user_halt(irq_state_t *cur_state)
{
    haltreason = SIGTRAP;
    _gdb_send_valid_response("S%02X", haltreason);
    return 1;
}

// Syscall for entering into GDB breakpoint mode if we need it.
void gdb_breakpoint()
{
    asm("trapa #255");
}
