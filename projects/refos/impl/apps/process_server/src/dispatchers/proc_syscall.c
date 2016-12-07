/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#include "proc_syscall.h"

#include <vka/kobject_t.h>
#include <refos-rpc/proc_common.h>
#include <refos-rpc/proc_server.h>

#include "../system/process/pid.h"
#include "../system/process/process.h"
#include "../system/process/thread.h"
#include "../system/process/proc_client_watch.h"
#include "../system/addrspace/vspace.h"
#include "../system/memserv/window.h"
#include "../system/memserv/dataspace.h"
#include "../system/memserv/ringbuffer.h"

#include "../system/partition/partition.h"
#include "../system/partition/port.h"
#include "../system/partition/partinit.h"
#include "../system/partition/sched.h"

#include <stdint.h>

/*! @file
    @brief Dispatcher for the procserv syscalls.

    Handles calls to process server syscall interface. The process server interface provides process
    abstraction, simple naming, server registration, memory windows...etc. (For more details, refer
    to the protocol design document.). The methods here implement the functions in the generated
    header file <refos-rpc/proc_server.h>.

    The memory related process server syscalls resides in mem_syscall.c and mem_syscall.h.
*/

/* ---------------------------- Proc Server syscall helper functions ---------------------------- */

/*! @brief Creates a kernel endpoint object for the given process.

    Creates a kernel endpoint object for the given process. Also does the book-keeping underneath so
    that the created objects can be destroyed when the process exits.

    @param pcb Handle to the process to create the object for. (no ownership)
    @param type The kernel endpoint object type to create for the process; sync or async.
    @return Handle to the created object if successful (no ownership), NULL if there was an
            error. (eg. ran out of heap or untyped memory).
 */

// ==================== sampling interface ===============//

int proc_port_sampling_create_handler(void *user, 
                                    char *name,
                                    int size, 
                                    int core, 
                                    int period, 
                                    int *id)
{
    struct proc_pcb* pcb = (struct proc_pcb*)user;
    struct partition *part = pcb->proc->part;

    *id = 0;
    int gid = *id;

    //pok_ports[gid].index      = pok_queue.size - pok_queue.available_size;
    pok_ports[gid].off_b      = 0;
    pok_ports[gid].off_e      = 0;
    pok_ports[gid].size       = size;
    pok_ports[gid].full       = FALSE;
    //pok_ports[gid].partition  = pok_current_partition;
    pok_ports[gid].partition  = part;
    pok_ports[gid].direction  = core;
    pok_ports[gid].ready      = TRUE;
    pok_ports[gid].kind       = POK_PORT_KIND_SAMPLING;

    //pok_queue.available_size  = pok_queue.available_size - size;
    pok_ports[gid].refresh        = period;
    pok_ports[gid].last_receive   = 0;

    pok_ports[gid].data = malloc(size);

    procServ.shared_dspace = ram_dspace_create(&procServ.dspaceList, size);

    //caps[gid] = procServ.shared_dspace->pages[0].cptr;

    return 0;
}

int proc_port_sampling_write_handler(void *user, int id, void* addr, int len)
{
    int error;
    struct proc_pcb* pcb = (struct proc_pcb*)user;

    pok_ports[id].must_be_flushed = TRUE;

    vspace_t *vspace = &pcb->vspace.vspace;
    seL4_CPtr cap = vspace_get_cap(vspace, addr);

    cspacepath_t loadee_frame_cap;
    cspacepath_t loader_frame_cap;

    vka_cspace_make_path(&procServ.vka, cap, &loadee_frame_cap);

    error = vka_cnode_copy(&loader_frame_cap, &loadee_frame_cap, seL4_AllRights);
    assert(error == 0);

    char *loader_vaddr = vspace_map_pages(&procServ.vspace, 
                                        &loader_frame_cap.capPtr, 
                                        NULL, 
                                        seL4_AllRights,
                                        1, 
                                        seL4_PageBits, 
                                        1
                            );

    if (len > pok_ports[id].size)
    {
        return POK_ERRNO_SIZE;
    }

    memcpy(pok_ports[id].data, loader_vaddr, len);

    pok_ports[id].empty = FALSE;
    //pok_ports[pid].last_receive = POK_GETTICK ();

    return POK_ERRNO_OK;    
}

