#ifndef __CONSOLE_H
#define __CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// If you wish for a debug console to be displayed on top of your screen drawing
// operations, call console_init(). This will hook printf() to show up on the screen.
// Calling console_free() will unhook and disable the console. Note that you must
// call console_init() after calling video_init()! Also, if you reinitialize the video
// library at any time you should call console_free() followed by console_init() again.
void console_init(unsigned int overscan);
void console_free();

// Render the console. This is called for you automatically in video_wait_for_vblank().
// So you do not need to handle calling it.
void console_render();

// Show or hide an initialized console.
void console_set_visible(unsigned int visible);

// Save or restore the current console position. You should only give console_restore()
// a pointer that was retrieved from console_save() previously.
char * console_save();
void console_restore(char *);
#ifdef __cplusplus
}
#endif

#endif
