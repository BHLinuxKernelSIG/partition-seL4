#ifndef _PARTITION_H
#define _PARTITION_H

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sel4/sel4.h>

#include "impl_types.h"
#include "sched.h"

#include "deployment.h"

// =================== ealier implementation ================== //

typedef unsigned long ptime_t;
typedef unsigned int pmem_t;

struct partition{
	int ID;
	int mode;
	ptime_t period;
	ptime_t duration;
	
	/*point array, pointing to struct process*/
	/*assume 32 processes at most in a partition*/
	struct process * procs[6];
	int num_of_process;

	struct partition* next;

	pmem_t memory_size;
};


#define PART_MODE_CODE_START 0x1; /*0001*/
#define PART_MODE_WARM_START 0x2; /*0002*/
#define PART_MODE_NORMAL 0x4; /*0004*/
#define PART_MODE_IDLE 0x8; /*0008*/

struct process{
	/* the array index of part->procs */
	int index;
	/* process name */
	char name[32];
	/* point to the ktcb capability */
	seL4_CPtr ktcb;
	/* address space and capability space */
	seL4_CPtr vspace, cspace;
	int periodic;
	int status;
	struct partition * part;
	int prio;
	/* points to user-level scheduled context */
	struct u_sc *u_sc;
};

struct u_sc{
	seL4_CPtr k_sc;
	struct process *proc;
	ptime_t budget;
	ptime_t period;
};

#define PROC_STATUS_READY 0x1; /*0001*/
#define PROC_STATUS_RUNNING 0x2; /*0002*/
#define PROC_STATUS_DORMANT 0x4; /*0004*/
#define PROC_STATUS_WAITING 0x8; /*0008*/

enum PART_TEST{
baobao = 0,
aibaobao,
lianbaobao
};

void config_part(struct partition *part, 
				 struct partition *next, 
				 int id,
				 ptime_t period, 
				 ptime_t duration, 
				 int num_of_proc, 
				 pmem_t memory_size);

void config_proc(struct process *proc, 
				 struct partition *part, 
				 int id,
				 int prio,
				 const char* name,
				 int periodic,
				 seL4_CPtr ktcb);

void traverse_all_parts(struct partition **part, struct process**proc, int num);

#endif