int proc_port_sampling_read_handler(void *user, int id, void *addr, 
                                    int* len, int *valid)
{
    int error;

    struct proc_pcb* pcb = (struct proc_pcb*)user;
    vspace_t *vspace = &pcb->vspace.vspace;
    seL4_CPtr cap = vspace_get_cap(vspace, addr);

    cspacepath_t loadee_frame_cap;
    cspacepath_t loader_frame_cap;

    vka_cspace_make_path(&procServ.vka, cap, &loadee_frame_cap);

    error = vka_cnode_copy(&loader_frame_cap, &loadee_frame_cap, seL4_AllRights);
    assert(error == 0);

    char *loader_vaddr = vspace_map_pages(&procServ.vspace, 
                                          &loader_frame_cap.capPtr, 
                                          NULL, 
                                          seL4_AllRights,
                                          1, 
                                          seL4_PageBits, 
                                          1
                                    );

    memcpy(loader_vaddr, pok_ports[id].data, pok_ports[id].size);

    return 0;
}

//==================process interface ==================//

int proc_set_prio_handler(void *user, int pid, int prio)
{
    struct proc_pcb* pcb = (struct proc_pcb*)user;
    struct proc_tcb* tcb = (struct proc_tcb*)cvector_get(&pcb->threads, 0);
    seL4_CPtr ktcb = thread_tcb_obj(tcb);

    struct partition *part = pcb->proc->part;

    if (part->low > pid || part->high < pid)
        return POK_ERRNO_PARAM;

    int error = seL4_TCB_SetPriority(ktcb, prio);
    if(error)
        return POK_ERRNO_PARAM;
    else
        return POK_ERRNO_OK;
}


int proc_stop_self_handler(void *user)
{
    struct proc_pcb* pcb = (struct proc_pcb*)user;
    struct proc_tcb* tcb = (struct proc_tcb*)cvector_get(&pcb->threads, 0);
    seL4_CPtr ktcb = thread_tcb_obj(tcb);

    seL4_TCB_Suspend(ktcb);

    return 0;
}

int proc_stop_handler(void *user, int pid)
{
    struct proc_pcb* pcb = (struct proc_pcb*)user;

    struct partition *part = pcb->proc->part;

    if (part->low > pid || part->high < pid)
        return POK_ERRNO_PARAM;

    struct proc_pcb* p = pid_get_pcb(&procServ.PIDList, pid);
    struct proc_tcb* tcb = (struct proc_tcb*)cvector_get(&p->threads, 0);
    seL4_CPtr ktcb = thread_tcb_obj(tcb);

    seL4_TCB_Suspend(ktcb);

    return POK_ERRNO_OK;
}

int proc_resume_handler(void *user, int pid)
{
    struct proc_pcb* pcb = (struct proc_pcb*)user;

    struct partition *part = pcb->proc->part;

    if (part->low > pid || part->high < pid)
        return POK_ERRNO_PARAM;

    struct proc_pcb* p = pid_get_pcb(&procServ.PIDList, pid);
    struct proc_tcb* tcb = (struct proc_tcb*)cvector_get(&p->threads, 0);
    seL4_CPtr ktcb = thread_tcb_obj(tcb);

    seL4_TCB_Resume(ktcb);

    return POK_ERRNO_OK;
}

// =============== to implement get_proces_status ============= //

int proc_get_baseprio_from_pid_handler(void *user, int pid)
{
    struct proc_pcb* pcb = (struct proc_pcb*)user;
    struct partition *part = pcb->proc->part;

    if (part->low > pid || part->high < pid)
        return -1;
    return proc_array[pid].prio;
}

int proc_get_currprio_from_pid_handler(void *user, int pid)
{
    struct proc_pcb* pcb = (struct proc_pcb*)user;
    struct partition *part = pcb->proc->part;
    if (part->low > pid || part->high < pid)
        return -1;
    return proc_array[pid].prio;
}

