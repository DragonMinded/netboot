#ifndef __GDB_H
#define __GDB_H

#ifdef __cplusplus
extern "C" {
#endif

// Halt processing of your program and wait for GDB to attach or recognize
// the halt, allowing you to examine your program state.
void gdb_breakpoint();

#ifdef __cplusplus
}
#endif

#endif
