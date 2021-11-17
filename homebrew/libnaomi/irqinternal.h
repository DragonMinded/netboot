#ifndef __IRQINTERNAL_H
#define __IRQINTERNAL_H

#include <stdint.h>

void _irq_display_invariant(char *msg, char *failure, ...);

uint32_t _irq_get_sr();

int _irq_is_disabled(uint32_t sr);

#endif
