#include "sched.h"
#include "../../state.h"
#include <sel4utils/vspace.h>
#include <sel4utils/mapping.h>
#include <stdio.h>
#include <sel4platsupport/timer.h>
#include <platsupport/plat/timer.h>

#include <simple/simple.h>
#include <simple-stable/simple-stable.h>

#include "partition.h"
#include "sched.h"
#include "deployment.h"

static uint64_t thread_2_stack[0x1000];
#define IPCBUF_GDT_SELECTOR ((IPCBUF_GDT_ENTRY << 3) | 3)

#define IPCBUF_FRAME_SIZE_BITS 12
#define IPCBUF_VADDR 0x7000000

extern seL4_timer_t* timer;
extern simple_t simple;

uint8_t current_partition;
uint64_t pok_tick_counter = 0;

seL4_CPtr need_suspend = 0;


uint64_t pok_sched_slots[POK_CONFIG_SCHEDULING_NBSLOTS] = 
    POK_CONFIG_SCHEDULING_SLOTS;

uint8_t pok_sched_slots_allocation[POK_CONFIG_SCHEDULING_NBSLOTS] = 
    POK_CONFIG_SCHEDULING_SLOTS_ALLOCATION;


pok_sched_t pok_global_sched;

uint64_t pok_sched_next_deadline;
uint64_t pok_sched_next_major_frame;
uint8_t  pok_sched_current_slot = 0;
uint32_t current_thread = IDLE_THREAD;

void pok_sched_context_switch(uint32_t last, uint32_t new)
{
    if (last != new)
    {
        /*
        seL4_DebugPrintf("\n[switch] will change thread from %d to %d!\n",
                             last, new);
        */
        if (proc_array[last].ktcb)
        {
            //seL4_DebugPrintf("[switch] will suspend %d\n", last);
            seL4_TCB_Suspend(proc_array[last].ktcb);
        }
        if (proc_array[new].ktcb)
        {
            //seL4_DebugPrintf("[switch] will resume %d\n", new);
            seL4_TCB_Resume(proc_array[new].ktcb);
        }
    }
    return;
}

uint8_t pok_elect_partition()
{
  uint8_t next_partition = POK_SCHED_CURRENT_PARTITION;
  uint64_t now = POK_GETTICK();

  if (pok_sched_next_deadline <= now)
  {
        seL4_DebugPrintf("\n[sched] Will change partition!\n");
        if (pok_sched_next_major_frame <= now)
        {
            //seL4_DebugPrintf("[sched] Will go to next major frame!\n");
            pok_sched_next_major_frame = 
            pok_sched_next_major_frame + POK_CONFIG_SCHEDULING_MAJOR_FRAME;
            //pok_port_flushall();
        }

    pok_sched_current_slot = (pok_sched_current_slot + 1) % 
                              POK_CONFIG_SCHEDULING_NBSLOTS;
    pok_sched_next_deadline = pok_sched_next_deadline + 
                              pok_sched_slots[pok_sched_current_slot];

    next_partition = pok_sched_slots_allocation[pok_sched_current_slot];

  }
  return next_partition;
}

