#ifndef _SCHEDULE_H
#define _SCHEDULE_H

#include <stdio.h>
#include <assert.h>
#include <refos/refos.h>
#include <refos-util/init.h>
#include <refos-io/morecore.h>
#include "../state.h"

#include <partition.h>

extern seL4_CPtr A_cptr;
extern seL4_CPtr B_cptr;
extern seL4_CPtr C_cptr;
extern seL4_CPtr D_cptr;
extern seL4_CPtr E_cptr;

void schedule(int i);
void schedule_entry(void);

#endif