int proc_get_status_from_pid_handler(void *user, int pid)
{
    struct proc_pcb* pcb = (struct proc_pcb*)user;
    struct partition *part = pcb->proc->part;

    if (part->low > pid || part->high < pid)
        return -1;
    return proc_array[pid].status;
}

int proc_get_deadline_from_pid_handler(void *user, int pid)
{
    struct proc_pcb* pcb = (struct proc_pcb*)user;
    struct partition *part = pcb->proc->part;

    if (part->low > pid || part->high < pid)
        return -1;
    return proc_array[pid].deadline;
}

int proc_get_period_from_pid_handler(void *user, int pid)
{
    struct proc_pcb* pcb = (struct proc_pcb*)user;
    struct partition *part = pcb->proc->part;

    if (part->low > pid || part->high < pid)
        return -1;
    return proc_array[pid].period;
}

int proc_get_timecap_from_pid(void *user, int pid)
{
    struct proc_pcb* pcb = (struct proc_pcb*)user;
    struct partition *part = pcb->proc->part;

    if (part->low > pid || part->high < pid)
        return -1;
    return proc_array[pid].timecap;
}

int proc_get_entrypoint_from_pid(void *user, int pid)
{
    struct proc_pcb* pcb = (struct proc_pcb*)user;
    struct partition *part = pcb->proc->part;

    if (part->low > pid || part->high < pid)
        return -1;
    return proc_array[pid].entrypoint;
}

int proc_get_stacksize_from_pid(void *user, int pid)
{
    return POK_USER_STACK_SIZE;
}

// TODO
/*
int proc_get_name_from_pid_handler(void *user, int pid)
{
    struct proc_pcb* p = (struct proc_pcb*)user;
    seL4_DebugPrintf("get name not implemented yet!\n");
    return 0;
}*/

// =============== to implement get_process_id ============= //

int proc_get_pid_from_name_handler(void *user, char *name)
{
    for (int i = 0; i < num_of_proc; i++)
    {
        if (!strcmp(name, proc_array[i].name))
            return i;
    }

    return 0;
}

int proc_getpid_handler(void *user)
{
    struct proc_pcb* pcb = (struct proc_pcb*)user;
    struct process *p = pcb->proc;
    return p->index;
}

// ================ to implement GET_PARTITION_STATUS ============== //

int proc_current_partition_get_id_handler(void *user)
{
    struct proc_pcb* pcb = (struct proc_pcb*)user;
    struct partition *part = pcb->proc->part;
    return part->index;
}

int proc_current_partition_get_period_handler(void *user)
{
    struct proc_pcb* pcb = (struct proc_pcb*)user;
    struct partition *part = pcb->proc->part;
    return part->period;
}

uint64_t proc_current_partition_get_duration_handler(void *user)
{
    //struct proc_pcb* pcb = (struct proc_pcb*)user;
    //struct partition *part = pcb->proc->part;
    uint64_t duration = pok_sched_slots[POK_SCHED_CURRENT_PARTITION];
    return duration;
}

int proc_current_partition_get_lock_level_handler(void *user)
{
    struct proc_pcb* pcb = (struct proc_pcb*)user;
    struct partition *part = pcb->proc->part;
    return part->lock_level;
}

int proc_current_partition_get_operating_mode_handler(void *user)
{
    struct proc_pcb* pcb = (struct proc_pcb*)user;
    struct partition *part = pcb->proc->part;
    return part->mode;
}

int proc_current_partition_get_start_condition_handler(void *user)
{
    struct proc_pcb* pcb = (struct proc_pcb*)user;
    struct partition *part = pcb->proc->part;
    return part->start_cond;
}

//================= testing ===============  //

int proc_getpid2_handler(void *user, int *pid)
{
    struct proc_pcb* p = (struct proc_pcb*)user;
    *pid = p->pid;
    return 0;
}

seL4_CPtr
proc_get_hello_cptr_handler(void *rpc_userptr) 
{
    return procServ.hello_cptr;
}

