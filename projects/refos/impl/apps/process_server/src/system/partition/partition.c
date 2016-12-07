#include "partition.h"
#include <refos-util/dprintf.h>
#include "../process/process.h"
#include "../process/thread.h"
#include "deployment.h"
#include "../../state.h"
#include "../../common.h"
#include <assert.h>

#include "sched.h"
#include "stdio.h"

struct partition part_array[POK_CONFIG_NB_PARTITIONS];
struct process proc_array[POK_CONFIG_NB_THREADS + 2];

char *names[POK_CONFIG_NB_THREADS] = POK_CONFIG_THREADS_NANME;
uint8_t prio_array[POK_CONFIG_NB_THREADS] = POK_CONFIG_THREAD_PRIO;
uint64_t period_array[POK_CONFIG_NB_THREADS] = POK_CONFIG_THREAD_PERIOD;
uint64_t time_array[POK_CONFIG_NB_THREADS] = POK_CONFIG_THREAD_TIME;
uint32_t main_array[POK_CONFIG_NB_PARTITIONS] = {0,2};

char *main_name[POK_CONFIG_NB_PARTITIONS] = CONFIG_MAIN_THREADS_NANE;

void partition_setup_scheduler (const uint8_t pid)
{
	  assert(pok_sched_part_rr);
      part_array[pid].sched_func = pok_sched_part_rr;
      return;
}

void pok_arch_idle (void)
{
   while (1)
   {
      asm ("nop");
   }
}

static void init_idle_thread()
{
	proc_array[IDLE_THREAD].period                     = 0;
    proc_array[IDLE_THREAD].deadline                   = 0;
    proc_array[IDLE_THREAD].timecap                    = 0;
    proc_array[IDLE_THREAD].next_activation            = 0;
    proc_array[IDLE_THREAD].remaining_time_capacity    = 0;
    proc_array[IDLE_THREAD].wakeup_time		           = 0;
    proc_array[IDLE_THREAD].entrypoint		   = pok_arch_idle;
    proc_array[IDLE_THREAD].base_priority  = 0;
    proc_array[IDLE_THREAD].status		   = POK_STATE_RUNNABLE;
    proc_array[IDLE_THREAD].ktcb 		   = 0;
    proc_array[IDLE_THREAD].name           = "IDLE_THREAD";
}

static void init_kernel_thread()
{
   proc_array[KERNEL_THREAD].prio	         = 0;
   proc_array[KERNEL_THREAD].base_priority	 = 0;
   proc_array[KERNEL_THREAD].status   	     = POK_STATE_RUNNABLE;
   proc_array[KERNEL_THREAD].next_activation = 0;
}

void setup_main_thread(int id, int part_id)
{
   proc_array[id].prio = 1;                 //prio_array[id];
   proc_array[id].base_priority =  1;       //    prio_array[id];

   //proc_array[id].period          = period_array[id];
   //proc_array[id].next_activation = period_array[id];

   proc_array[id].period = 0;
   proc_array[id].next_activation = 0;

   proc_array[id].timecap = 0;   //time_array[id];
   proc_array[id].remaining_time_capacity = 0;   //time_array[id];

   proc_array[id].status = POK_STATE_RUNNABLE;
   proc_array[id].wakeup_time   = 0;

   proc_array[id].partition = part_id;

   assert(main_name[part_id]);

   int error = proc_load_direct("selfloader", 
      							  proc_array[id].prio, 
      							  //"fileserv/hello_world", 
      							  main_name[part_id],
      							  PID_NULL,
              PROCESS_PERMISSION_DEVICE_IRQ | PROCESS_PERMISSION_DEVICE_MAP |
              PROCESS_PERMISSION_DEVICE_IOPORT);
   if (error) {
        	assert(!"RefOS system startup error.");
   }
}

void setup_main_thread_ktcb(uint32_t proc_id, uint8_t part_id)
{
	proc_array[proc_id].ktcb = 
	    get_tcb_cptr_from_pid(part_id + CONFIG_PCB_OFFSET, 0);
}

