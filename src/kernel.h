#ifndef KERNEL_H
#define KERNEL_H

#include "common.h"

void kernel_init(KernelConfig cfg);
void kernel_run(void);
void kernel_shutdown(void);

#endif