seL4_CPtr
proc_get_hello1_cptr_handler(void *rpc_userptr) 
{
    return procServ.hello1_cptr;
}

seL4_CPtr
proc_get_hello2_cptr_handler(void *rpc_userptr) 
{
    return procServ.hello2_cptr;
}

seL4_CPtr
proc_get_hello3_cptr_handler(void *rpc_userptr) 
{
    return procServ.hello3_cptr;
}

seL4_CPtr
proc_get_hello4_cptr_handler(void *rpc_userptr) 
{
    return procServ.hello4_cptr;
}


static seL4_CPtr
proc_syscall_allocate_endpoint(struct proc_pcb *pcb, kobject_t type)
{
    assert(pcb && pcb->magic == REFOS_PCB_MAGIC);

    /* Allocate the kernel object. */
    vka_object_t endpoint;
    int error = -1;
    if (type == KOBJECT_ENDPOINT_SYNC) {
        error = vka_alloc_endpoint(&procServ.vka, &endpoint);
    } else if (type == KOBJECT_ENDPOINT_ASYNC) {
        error = vka_alloc_async_endpoint(&procServ.vka, &endpoint);
    } else {
        assert(!"Invalid endpoint type.");
    }
    if (error || endpoint.cptr == 0) {
        ROS_ERROR("failed to allocate endpoint for process. Procserv out of memory.\n");
        return 0;
    }

    /* Track this allocation, so it may be freed along with the process vspace. */
    vs_track_obj(&pcb->vspace, endpoint);
    return endpoint.cptr;
}

/* -------------------------------- Proc Server syscall handlers -------------------------------- */

/*! @brief Handles ping syscalls. */
refos_err_t
proc_ping_handler(void *rpc_userptr) {
    struct proc_pcb *pcb = (struct proc_pcb*) rpc_userptr;
    struct procserv_msg *m = (struct procserv_msg*) pcb->rpcClient.userptr;
    assert(pcb->magic == REFOS_PCB_MAGIC);

    (void) pcb;
    (void) m;

    dprintf(COLOUR_G "PROCESS SERVER RECIEVED PING!!! HELLO THERE! (´・ω・)っ由" COLOUR_RESET "\n");
    return ESUCCESS;
}

/*! @brief Handles sync endpoint creation syscalls. */
seL4_CPtr
proc_new_endpoint_internal_handler(void *rpc_userptr , refos_err_t* rpc_errno)
{
    struct proc_pcb *pcb = (struct proc_pcb*) rpc_userptr;
    assert(pcb->magic == REFOS_PCB_MAGIC);
    dprintf("Process server creating endpoint!\n");
    seL4_CPtr ep = proc_syscall_allocate_endpoint(pcb, KOBJECT_ENDPOINT_SYNC);
    if (!ep) {
        SET_ERRNO_PTR(rpc_errno, ENOMEM);
        return 0;
    }
    SET_ERRNO_PTR(rpc_errno, ESUCCESS);
    return ep;
}

/*! @brief Handles async endpoint creation syscalls. */
seL4_CPtr
proc_new_async_endpoint_internal_handler(void *rpc_userptr , refos_err_t* rpc_errno)
{
    struct proc_pcb *pcb = (struct proc_pcb*) rpc_userptr;
    assert(pcb->magic == REFOS_PCB_MAGIC);
    dprintf("Process server creating async endpoint!\n");
    seL4_CPtr ep = proc_syscall_allocate_endpoint(pcb, KOBJECT_ENDPOINT_ASYNC);
    if (!ep) {
        SET_ERRNO_PTR(rpc_errno, ENOMEM);
        return 0;
    }
    SET_ERRNO_PTR(rpc_errno, ESUCCESS);
    return ep;
}

/*! @brief Handles client watching syscalls.

    Most servers would need to call this in order to be notified of client death in order to be able
    to delete any internal book-keeping for the dead client.
 */
