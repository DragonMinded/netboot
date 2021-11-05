#ifndef __IRQINTERNAL_H
#define __IRQINTERNAL_H

#include <stdint.h>

void _irq_display_invariant(char *msg, char *failure, ...);

int _irq_was_disabled(uint32_t sr);

#endif
