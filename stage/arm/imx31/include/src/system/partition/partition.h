#ifndef _PARTITION_H
#define _PARTITION_H

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sel4/sel4.h>

#include "impl_types.h"
#include "sched.h"

#include "deployment.h"
#include <stdint.h>

typedef unsigned int pmem_t;
typedef unsigned int ptime_t;
typedef unsigned int mode_t;

extern void partition_init();
extern void process_init();

#ifdef CONFIG_PART_DEBUG
extern void traverse_all_parts();
extern void traverse_all_procs();
#endif

#define POK_USER_STACK_SIZE 8096

#define IDLE_THREAD POK_CONFIG_NB_THREADS
#define KERNEL_THREAD POK_CONFIG_NB_THREADS + 1 

#define POK_THREAD_DEFAULT_TIME_CAPACITY 10

#define POK_CURRENT_PARTITION part_array[POK_SCHED_CURRENT_PARTITION]
#define POK_CURRENT_THREAD    proc_array[POK_SCHED_CURRENT_THREAD]

// =================== ealier implementation ================== //

typedef enum
{
   PART_MODE_COLD_START = 1,
   PART_MODE_WARM_START = 2,
   PART_MODE_NORMAL    = 3,
   PART_MODE_IDLE      = 4,
   PART_MODE_RESTART   = 5,
   PART_MODE_STOPPED   = 6,
}pok_partition_mode_t;

struct partition
{
	//int test;
	
	// partition index
	uint8_t      index;
	// partition name
	const char    *name;
	// partition mode
	pok_partition_mode_t         mode;

	uint32_t       lock_level;
	uint32_t       start_cond;
	
	uint32_t       low;
	uint32_t       high;
	uint32_t       thread_index;

	// number of threads
	uint32_t       nthreads;

	// scheduler kind, such as RR and FIFO
	pok_sched_t    sched;
	uint32_t       (*sched_func)
	                 (
	                 	uint32_t low, 
	                    uint32_t high,
	                    uint32_t prev_thread, 
	                    uint32_t cur_thread
	                 ); 

	uint64_t        period;       //unused
    uint64_t        activation;                    
    uint32_t        prev_thread;           
    uint32_t        current_thread;        

    uint32_t             thread_main;             
    // init thread        
   	uint32_t             thread_main_entry;
   	// init thread entry (for reinit)

	//struct partition* next;
	pmem_t          memory_size;
	
};
extern struct partition part_array[POK_CONFIG_NB_PARTITIONS];

typedef enum
{
   NORMAL_START          = 0,
   PARTITION_RESTART     = 1,
   HM_MODULE_RESTART     = 2,
   HM_PARTITION_RESTART  = 3
} START_CONDITION_TYPE;

typedef enum
{
   DORMANT = 0,
   READY    = 1,
   RUNNING = 2,
   WAITING = 3
} PROCESS_STATE_TYPE;

struct process{
	pok_state_t status;

	uint8_t partition;

	uint8_t prio;
	uint8_t	base_priority;

	void * entrypoint;

	uint64_t timecap;
	uint64_t period;
	uint64_t deadline;
	uint64_t remaining_time_capacity;
	uint64_t next_activation;

	uint64_t end_time;
	uint64_t wakeup_time;

	// unused 
	struct partition * part;
	int need_suspend;
	/* the array index of part->procs */
	int index;
	/* process name */
	char *name;
	/* point to the ktcb capability */
	seL4_CPtr ktcb;
	/* address space and capability space */
	seL4_CPtr vspace, cspace;

	int periodic;
};
extern struct process proc_array[POK_CONFIG_NB_THREADS + 2];

typedef struct
{
	 uint8_t      priority;         /* Priority is from 0 to 255 */
	 void*        entry;            /* entrypoint of the thread  */
	 uint64_t     period;
	 uint64_t     deadline;
	 uint64_t     time_capacity;
	 uint32_t     stack_size;
	 pok_state_t      state;
} pok_thread_attr_t;

#define PROC_STATUS_READY 0x1; /*0001*/
#define PROC_STATUS_RUNNING 0x2; /*0002*/
#define PROC_STATUS_DORMANT 0x4; /*0004*/
#define PROC_STATUS_WAITING 0x8; /*0008*/

enum PART_TEST{
baobao = 0,
aibaobao,
lianbaobao
};

/*
void config_part(struct partition *part, 
				 struct partition *next, 
				 int id,
				 ptime_t period, 
				 ptime_t duration, 
				 int num_procs, 
				 pmem_t memory_size,
				 int low,
				 int high
				 );

void config_proc(struct process *proc, 
				 struct partition *part, 
				 int id,
				 int prio,
				 const char* name,
				 int periodic,
				 seL4_CPtr ktcb);
*/




typedef enum
{
		POK_ERRNO_OK                    =   0,
		POK_ERRNO_EINVAL                =   1,

		POK_ERRNO_UNAVAILABLE           =   2,
		POK_ERRNO_PARAM					=   3,
		POK_ERRNO_TOOMANY               =   5,
		POK_ERRNO_EPERM                 =   6,
		POK_ERRNO_EXISTS                =   7,

		POK_ERRNO_ERANGE                =   8,
		POK_ERRNO_EDOM                  =   9,
		POK_ERRNO_HUGE_VAL              =  10,

		POK_ERRNO_EFAULT                =  11,

		POK_ERRNO_THREAD                =  49,
		POK_ERRNO_THREADATTR            =  50,

		POK_ERRNO_TIME                 =  100,

		POK_ERRNO_PARTITION_ATTR        = 200,

		POK_ERRNO_PORT                 =  301,
		POK_ERRNO_NOTFOUND             =  302,
		POK_ERRNO_DIRECTION            =  303,
		POK_ERRNO_SIZE                 =  304,
		POK_ERRNO_DISCIPLINE           =  305,
		POK_ERRNO_PORTPART             =  307,
		POK_ERRNO_EMPTY                =  308,
		POK_ERRNO_KIND                 =  309,
		POK_ERRNO_FULL                 =  311,
		POK_ERRNO_READY                =  310,
		POK_ERRNO_TIMEOUT              =  250,
		POK_ERRNO_MODE                 =  251,

		POK_ERRNO_LOCKOBJ_UNAVAILABLE  =  500,
		POK_ERRNO_LOCKOBJ_NOTREADY     =  501,
		POK_ERRNO_LOCKOBJ_KIND         =  502,
		POK_ERRNO_LOCKOBJ_POLICY       =  503,

		POK_ERRNO_PARTITION_MODE       =  601,

		POK_ERRNO_PARTITION            =  401
} pok_ret_t;

extern pok_ret_t pok_partition_thread_create (uint32_t* thread_id,
                                       const pok_thread_attr_t* attr,
                                       const uint8_t partition_id);

#endif