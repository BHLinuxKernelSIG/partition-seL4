#ifndef _TIMER_INIT_H
#define _TIMER_INIT_H

#include <stdio.h>
#include <assert.h>
#include <refos/refos.h>
#include <refos-util/init.h>
#include <refos-io/morecore.h>
#include "../state.h"

#include "partition.h"

seL4_CPtr A_cptr;
seL4_CPtr B_cptr;
seL4_CPtr C_cptr;
seL4_CPtr D_cptr;
seL4_CPtr E_cptr;

void part_init();

struct partition* part_array[2];
struct process *proc_array[5];

#endif