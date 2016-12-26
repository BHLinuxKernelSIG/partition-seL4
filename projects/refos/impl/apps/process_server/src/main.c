#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <partition.h>

#include "common.h"
#include "state.h"
#include "test/test.h"
#include "dispatchers/proc_syscall.h"
#include "dispatchers/mem_syscall.h"
#include "dispatchers/data_syscall.h"
#include "dispatchers/name_syscall.h"
#include "dispatchers/fault_handler.h"
#include "system/process/process.h"

#include "system/process/thread.h"
#include "system/memserv/dataspace.h"

#include "system/partition/partinit.h"
#include "system/partition/sched.h"
#include "system/partition/partition.h"

#include <simple/simple.h>
#include <simple-stable/simple-stable.h>

#include <sel4platsupport/timer.h>
#include <platsupport/plat/timer.h>

#include <sys_apex.h>

seL4_CPtr get_tcb_cptr_from_pid(int pid, int tid);

simple_t simple;
seL4_BootInfo *info;
vka_t vka;
vspace_t vspace;
seL4_timer_t *timer;

static void
proc_server_handle_message(struct procserv_state *s, struct procserv_msg *msg)
{
    int result;
    int label = seL4_GetMR(0);
    void *userptr = NULL;
    (void) result;

    struct proc_pcb *pcb = pid_get_pcb_from_badge(
                                    &procServ.PIDList, msg->badge);

    if (label == 0xabcd)
    {
        POK_CURRENT_THREAD.status = POK_STATE_WAIT_NEXT_ACTIVATION;
        POK_CURRENT_THREAD.remaining_time_capacity = 0;
        pok_sched ();
        return;
    }

    if (label == SYS_SET_PARTITION_MODE)
    {
        int mode = seL4_GetMR(1);

        seL4_DebugPrintf("\n[PROCSERV]%s ping here,will set mode to %d\n", 
                POK_CURRENT_THREAD.name, mode);

        pok_ret_t ret = pok_partition_set_mode
                              (current_partition, mode);
        assert(ret == POK_ERRNO_OK);
        return;
    }

    /* Attempt to dispatch to procserv syscall dispatcher. */
    if (check_dispatch_syscall(msg, &userptr) == DISPATCH_SUCCESS) {
        result = rpc_sv_proc_dispatcher(userptr, label);
        assert(result == DISPATCH_SUCCESS);
        mem_syscall_postaction();
        proc_syscall_postaction();
        return;
    }

    /* Attempt to dispatch to VM fault dispatcher. */
    if (check_dispatch_fault(msg, &userptr) == DISPATCH_SUCCESS) {
        result = dispatch_vm_fault(msg, &userptr);
        assert(result == DISPATCH_SUCCESS);
        return;
    }

    /* Attempt to dispatch to RAM dataspace syscall dispatcher. */
    if (check_dispatch_dataspace(msg, &userptr) == DISPATCH_SUCCESS) {
        result = rpc_sv_data_dispatcher(userptr, label);
        assert(result == DISPATCH_SUCCESS);
        mem_syscall_postaction();
        return;
    }

    /* Attempt to dispatch to nameserv syscall dispatcher. */
    if (check_dispatch_nameserv(msg, &userptr) == DISPATCH_SUCCESS) {
        result = rpc_sv_name_dispatcher(userptr, label);
        assert(result == DISPATCH_SUCCESS);
        return;
    }

    /* Unknown message. Block calling client indefinitely. */
    dprintf("Unknown message (badge = %d msgInfo = %d syscall = 0x%x).\n",
            msg->badge, seL4_MessageInfo_get_label(msg->message), label);
    ROS_ERROR("Process server unknown message. ¯＼(º_o)/¯");
}

static int
proc_server_loop(void)
{
    struct procserv_state *s = &procServ;
    struct procserv_msg msg = { .state = s };

    while (1) {
        dvprintf("procserv blocking for new message...\n");
        //seL4_DebugPrintf("procserv blocking for new message...\n");
        msg.message = seL4_Wait(s->endpoint.cptr, &msg.badge);
        proc_server_handle_message(s, &msg);
        s->faketime++;
    }

    return 0;
}

int main(void)
{
    SET_MUSLC_SYSCALL_TABLE;
    info = seL4_GetBootInfo();
    simple_stable_init_bootinfo(&simple, info);

    initialise(seL4_GetBootInfo(), &procServ);

    vka = procServ.vka;
    vspace = procServ.vspace;

    dprintf("======== RefOS Process Server ========\n");
    
    int error = system_init();
    if (error) {
        ROS_WARNING("Partition init error!\n");
        assert(!"Partition init error!\n");
    }

    return proc_server_loop();
}