void partition_init()
{
	int threads_index = 0;

	for (int i = 0; i < POK_CONFIG_NB_PARTITIONS ; i++)
	{
		part_array[i].index = 
				((uint32_t[]) POK_CONFIG_PARTITIONS_NTHREADS) [i];

		part_array[i].nthreads = 
				((uint32_t[]) POK_CONFIG_PARTITIONS_NTHREADS) [i];

		partition_setup_scheduler(i);

		part_array[i].lock_level = 0;
		
		// Part threads info
		part_array[i].low = threads_index;
		part_array[i].high = part_array[i].low + 
						     part_array[i].nthreads - 1;

		part_array[i].thread_index = 1;

		// Scheduling information
		part_array[i].sched = 
		        ((pok_sched_t[]) POK_CONFIG_PARTITIONS_SCHEDULER)[i];

		part_array[i].activation        = 0;
      	part_array[i].period            = 0;

      	part_array[i].current_thread    = IDLE_THREAD;
        part_array[i].prev_thread       = IDLE_THREAD;

      	// Part init info
      	part_array[i].mode              = PART_MODE_WARM_START;
      	part_array[i].start_cond = NORMAL_START;

      	part_array[i].thread_main       = main_array[i];
      	part_array[i].thread_main_entry = 0; 

      	threads_index = part_array[i].high + 1;

        part_array[i].current_thread = part_array[i].thread_main;

        uint32_t mid = main_array[i];

        setup_main_thread(mid, i);
        setup_main_thread_ktcb(mid, i);

        assert(proc_array[mid].ktcb);
        seL4_TCB_Suspend(proc_array[mid].ktcb);
    }
    return;
}


pok_ret_t pok_partition_thread_create (uint32_t* thread_id,
                                       const pok_thread_attr_t* attr,
                                       const uint8_t  partition_id)
{
   uint32_t id;
   uint32_t stack_vaddr;
   /**
    * We can create a thread only if the partition is in INIT mode
    */
   if ((part_array[partition_id].mode != PART_MODE_COLD_START) &&
       (part_array[partition_id].mode != PART_MODE_WARM_START))
   {
      return POK_ERRNO_MODE;
   }

   if (part_array[partition_id].thread_index > part_array[partition_id].high)
   {
      return POK_ERRNO_TOOMANY;
   }

   id = part_array[partition_id].low + 
        part_array[partition_id].thread_index;

   part_array[partition_id].thread_index += 1;


    //proc_array[id].prio      = attr->priority;
    //proc_array[id].base_priority      = attr->priority;

   proc_array[id].prio = prio_array[id];
   proc_array[id].base_priority = prio_array[id];

   /*
   if (attr->period > 0)
   {
      proc_array[id].period          = attr->period;
      proc_array[id].next_activation = attr->period;
   }
   */

   proc_array[id].period          = period_array[id];
   proc_array[id].next_activation = period_array[id];

/*
   if (attr->time_capacity > 0)
   {
      proc_array[id].timecap = attr->time_capacity;
      proc_array[id].remaining_time_capacity = attr->time_capacity;
   }
   else
   {
      proc_array[id].remaining_time_capacity   = POK_THREAD_DEFAULT_TIME_CAPACITY;
      proc_array[id].timecap   = POK_THREAD_DEFAULT_TIME_CAPACITY;
   }
 */

   proc_array[id].timecap = time_array[id];
   proc_array[id].remaining_time_capacity = time_array[id];

   proc_array[id].status = POK_STATE_RUNNABLE;
   proc_array[id].wakeup_time = 0;
   proc_array[id].partition = current_partition;
   
   *thread_id = id;

   proc_array[id].ktcb = get_tcb_cptr_from_pid(
   	             current_partition + CONFIG_PCB_OFFSET, 
   	             id - part_array[current_partition].low
   	            );
   
   assert(proc_array[id].ktcb > 0);
   seL4_TCB_Suspend(proc_array[id].ktcb);

   return POK_ERRNO_OK;
}

extern void test_array();