uint32_t pok_elect_thread(uint8_t new_partition_id)
{
   uint64_t now = POK_GETTICK();
   struct partition* new_partition = &(part_array[new_partition_id]);

   uint8_t i;

   struct process* thread;
   for (i = 0; i < new_partition->nthreads; i++)
   {
      thread = &(proc_array[new_partition->low + i]);

      if ((thread->status == POK_STATE_WAITING) 
                     && (thread->wakeup_time <= now))
      {
          thread->status = POK_STATE_RUNNABLE;
      }

      if ((thread->status == POK_STATE_WAIT_NEXT_ACTIVATION) 
                           && (thread->next_activation <= now))
      {
        /*
          seL4_DebugPrintf("[elect thread] now = %lld, next = %lld\n",
                          now, thread->next_activation);
        */
          thread->status = POK_STATE_RUNNABLE;
          thread->remaining_time_capacity =  thread->timecap;
          //thread->next_activation += thread->period; 
          thread->next_activation += thread->period; 
      }
   }

   uint32_t elected;
   switch (new_partition->mode)
   {
      case PART_MODE_WARM_START:
      case PART_MODE_COLD_START:
         elected = new_partition->thread_main;
         break;

      case PART_MODE_NORMAL:
      {
         if ((POK_SCHED_CURRENT_THREAD != IDLE_THREAD)
         && (POK_SCHED_CURRENT_THREAD != POK_CURRENT_PARTITION.thread_main))
         {
            if (POK_CURRENT_THREAD.remaining_time_capacity > 0)
            {
               POK_CURRENT_THREAD.remaining_time_capacity = 
                         POK_CURRENT_THREAD.remaining_time_capacity - 1;

                /*
               seL4_DebugPrintf("\n[elect_thread] remaining time is %lld.\n",
                                  POK_CURRENT_THREAD.remaining_time_capacity);
                */

               if (POK_CURRENT_THREAD.remaining_time_capacity <= 0)
               {
                    POK_CURRENT_THREAD.status = POK_STATE_WAIT_NEXT_ACTIVATION;
               }
            }
            else
            {
               POK_CURRENT_THREAD.status = POK_STATE_WAIT_NEXT_ACTIVATION;
            }
         }
         elected = new_partition->sched_func(
                             new_partition->low,
                             new_partition->high,
                             new_partition->prev_thread,
                             new_partition->current_thread
                            );
         break;
      }
      
      default:
         seL4_DebugPrintf("[elect_thread] will return IDLE_THREAD");
         elected = IDLE_THREAD;
         break;
   }

   proc_array[elected].end_time = now + 
                       proc_array[elected].remaining_time_capacity;

   return (elected);
}


void pok_sched()
{
    uint32_t last_thread = current_thread;
    uint32_t elected_thread;
    uint8_t elected_partition = POK_SCHED_CURRENT_PARTITION;

    //seL4_DebugPrintf("\n[sched] scheduling entry\n");
  
    elected_partition = pok_elect_partition();
    elected_thread = pok_elect_thread(elected_partition);

    
    //seL4_DebugPrintf("\n[sched] schedule finish: part %d thread %d\n",
    //                        elected_partition, elected_thread);
    

    current_thread = elected_thread;
    current_partition = elected_partition;

    if(part_array[current_partition].current_thread != elected_thread) 
    {
       if(part_array[current_partition].current_thread != IDLE_THREAD) 
       {
           part_array[current_partition].prev_thread = 
             part_array[current_partition].current_thread;
       }
       part_array[current_partition].current_thread = elected_thread;
    }
    pok_sched_context_switch(last_thread, elected_thread);
}



uint32_t pok_sched_part_rr(
            const uint32_t index_low, 
            const uint32_t index_high,
            const uint32_t prev_thread,
            const uint32_t current_thread)
{
   uint32_t res;
   uint32_t from;

   if (current_thread == IDLE_THREAD)
   {
      res = prev_thread;
   }
   else
   {
      res = current_thread;
   }

   from = res;

   if ((proc_array[current_thread].remaining_time_capacity > 0) && (proc_array[current_thread].status == POK_STATE_RUNNABLE))
   {
        return current_thread;
   }

   do
   {
      res++;
      if (res > index_high)
      {
         res = index_low;
      }
   }
   while ((res != from) && (proc_array[res].status != POK_STATE_RUNNABLE));

   if ((res == from) && (proc_array[res].status != POK_STATE_RUNNABLE))
   {
      res = IDLE_THREAD;
   }
   return res;
}


void sched_init()
{
    uint64_t   total_time;
    uint8_t    slot;

    total_time = 0;

    for (slot = 0 ; slot < POK_CONFIG_SCHEDULING_NBSLOTS ; slot++)
    {
        total_time = total_time + pok_sched_slots[slot];
    }

    if (total_time != POK_CONFIG_SCHEDULING_MAJOR_FRAME)
    {
        seL4_DebugPrintf("Sched config error!\n");
        while(1);
    }

    pok_sched_current_slot = 0;
    pok_sched_next_major_frame = POK_CONFIG_SCHEDULING_MAJOR_FRAME;
    pok_sched_next_deadline    = pok_sched_slots[0];
    current_partition          = pok_sched_slots_allocation[0];

    // TODO: no main thread now
    //current_thread             = part_array[current_partition].low;

    return ;
}

