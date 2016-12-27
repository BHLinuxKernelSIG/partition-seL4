#ifndef _SCHED_H
#define _SCHED_H

#include "deployment.h"
#include <stdint.h>
#include <stdio.h>
#include "../../common.h"

#define POK_GETTICK() pok_tick_counter
extern uint64_t pok_tick_counter;

extern void create_timer_thread();
extern void sched_init();
extern uint32_t pok_sched_part_rr (const uint32_t index_low, 
	                        const uint32_t index_high,
	                        const uint32_t prev_thread,
	                        const uint32_t current_thread);

typedef enum
{
   POK_SCHED_FIFO             = 0,
   POK_SCHED_RR               = 1,
   POK_SCHED_GLOBAL_TIMESLICE = 2,
   POK_SCHED_RMS              = 3,
   POK_SCHED_EDF              = 4,
   POK_SCHED_LLF              = 5,
   POK_SCHED_STATIC           = 6
} pok_sched_t;

typedef enum
{
  POK_STATE_STOPPED = 0,
  POK_STATE_RUNNABLE = 1,
  POK_STATE_WAITING = 2,
  POK_STATE_LOCK = 3,
  POK_STATE_WAIT_NEXT_ACTIVATION = 4,
  POK_STATE_DELAYED_START = 5
} pok_state_t;

extern uint32_t pok_sched_part_rr(const uint32_t ,const uint32_t,const uint32_t prev_thread,const uint32_t current_thread);

extern uint8_t current_partition;
#define POK_SCHED_CURRENT_PARTITION current_partition

extern uint32_t  current_thread;
#define POK_SCHED_CURRENT_THREAD current_thread

extern seL4_CPtr need_suspend;

extern uint64_t pok_sched_slots[POK_CONFIG_SCHEDULING_NBSLOTS];
extern uint8_t  pok_sched_slots_allocation[POK_CONFIG_SCHEDULING_NBSLOTS];

extern void pok_sched();

#endif