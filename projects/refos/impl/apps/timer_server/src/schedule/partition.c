#include "partition.h"
#include <refos-util/dprintf.h>

//==============config process or partition function==============//

void config_part(struct partition *part, 
				 struct partition *next, 
				 int id,
				 ptime_t period, 
				 ptime_t duration, 
				 int num_of_proc, 
				 pmem_t memory_size)
{
	part->ID = id;
	part->next = next;
	part->period = period;
	part->duration = duration;
	part->num_of_process = num_of_proc;
	part->memory_size = memory_size;
}

void config_proc(struct process *proc, 
				 struct partition *part, 
				 int id,
				 int prio,
				 const char* name,
				 int periodic,
				 seL4_CPtr ktcb)
{
	proc->part = part;
	proc->index = id;
	proc->prio = prio;

	//seL4_DebugPrintf("	Before strcpy\n");

	strcpy(proc->name, name);

	//seL4_DebugPrintf("	After strcpy, name is %s\n", proc->name);

	proc->periodic = periodic;
	proc->ktcb = ktcb;
}

//====================DEBUG FUNCTIONS========================//

static void visit_part(struct partition *part)
{
	seL4_DebugPrintf("This is part ID %d\n", part->ID);
	seL4_DebugPrintf("	I have %d procs\n", part->num_of_process);
}

static void visit_proc(struct process *proc)
{
	seL4_DebugPrintf("This is proc ID %d\n", proc->index);
	seL4_DebugPrintf("	I belong to part %d\n", proc->part->ID);
}

void traverse_all_parts(struct partition **part, struct process**proc, int num)
{
	assert(num == 2);

	visit_part(part[0]);
	visit_part(part[0]->next);

	visit_proc(part[0]->procs[0]);
	visit_proc(part[0]->procs[1]);

	return ;
}