#include "../process/process.h"
#include "../process/thread.h"
#include "../process/pid.h"

#include "../../common.h"
#include "../../state.h"

#include "shared.h"

#include "partinit.h"
#include "partition.h"
#include "port.h"

#include <refos-util/dprintf.h>
#include "../process/process.h"
#include "deployment.h"

void spawn_system_process()
{
    int error;

    error = proc_load_direct("console_server", 252, "", PID_NULL, 
            PROCESS_PERMISSION_DEVICE_IRQ | PROCESS_PERMISSION_DEVICE_MAP |
            PROCESS_PERMISSION_DEVICE_IOPORT);
    if (error) {
        ROS_WARNING("Procserv could not start console_server.");
        assert(!"RefOS system startup error.");
    }

    error = proc_load_direct("file_server", 250, "", PID_NULL, 0x0);
    if (error) {
        ROS_WARNING("Procserv could not start file_server.");
        assert(!"RefOS system startup error.");
    }
}

seL4_CPtr get_tcb_cptr_from_pid(int pid, int tid)
{
    struct proc_pcb* pcb = pid_get_pcb(&procServ.PIDList, pid);
    struct proc_tcb* tcb = (struct proc_tcb*)cvector_get(&pcb->threads, tid);
    seL4_CPtr hello_cptr = thread_tcb_obj(tcb);
    return hello_cptr;
}

// system init function

extern uint64_t time_array[POK_CONFIG_NB_THREADS];

void test_array()
{
    for (int i = 0; i < 5; i++)
    {
        seL4_DebugPrintf("timecap %d is %d\n", i, time_array[i]);
    }
}

int system_init()
{
#ifdef CONFIG_PART_DEBUG
    seL4_DebugPrintf("\n============ System Init Start =============\n");
#endif

    // start fileserv and console serv
#ifdef CONFIG_PART_DEBUG
    seL4_DebugPrintf("\nspawning system threads...\n");
#endif

    spawn_system_process();

#ifdef CONFIG_PART_DEBUG
    seL4_DebugPrintf("...finished\n");
#endif
    
    // create partition data structs
    partition_init();
#ifdef CONFIG_PART_DEBUG
    traverse_all_parts();
#endif
    
    // create process data structs
    process_init();

#ifdef CONFIG_PART_DEBUG
    traverse_all_procs();
#endif

    // init scheduling data structs
    sched_init();

    // port init [TODO]
    port_init();

    // create scheduler thread and start scheduling
    create_timer_thread();

#ifdef CONFIG_PART_DEBUG
    seL4_DebugPrintf("\n============ System Init Finished =============\n");
#endif

    // will return to main() and start finite loop
    return 0;
}