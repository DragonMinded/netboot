#ifndef __DIMMCOMMS_H
#define __DIMMCOMMS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef uint32_t (*peek_call_t)(unsigned int address, int size);
typedef void (*poke_call_t)(unsigned int address, int size, uint32_t data);

// If you want to handle peek and poke events in your code, you
// can register handlers for such events. Normally, the Naomi
// BIOS would handle peek and poke by actually reading/writing
// direct memory addresses. However, for homebrew it seems more
// convenient to have these be RPC calls.
void dimm_comms_attach_hooks(peek_call_t peek_hook, poke_call_t poke_hook);

// If you so desire, you can implement the original peek/poke by
// calling this handler instead of the above ones.
void dimm_comms_attach_default_hooks();

// You can call this as a convenience method to unhook any previous
// code.
void dimm_comms_detach_hooks();

#ifdef __cplusplus
}
#endif

#endif