static void suspend_all()
{
	seL4_DebugPrintf("In suspend all\n");
	for (int i = 0 ; i < POK_CONFIG_NB_THREADS ; i++)
	{
		seL4_TCB_Suspend(proc_array[i].ktcb);
	}
}

void scheduler_thread()
{
	seL4_DebugPrintf("hi I am scheduler thread\n");
	//while(1);
	vka_object_t ep;
	vka_object_t aep;

	int error;

    error = vka_alloc_endpoint(&procServ.vka, &ep);
    assert(error == 0 && ep.cptr);

    error = vka_alloc_async_endpoint(&procServ.vka, &aep);
    assert(error == 0 && aep.cptr);

    seL4_timer_t *timer;
    timer = sel4platsupport_get_default_timer(&procServ.vka, 
    										  &procServ.vspace, 
    										  &simple, 
    										  aep.cptr);

    seL4_Word sender;
    current_partition = 0;

    suspend_all();
    while(1) {
        timer_oneshot_relative(timer->timer, 1000 * 1000);
        seL4_Wait(aep.cptr, &sender);
	    sel4_timer_handle_single_irq(timer);
        pok_tick_counter += 1;
        /*
        if (need_suspend > 0){
        	seL4_TCB_Suspend(need_suspend);
        	need_suspend = 0;
        }
        */
        //seL4_DebugPrintf("\n[pok_sched] scheduler start\n");
        pok_sched();
    }

}

void create_timer_thread()
{
	int error;

	vka_object_t tcb_object = {0};
    error = vka_alloc_tcb(&procServ.vka, &tcb_object);

    vka_object_t ipc_frame_object1 = {0};
    error = vka_alloc_frame(&procServ.vka, 
    	                    IPCBUF_FRAME_SIZE_BITS, 
    	                    &ipc_frame_object1);
    
    seL4_Word ipc_buffer_vaddr = IPCBUF_VADDR;

    error = seL4_ARCH_Page_Map(ipc_frame_object1.cptr, 
    							0x3, 
    							ipc_buffer_vaddr,
       							seL4_AllRights, 
       							0x3);

    if (error != 0) {
        vka_object_t pt_object = {0};
        error =  vka_alloc_page_table(&procServ.vka, &pt_object);
        assert(error == 0);

    	error = seL4_ARCH_PageTable_Map(pt_object.cptr, 0x3,
            ipc_buffer_vaddr, 0x3);
        assert(error == 0);

        error = seL4_ARCH_Page_Map(ipc_frame_object1.cptr, 0x3,
            ipc_buffer_vaddr, seL4_AllRights, 0x3);
        assert(error == 0);
    }

    seL4_IPCBuffer *ipcbuf = (seL4_IPCBuffer*)ipc_buffer_vaddr;
    ipcbuf->userData = ipc_buffer_vaddr;

    error = seL4_TCB_Configure(tcb_object.cptr, 
    							seL4_CapNull, 
    							246,    
    							2, 
    							seL4_NilData, 
    							3, 
    							seL4_NilData,
    							ipc_buffer_vaddr, 
    							ipc_frame_object1.cptr
    						);
    assert(error == 0);

    seL4_UserContext regs = {0};
    size_t regs_size = sizeof(seL4_UserContext) / sizeof(seL4_Word);

    /* set instruction pointer where the thread shoud start running */
    sel4utils_set_instruction_pointer(&regs, (seL4_Word)scheduler_thread);

    /* check that stack is aligned correctly */
    uintptr_t thread_2_stack_top = (uintptr_t)thread_2_stack + sizeof(thread_2_stack);
    assert(thread_2_stack_top % (sizeof(seL4_Word) * 2) == 0);

    /* set stack pointer for the new thread. remember the stack grows down */
    sel4utils_set_stack_pointer(&regs, thread_2_stack_top);

    //regs.gs = IPCBUF_GDT_SELECTOR;

    /* actually write the TCB registers. */
    error = seL4_TCB_WriteRegisters(tcb_object.cptr, 0, 0, regs_size, &regs);
    assert(error == 0);

    /* start the new thread running */
    error = seL4_TCB_Resume(tcb_object.cptr);
    assert(error == 0);

    /* we are done, say hello */
    seL4_DebugPrintf("scheduler created!\n");

}