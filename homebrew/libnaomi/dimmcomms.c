#include <stdio.h>
#include <stdint.h>
#include "naomi/dimmcomms.h"
#include "naomi/interrupt.h"
#include "naomi/thread.h"
#include "holly.h"
#include "irqstate.h"

#define NAOMI_DIMM_COMMAND ((volatile uint16_t *)0xA05F703C)
#define NAOMI_DIMM_OFFSETL ((volatile uint16_t *)0xA05F7040)
#define NAOMI_DIMM_PARAMETERL ((volatile uint16_t *)0xA05F7044)
#define NAOMI_DIMM_PARAMETERH ((volatile uint16_t *)0xA05F7048)
#define NAOMI_DIMM_STATUS ((volatile uint16_t *)0xA05F704C)

#define CONST_NO_DIMM 0xFFFF
#define CONST_DIMM_HAS_COMMAND 0x8000
#define CONST_DIMM_COMMAND_MASK 0x7E00
#define CONST_DIMM_TOP_MASK 0x1FF

static peek_call_t global_peek_hook = 0;
static poke_call_t global_poke_hook = 0;

// Prototypes for GDB functionality.
int _gdb_check_address(uint32_t address);
int _gdb_handle_command(uint32_t address, irq_state_t *cur_state);
int _gdb_has_response();
uint32_t _gdb_handle_response();

int check_has_dimm_inserted()
{
    if (*NAOMI_DIMM_COMMAND == CONST_NO_DIMM) {
        return -1;
    }
    return 1;
}

int _dimm_command_handler(int halted, irq_state_t *cur_state)
{
    // Keep track of the top 8 bits of the address for peek/poke commands.
    static uint32_t base_address = 0;

    if ((HOLLY_EXTERNAL_IRQ_STATUS & HOLLY_EXTERNAL_INTERRUPT_DIMM_COMMS) != 0)
    {
        uint16_t dimm_command = *NAOMI_DIMM_COMMAND;
        if (dimm_command & CONST_DIMM_HAS_COMMAND)
        {
            // Get the command ID
            unsigned int dimm_command_id = (dimm_command & CONST_DIMM_COMMAND_MASK) >> 9;
            unsigned short retval = 0;
            unsigned short offsetl = 0;
            unsigned short paraml = 0;
            unsigned short paramh = 0;

            switch (dimm_command_id) {
                case 0:
                {
                    /* NOOP command */
                    retval = 1;
                    break;
                }
                case 1:
                {
                    /* Net Dimm firmware calls this "control read." Still not sure what it is. If this returns
                     * a valid nonzero value, the net dimm will request a bunch of pokes at addresses relative
                     * to this return. So its clearly returning the control structure, but what is that? On
                     * an H bios with net dimm 4.02 I get the address 0xc299394. */
                    retval = 1;
                    break;
                }
                case 3:
                {
                    /* Update base address */
                    base_address = (((*NAOMI_DIMM_PARAMETERH) & 0xFFFF) << 16) | ((*NAOMI_DIMM_PARAMETERL) & 0xFFFF);
                    retval = 1;
                    break;
                }
                case 4:
                {
                    /* Peek 8-bit value out of memory */
                    uint32_t address = (((dimm_command & CONST_DIMM_TOP_MASK) << 16) | ((*NAOMI_DIMM_OFFSETL) & 0xFFFF)) + base_address;

                    if (global_peek_hook)
                    {
                        paraml = global_peek_hook(address, 1) & 0xFF;
                    }

                    retval = 1;
                    break;
                }
                case 5:
                {
                    /* Peek 16-bit value out of memory */
                    uint32_t address = (((dimm_command & CONST_DIMM_TOP_MASK) << 16) | ((*NAOMI_DIMM_OFFSETL) & 0xFFFF)) + base_address;

                    if ((address & 1) == 0)
                    {
                        if (global_peek_hook)
                        {
                            paraml = global_peek_hook(address, 2) & 0xFFFF;
                        }

                        retval = 1;
                    }
                    else
                    {
                        retval = 0;
                    }

                    break;
                }
                case 6:
                {
                    /* Peek 32-bit value out of memory */
                    uint32_t address = (((dimm_command & CONST_DIMM_TOP_MASK) << 16) | ((*NAOMI_DIMM_OFFSETL) & 0xFFFF)) + base_address;

                    if ((address & 3) == 0)
                    {
                        /* Mask off the upper bits since the net dimm sets the base address unpredictably. */
                        if ((address & 0x01FFFFFF) == (START_ADDR & 0x01FFFFFF) && _gdb_has_response())
                        {
                            uint32_t data = _gdb_handle_response();
                            paramh = (data >> 16) & 0xFFFF;
                            paraml = data & 0xFFFF;
                        }
                        else
                        {
                            if (global_peek_hook)
                            {
                                uint32_t data = global_peek_hook(address, 4);
                                paramh = (data >> 16) & 0xFFFF;
                                paraml = data & 0xFFFF;
                            }
                        }

                        retval = 1;
                    }
                    else
                    {
                        retval = 0;
                    }

                    break;
                }
                case 8:
                {
                    /* Poke 8-bit value into memory */
                    uint32_t address = (((dimm_command & CONST_DIMM_TOP_MASK) << 16) | ((*NAOMI_DIMM_OFFSETL) & 0xFFFF)) + base_address;
                    uint8_t value = (*NAOMI_DIMM_PARAMETERL) & 0xFF;
                    if (global_poke_hook)
                    {
                        global_poke_hook(address, 1, value);
                    }

                    retval = 1;
                    break;
                }
                case 9:
                {
                    /* Poke 16-bit value into memory */
                    uint32_t address = (((dimm_command & CONST_DIMM_TOP_MASK) << 16) | ((*NAOMI_DIMM_OFFSETL) & 0xFFFF)) + base_address;
                    uint16_t value = (*NAOMI_DIMM_PARAMETERL) & 0xFFFF;

                    if ((address & 1) == 0)
                    {
                        if (global_poke_hook)
                        {
                            global_poke_hook(address, 2, value);
                        }

                        retval = 1;
                    }
                    else
                    {
                        retval = 0;
                    }

                    break;
                }
                case 10:
                {
                    /* Poke 32-bit value into memory */
                    uint32_t address = (((dimm_command & CONST_DIMM_TOP_MASK) << 16) | ((*NAOMI_DIMM_OFFSETL) & 0xFFFF)) + base_address;
                    uint32_t value = (((*NAOMI_DIMM_PARAMETERH) & 0xFFFF) << 16) | ((*NAOMI_DIMM_PARAMETERL) & 0xFFFF);

                    if ((address & 3) == 0)
                    {
                        /* Must check for GDB knock address. */
                        if ((address & 0x01FFFFFF) == (START_ADDR & 0x01FFFFFF) && _gdb_check_address(value))
                        {
                            halted = _gdb_handle_command(value & 0x00FFFFFF, cur_state);
                        }
                        else
                        {
                            if (global_poke_hook)
                            {
                                global_poke_hook(address, 4, value);
                            }
                        }

                        retval = 1;
                    }
                    else
                    {
                        retval = 0;
                    }

                    break;
                }
                default:
                {
                    /* Invalid command */
                    retval = 0xFF;
                    break;
                }
            }

            // Acknowledge the command, return the response.
            *NAOMI_DIMM_COMMAND = (dimm_command & CONST_DIMM_COMMAND_MASK) | (retval & 0xFF);
            *NAOMI_DIMM_OFFSETL = offsetl;
            *NAOMI_DIMM_PARAMETERL = paraml;
            *NAOMI_DIMM_PARAMETERH = paramh;
            *NAOMI_DIMM_STATUS = *NAOMI_DIMM_STATUS | 0x100;

            do {
                /* Do a spinloop to wait for external IRQ to clear. */
            } while ((HOLLY_EXTERNAL_IRQ_STATUS & HOLLY_EXTERNAL_INTERRUPT_DIMM_COMMS) != 0);

            /* Send interrupt to the DIMM itself saying we have data. */
            *NAOMI_DIMM_STATUS = *NAOMI_DIMM_STATUS & 0xFFFE;
        }
        else {
            /* Acknowledge the command */
            *NAOMI_DIMM_STATUS = *NAOMI_DIMM_STATUS | 0x100;
            do {
                /* Do a spinloop to wait for external IRQ to clear. */
            } while ((HOLLY_EXTERNAL_IRQ_STATUS & HOLLY_EXTERNAL_INTERRUPT_DIMM_COMMS) != 0);
        }
    }

    // Whether we should halt and poll or exit the IRQ handler after exiting this function.
    return halted;
}