void process_init()
{
	init_idle_thread();
	init_kernel_thread();
	
	seL4_DebugPrintf("\n[procinit] start...\n");
	

	int error;
	for (int i = 0; i < POK_CONFIG_NB_THREADS; ++i)
   	{
   		proc_array[i].name = names[i];
   		proc_array[i].index  = i;

      	proc_array[i].status = POK_STATE_STOPPED;

      	proc_array[i].prio = prio_array[i];
      	proc_array[i].base_priority = prio_array[i];

      	proc_array[i].entrypoint = NULL;
      	
      	proc_array[i].deadline = 0;
		proc_array[i].period = (uint64_t)period_array[i];
		proc_array[i].timecap = ((uint64_t[])POK_CONFIG_THREAD_TIME)[i];	
      	proc_array[i].remaining_time_capacity = (uint64_t)time_array[i];
      	                                
      	proc_array[i].next_activation = proc_array[i].period;

      	proc_array[i].end_time = 0;
      	proc_array[i].wakeup_time = 0;
  	}

  	//proc_array[0].status = POK_STATE_RUNNABLE;
  	//proc_array[2].status = POK_STATE_RUNNABLE;
    return;
}

pok_ret_t pok_partition_set_mode
				(const uint8_t pid, 
				 const pok_partition_mode_t mode)
{
   switch (mode)
   {
      case PART_MODE_NORMAL:
         if (POK_SCHED_CURRENT_THREAD != POK_CURRENT_PARTITION.thread_main)
         {
            return POK_ERRNO_PARTITION_MODE;
         }

         part_array[pid].mode = mode;  /* Here, we change the mode */

	 struct process* thread;
	 unsigned int i;
	 for (i = 0; i < part_array[pid].nthreads; i++)
	 {
		 thread = &(proc_array[POK_CURRENT_PARTITION.low + i]);
		 if ((long long)thread->period == -1) {//-1 <==> ARINC INFINITE_TIME_VALUE
			 if(thread->status == POK_STATE_DELAYED_START) { // delayed start, the delay is in the wakeup time
				 if(!thread->wakeup_time) {
					 thread->status = POK_STATE_RUNNABLE;
				 } else {
					 thread->status = POK_STATE_WAITING;
				 }
				 thread->wakeup_time += POK_GETTICK();
				 thread->end_time =  thread->wakeup_time + thread->timecap;
			 }
		 } 
		 else 
		 {
			 if(thread->status == POK_STATE_DELAYED_START) 
			 {
				 thread->next_activation = thread->wakeup_time + POK_CONFIG_SCHEDULING_MAJOR_FRAME + POK_CURRENT_PARTITION.activation;
				 thread->end_time =  thread->next_activation + thread->timecap;
				 thread->status = POK_STATE_WAIT_NEXT_ACTIVATION;
			 }
		 }
	 }
         //pok_sched_stop_thread(part_array[pid].thread_main);
	 proc_array[part_array[pid].thread_main].status = POK_STATE_STOPPED;
         pok_sched ();
         break;

      default:
         return POK_ERRNO_PARTITION_MODE;
         break;
   }
   return POK_ERRNO_OK;
}


//====================DEBUG FUNCTIONS========================//

#ifdef CONFIG_PART_DEBUG

static void visit_part(struct partition *part)
{
	seL4_DebugPrintf("This is partition ID %d\n", 
		part->index);
	seL4_DebugPrintf("	I have %d procs\n", part->nthreads);
	seL4_DebugPrintf("	from %d to %d.\n", part->low, part->high);
}

static void visit_proc(struct process *proc)
{
	seL4_DebugPrintf("This is proc ID %d\n", proc->index);
	seL4_DebugPrintf("	My ktcb is %d.\n", proc->ktcb);
	seL4_DebugPrintf("	period is %lld.\n", proc->period);
	seL4_DebugPrintf("	timecap is %lld.\n", proc->timecap);
	seL4_DebugPrintf("	remain is %lld.\n", proc->remaining_time_capacity);
	seL4_DebugPrintf("	prio is %hd.\n", proc->prio);
}

void traverse_all_parts()
{
	seL4_DebugPrintf("\n============= PARTS TRAVERSE ==============\n");
	for (int i = 0 ; i < POK_CONFIG_NB_PARTITIONS ; i++)
	{
		visit_part(&part_array[i]);
	}
	seL4_DebugPrintf("\n============= TRAVERSE END ==============\n");
}

void traverse_all_procs()
{	
	seL4_DebugPrintf("============= PROCS TRAVERSE ==============\n");
	for (int i = 0 ; i < POK_CONFIG_NB_THREADS ; i++)
	{
		visit_proc(&proc_array[i]);
	}
	seL4_DebugPrintf("============= TRAVERSE END ==============\n");
	return ;
}

#endif