refos_err_t
proc_watch_client_handler(void *rpc_userptr , seL4_CPtr rpc_liveness , seL4_CPtr rpc_deathEP ,
                          int32_t* rpc_deathID)
{
    struct proc_pcb *pcb = (struct proc_pcb*) rpc_userptr;
    struct procserv_msg *m = (struct procserv_msg*) pcb->rpcClient.userptr;
    assert(pcb->magic == REFOS_PCB_MAGIC);

    if (!check_dispatch_caps(m, 0x00000001, 2)) {
        return EINVALIDPARAM;
    }

    /* Retrieve the corresponding client's ASID unwrapped from its liveness cap. */
    if (!dispatcher_badge_liveness(rpc_liveness)) {
        return EINVALIDPARAM;
    }
    
    /* Verify the corresponding client. */
    struct proc_pcb *client = pid_get_pcb(&procServ.PIDList,
                                          rpc_liveness - PID_LIVENESS_BADGE_BASE);
    if (!client) {
        return EINVALIDPARAM; 
    }
    assert(client->magic == REFOS_PCB_MAGIC);

    /* Copy out the death notification endpoint. */
    seL4_CPtr deathNotifyEP = dispatcher_copyout_cptr(rpc_deathEP);
    if (!deathNotifyEP) {
        ROS_ERROR("could not copy out deathNotifyEP.");
        return ENOMEM; 
    }

    /* Add the new client to the watch list of the calling process. */
    int error = client_watch(&pcb->clientWatchList, client->pid, deathNotifyEP);
    if (error) {
        ROS_ERROR("failed to add to watch list. Procserv possibly out of memory.");
        dispatcher_release_copyout_cptr(deathNotifyEP);
        return error;
    }
    if (rpc_deathID) {
        (*rpc_deathID) = client->pid;
    }

    return ESUCCESS;
}


/*! @brief Handles client un-watching syscalls. */
refos_err_t
proc_unwatch_client_handler(void *rpc_userptr , seL4_CPtr rpc_liveness) 
{
    struct proc_pcb *pcb = (struct proc_pcb*) rpc_userptr;
    struct procserv_msg *m = (struct procserv_msg*) pcb->rpcClient.userptr;
    assert(pcb->magic == REFOS_PCB_MAGIC);

    if (!check_dispatch_caps(m, 0x00000001, 1)) {
        return EINVALIDPARAM;
    }

    /* Retrieve the corresponding client's ASID unwrapped from its liveness cap. */
    if (!dispatcher_badge_liveness(rpc_liveness)) {
        return EINVALIDPARAM;
    }
    
    /* Verify the corresponding client. */
    struct proc_pcb *client = pid_get_pcb(&procServ.PIDList,
                                          rpc_liveness - PID_LIVENESS_BADGE_BASE);
    if (!client) {
        return EINVALIDPARAM; 
    }
    assert(client->magic == REFOS_PCB_MAGIC);

    /* Remove the given client PID from the watch list. */
    client_unwatch(&pcb->clientWatchList, client->pid);
    return ESUCCESS;
}

/*! @brief Sets the process's parameter buffer.

    Sets the process's parameter buffer to the given RAM dataspace. Only support a RAM dataspace
    which orginated from the process server's own dataspace implementation, does not support
    an external dataspace.
*/
refos_err_t
proc_set_parambuffer_handler(void *rpc_userptr , seL4_CPtr rpc_dataspace , uint32_t rpc_size)
{
    struct proc_pcb *pcb = (struct proc_pcb*) rpc_userptr;
    struct procserv_msg *m = (struct procserv_msg*) pcb->rpcClient.userptr;
    assert(pcb->magic == REFOS_PCB_MAGIC);
    struct ram_dspace *dataspace;

    /* Special case zero size and NULL parameter buffer - means unset the parameter buffer. */
    if (rpc_size == 0 && rpc_dataspace == 0) {
        proc_set_parambuffer(pcb, NULL);
        return ESUCCESS;
    }

    if (!check_dispatch_caps(m, 0x00000001, 1)) {
        return EINVALIDPARAM;
    }

    /* Check if the given badge is a RAM dataspace. */
    if (!dispatcher_badge_dspace(rpc_dataspace)) {
        return EINVALIDPARAM;
    }

    /* Retrieve RAM dataspace structure. */
    dataspace = ram_dspace_get_badge(&procServ.dspaceList, rpc_dataspace);
    if (!dataspace) {
        dvprintf("No such dataspace!.\n");
        return EINVALID;
    }

    /* Set new parameter buffer. */
    proc_set_parambuffer(pcb, dataspace);
    return ESUCCESS;
}