void _dimm_comms_init()
{
    uint32_t old_interrupts = irq_disable();

    if (check_has_dimm_inserted())
    {
        if ((HOLLY_EXTERNAL_IRQ_2_MASK & HOLLY_EXTERNAL_INTERRUPT_DIMM_COMMS) == 0)
        {
            HOLLY_EXTERNAL_IRQ_2_MASK = HOLLY_EXTERNAL_IRQ_2_MASK | HOLLY_EXTERNAL_INTERRUPT_DIMM_COMMS;
        }
    }

    irq_restore(old_interrupts);
}

void _dimm_comms_free()
{
    uint32_t old_interrupts = irq_disable();

    if ((HOLLY_EXTERNAL_IRQ_2_MASK & HOLLY_EXTERNAL_INTERRUPT_DIMM_COMMS) != 0)
    {
        HOLLY_EXTERNAL_IRQ_2_MASK = HOLLY_EXTERNAL_IRQ_2_MASK & (~HOLLY_EXTERNAL_INTERRUPT_DIMM_COMMS);
    }

    irq_restore(old_interrupts);
}

void dimm_comms_attach_hooks(peek_call_t peek_hook, poke_call_t poke_hook)
{
    uint32_t old_interrupts = irq_disable();
    global_peek_hook = peek_hook;
    global_poke_hook = poke_hook;
    irq_restore(old_interrupts);
}

void dimm_comms_detach_hooks()
{
    uint32_t old_interrupts = irq_disable();
    global_peek_hook = 0;
    global_poke_hook = 0;
    irq_restore(old_interrupts);
}

// These are hooks that implement peek/poke as actual memory address handlers.
uint32_t __address_peek_memory(unsigned int address, int size)
{
    if (size == 1)
    {
        return *((volatile uint8_t *)address);
    }
    if (size == 2)
    {
        return *((volatile uint16_t *)address);
    }
    if (size == 4)
    {
        return *((volatile uint32_t *)address);
    }
    return 0;
}

void __address_poke_memory(unsigned int address, int size, uint32_t data)
{
    if (size == 1)
    {
        *((volatile uint8_t *)address) = data & 0xFF;
    }
    if (size == 2)
    {
        *((volatile uint16_t *)address) = data & 0xFFFF;
    }
    if (size == 4)
    {
        *((volatile uint32_t *)address) = data;
    }
}

void dimm_comms_attach_default_hooks()
{
    uint32_t old_interrupts = irq_disable();
    global_peek_hook = &__address_peek_memory;
    global_poke_hook = &__address_poke_memory;
    irq_restore(old_interrupts);
}