/*! @brief Starts a new process. */
refos_err_t
proc_new_proc_handler(void *rpc_userptr , char* rpc_name , char* rpc_params , bool rpc_block ,
                      int32_t rpc_priority , int32_t* rpc_status)
{
    struct proc_pcb *pcb = (struct proc_pcb*) rpc_userptr;
    assert(pcb->magic == REFOS_PCB_MAGIC);
    
    if (!rpc_name) {
        return EINVALIDPARAM;
    }

    /* Kick off an instance of selfloader, which will do the actual process loading work. */
    int error = proc_load_direct("selfloader", rpc_priority, rpc_name, pcb->pid, 0x0);
    if (error != ESUCCESS) {
        ROS_WARNING("failed to run selfloader for new process [%s].", rpc_name);
        return error;
    }

    /* Optionally block parent process until child process has finished. */
    if (rpc_block) {
        /* Save the reply endpoint. */
        proc_save_caller(pcb);
        pcb->parentWaiting = true;
        pcb->rpcClient.skip_reply = true;
        return ESUCCESS;
    }

    /* Immediately resume the parent process. */
    return ESUCCESS;
}

/*! @brief Exits and deletes the process which made this call. */
refos_err_t
proc_exit_handler(void *rpc_userptr , int32_t rpc_status)
{
    struct proc_pcb *pcb = (struct proc_pcb*) rpc_userptr;
    assert(pcb->magic == REFOS_PCB_MAGIC);
    dprintf("Process PID %u exiting with status %d !!!\n", pcb->pid, rpc_status);
    pcb->exitStatus = rpc_status;
    pcb->rpcClient.skip_reply = true;
    proc_queue_release(pcb);
    return ESUCCESS;
}

int
proc_clone_internal_handler(void *rpc_userptr , seL4_Word rpc_entryPoint , seL4_Word rpc_childStack
        , int rpc_flags , seL4_Word rpc_arg , refos_err_t* rpc_errno)
{
    struct proc_pcb *pcb = (struct proc_pcb*) rpc_userptr;
    assert(pcb->magic == REFOS_PCB_MAGIC);

    int threadID = -1;
    int error = proc_clone(pcb, &threadID, (vaddr_t) rpc_childStack, (vaddr_t) rpc_entryPoint);
    SET_ERRNO_PTR(rpc_errno, error);
    return threadID;
}

refos_err_t
proc_nice_handler(void *rpc_userptr , int rpc_threadID , int rpc_priority)
{
    struct proc_pcb *pcb = (struct proc_pcb*) rpc_userptr;
    assert(pcb->magic == REFOS_PCB_MAGIC);
    return proc_nice(pcb, rpc_threadID, rpc_priority);
}

seL4_CPtr
proc_get_irq_handler_handler(void *rpc_userptr , int rpc_irq)
{
    struct proc_pcb *pcb = (struct proc_pcb*) rpc_userptr;
    assert(pcb->magic == REFOS_PCB_MAGIC);
    if ((pcb->systemCapabilitiesMask & PROCESS_PERMISSION_DEVICE_IRQ) == 0) {
        return 0;
    }
    dprintf("Process %d (%s) getting IRQ number %d...\n", pcb->pid, pcb->debugProcessName, rpc_irq);
    return procserv_get_irq_handler(rpc_irq);
}


/* ------------------------------------ Dispatcher functions ------------------------------------ */

int
check_dispatch_syscall(struct procserv_msg *m, void **userptr) {
    return check_dispatch_interface(m, userptr, RPC_PROC_LABEL_MIN, RPC_PROC_LABEL_MAX);
}