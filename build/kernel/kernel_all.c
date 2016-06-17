#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/api/syscall.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <types.h>
#include <benchmark.h>
#include <api/syscall.h>
#include <api/failures.h>
#include <api/faults.h>
#include <kernel/cspace.h>
#include <kernel/faulthandler.h>
#include <kernel/thread.h>
#include <kernel/vspace.h>
#include <machine/io.h>
#include <object/interrupt.h>
#include <model/statedata.h>
#include <string.h>

#ifdef DEBUG
#include <arch/machine/capdl.h>
#endif

void print_cycle(char *info);
/* The haskell function 'handleEvent' is split into 'handleXXX' variants
 * for each event causing a kernel entry */

void print_cycle(char *info)
{
    unsigned long long result;
    asm volatile("rdtsc" : "=A" (result));
    printf("%s: cycles are %lld\n", info, result);
}

exception_t
handleInterruptEntry(void)
{
    irq_t irq;

    irq = getActiveIRQ();
    if (irq != irqInvalid) {
        handleInterrupt(irq);
    } else {
        printf("Spurious interrupt\n");
        handleSpuriousIRQ();
    }

    schedule();
    activateThread();

    //print_cycle("will go user-land");

    return EXCEPTION_NONE;
}

exception_t
handleUnknownSyscall(word_t w)
{
#ifdef DEBUG
    if (w == SysDebugPutChar) {
        kernel_putchar(getRegister(ksCurThread, capRegister));
        return EXCEPTION_NONE;
    }
    if (w == SysDebugHalt) {
        printf("Debug halt syscall from user thread 0x%x\n", (unsigned int)ksCurThread);
        halt();
    }
    if (w == SysDebugSnapshot) {
        printf("Debug snapshot syscall from user thread 0x%x\n", (unsigned int)ksCurThread);
        capDL();
        return EXCEPTION_NONE;
    }
    if (w == SysDebugCapIdentify) {
        word_t cptr = getRegister(ksCurThread, capRegister);
        lookupCapAndSlot_ret_t lu_ret = lookupCapAndSlot(ksCurThread, cptr);
        uint32_t cap_type = cap_get_capType(lu_ret.cap);
        setRegister(ksCurThread, capRegister, cap_type);
        return EXCEPTION_NONE;
    }
    if (w == SysDebugNameThread) {
        /* This is a syscall meant to aid debugging, so if anything goes wrong
         * then assume the system is completely misconfigured and halt */
        const char *name;
        word_t cptr = getRegister(ksCurThread, capRegister);
        lookupCapAndSlot_ret_t lu_ret = lookupCapAndSlot(ksCurThread, cptr);
        /* ensure we got a TCB cap */
        uint32_t cap_type = cap_get_capType(lu_ret.cap);
        if (cap_type != cap_thread_cap) {
            userError("SysDebugNameThread: cap is not a TCB, halting");
            halt();
        }
        /* Add 1 to the IPC buffer to skip the message info word */
        name = (const char*)(lookupIPCBuffer(true, ksCurThread) + 1);
        if (!name) {
            userError("SysDebugNameThread: Failed to lookup IPC buffer, halting");
            halt();
        }
        /* ensure the name isn't too long */
        if (name[strnlen(name, seL4_MsgMaxLength * sizeof(word_t))] != '\0') {
            userError("SysDebugNameThread: Name too long, halting");
            halt();
        }
        setThreadName(TCB_PTR(cap_thread_cap_get_capTCBPtr(lu_ret.cap)), name);
        return EXCEPTION_NONE;
    }
#endif

#ifdef DANGEROUS_CODE_INJECTION
    if (w == SysDebugRun) {
        ((void (*) (void *))getRegister(ksCurThread, capRegister))((void*)getRegister(ksCurThread, msgInfoRegister));
        return EXCEPTION_NONE;
    }
#endif

#ifdef CONFIG_BENCHMARK
    if (w == SysBenchmarkResetLog) {
        ksLogIndex = 0;
        return EXCEPTION_NONE;
    } else if (w == SysBenchmarkDumpLog) {
        int i;
        word_t *buffer = lookupIPCBuffer(true, ksCurThread);
        word_t start = getRegister(ksCurThread, capRegister);
        word_t size = getRegister(ksCurThread, msgInfoRegister);
        word_t logSize = ksLogIndex > MAX_LOG_SIZE ? MAX_LOG_SIZE : ksLogIndex;

        if (buffer == NULL) {
            userError("Cannot dump benchmarking log to a thread without an ipc buffer\n");
            current_syscall_error.type = seL4_IllegalOperation;
            return EXCEPTION_SYSCALL_ERROR;
        }

        if (start > logSize) {
            userError("Start > logsize\n");
            current_syscall_error.type = seL4_InvalidArgument;
            return EXCEPTION_SYSCALL_ERROR;
        }

        /* Assume we have access to an ipc buffer 1024 words big.
         * Do no write to the first 4 bytes as these are overwritten */
        if (size > MAX_IPC_BUFFER_STORAGE) {
            size = MAX_IPC_BUFFER_STORAGE;
        }

        /* trim to size */
        if ((start + size) > logSize) {
            size = logSize - start;
        }

        /* write to ipc buffer */
        for (i = 0; i < size; i++) {
            buffer[i + 1] = ksLog[i + start];
        }

        /* Return the amount written */
        setRegister(ksCurThread, capRegister, size);
        return EXCEPTION_NONE;
    } else if (w == SysBenchmarkLogSize) {
        /* Return the amount of log items we tried to log (may exceed max size) */
        setRegister(ksCurThread, capRegister, ksLogIndex);
        return EXCEPTION_NONE;
    }
#endif /* CONFIG_BENCHMARK */

    current_fault = fault_unknown_syscall_new(w);
    handleFault(ksCurThread);

    schedule();
    activateThread();

    return EXCEPTION_NONE;
}

exception_t
handleUserLevelFault(word_t w_a, word_t w_b)
{
    current_fault = fault_user_exception_new(w_a, w_b);
    handleFault(ksCurThread);

    schedule();
    activateThread();

    return EXCEPTION_NONE;
}

exception_t
handleVMFaultEvent(vm_fault_type_t vm_faultType)
{
    exception_t status;

    status = handleVMFault(ksCurThread, vm_faultType);
    if (status != EXCEPTION_NONE) {
        handleFault(ksCurThread);
    }

    schedule();
    activateThread();

    return EXCEPTION_NONE;
}


static exception_t
handleInvocation(bool_t isCall, bool_t isBlocking)
{
    message_info_t info;
    cptr_t cptr;
    lookupCapAndSlot_ret_t lu_ret;
    word_t *buffer;
    exception_t status;
    word_t length;
    tcb_t *thread;

    thread = ksCurThread;

    info = messageInfoFromWord(getRegister(thread, msgInfoRegister));
    cptr = getRegister(thread, capRegister);

    /* faulting section */
    lu_ret = lookupCapAndSlot(thread, cptr);

    if (unlikely(lu_ret.status != EXCEPTION_NONE)) {
        userError("Invocation of invalid cap #%d.", (int)cptr);
        current_fault = fault_cap_fault_new(cptr, false);

        if (isBlocking) {
            handleFault(thread);
        }

        return EXCEPTION_NONE;
    }

    buffer = lookupIPCBuffer(false, thread);

    status = lookupExtraCaps(thread, buffer, info);

    if (unlikely(status != EXCEPTION_NONE)) {
        userError("Lookup of extra caps failed.");
        if (isBlocking) {
            handleFault(thread);
        }
        return EXCEPTION_NONE;
    }

    /* Syscall error/Preemptible section */
    length = message_info_get_msgLength(info);
    if (unlikely(length > n_msgRegisters && !buffer)) {
        length = n_msgRegisters;
    }
    status = decodeInvocation(message_info_get_msgLabel(info), length,
                              cptr, lu_ret.slot, lu_ret.cap,
                              current_extra_caps, isBlocking, isCall,
                              buffer);

    if (unlikely(status == EXCEPTION_PREEMPTED)) {
        return status;
    }

    if (unlikely(status == EXCEPTION_SYSCALL_ERROR)) {
        if (isCall) {
            replyFromKernel_error(thread);
        }
        return EXCEPTION_NONE;
    }

    if (unlikely(
                thread_state_get_tsType(thread->tcbState) == ThreadState_Restart)) {
        if (isCall) {
            replyFromKernel_success_empty(thread);
        }
        setThreadState(thread, ThreadState_Running);
    }

    return EXCEPTION_NONE;
}

static void
handleReply(void)
{
    cte_t *callerSlot;
    cap_t callerCap;

    callerSlot = TCB_PTR_CTE_PTR(ksCurThread, tcbCaller);
    callerCap = callerSlot->cap;
    switch (cap_get_capType(callerCap)) {
    case cap_reply_cap: {
        tcb_t *caller;

        if (cap_reply_cap_get_capReplyMaster(callerCap)) {
            break;
        }
        caller = TCB_PTR(cap_reply_cap_get_capTCBPtr(callerCap));
        /* Haskell error:
         * "handleReply: caller must not be the current thread" */
        assert(caller != ksCurThread);
        doReplyTransfer(ksCurThread, caller, callerSlot);
        //deleteCallerCap(ksCurThread);
        return;
    }

    case cap_null_cap:
        userError("Attempted reply operation when no reply cap present.");
        return;

    default:
        break;
    }

    fail("handleReply: invalid caller cap");
}

static void
handleWait(bool_t isBlocking)
{
    word_t epCPtr;
    lookupCap_ret_t lu_ret;

    epCPtr = getRegister(ksCurThread, capRegister);

    lu_ret = lookupCap(ksCurThread, epCPtr);
    if (unlikely(lu_ret.status != EXCEPTION_NONE)) {
        /* current_lookup_fault has been set by lookupCap */
        current_fault = fault_cap_fault_new(epCPtr, true);
        handleFault(ksCurThread);
        return;
    }

    switch (cap_get_capType(lu_ret.cap)) {
    case cap_endpoint_cap:

        if (unlikely(!cap_endpoint_cap_get_capCanReceive(lu_ret.cap) || !isBlocking)) {
            current_lookup_fault = lookup_fault_missing_capability_new(0);
            current_fault = fault_cap_fault_new(epCPtr, true);
            handleFault(ksCurThread);
            break;
        }

        deleteCallerCap(ksCurThread);
        receiveIPC(ksCurThread, lu_ret.cap);
        break;

    case cap_async_endpoint_cap: {
        async_endpoint_t *aepptr;
        tcb_t *boundTCB;
        aepptr = AEP_PTR(cap_async_endpoint_cap_get_capAEPPtr(lu_ret.cap));
        boundTCB = (tcb_t*)async_endpoint_ptr_get_aepBoundTCB(aepptr);
        if (unlikely(!cap_async_endpoint_cap_get_capAEPCanReceive(lu_ret.cap)
                     || (boundTCB && boundTCB != ksCurThread))) {
            current_lookup_fault = lookup_fault_missing_capability_new(0);
            current_fault = fault_cap_fault_new(epCPtr, true);
            handleFault(ksCurThread);
            break;
        }

        receiveAsyncIPC(ksCurThread, lu_ret.cap, isBlocking);
        break;
    }
    default:
        current_lookup_fault = lookup_fault_missing_capability_new(0);
        current_fault = fault_cap_fault_new(epCPtr, true);
        handleFault(ksCurThread);
        break;
    }
}

static void
handleYield(void)
{
    tcbSchedDequeue(ksCurThread);
    tcbSchedAppend(ksCurThread);
    rescheduleRequired();
}

exception_t
handleSyscall(syscall_t syscall)
{
    exception_t ret;
    irq_t irq;

    switch (syscall) {
    case SysSend:
        ret = handleInvocation(false, true);
        if (unlikely(ret != EXCEPTION_NONE)) {
            irq = getActiveIRQ();
            if (irq != irqInvalid) {
                handleInterrupt(irq);
            }
        }
        break;

    case SysNBSend:
        ret = handleInvocation(false, false);
        if (unlikely(ret != EXCEPTION_NONE)) {
            irq = getActiveIRQ();
            if (irq != irqInvalid) {
                handleInterrupt(irq);
            }
        }
        break;

    case SysCall:
        ret = handleInvocation(true, true);
        if (unlikely(ret != EXCEPTION_NONE)) {
            irq = getActiveIRQ();
            if (irq != irqInvalid) {
                handleInterrupt(irq);
            }
        }
        break;

    case SysWait:
        handleWait(true);
        break;

    case SysReply:
        handleReply();
        break;

    case SysReplyWait:
        handleReply();
        handleWait(true);
        break;

    case SysPoll:
        handleWait(false);
        break;

    case SysYield:
        handleYield();
        break;

    default:
        fail("Invalid syscall");
    }

    schedule();
    activateThread();

    return EXCEPTION_NONE;
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/api/benchmark.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#ifdef CONFIG_BENCHMARK

#include <benchmark.h>
#include <arch/benchmark.h>
#include <arch/machine/hardware.h>

DATA_GLOB uint64_t ksEntry;
DATA_GLOB uint64_t ksExit;
DATA_GLOB uint32_t ksLogIndex = 0;
DATA_GLOB uint32_t *ksLog;

#endif /* CONFIG_BENCHMARK */

#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/api/faults.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <types.h>
#include <object.h>
#include <machine/io.h>
#include <kernel/vspace.h>
#include <api/faults.h>
#include <api/syscall.h>
#include <util.h>

bool_t handleFaultReply(tcb_t *receiver, tcb_t *sender)
{
    message_info_t tag;
    word_t         label;
    fault_t        fault;
    unsigned int   length;

    /* These lookups are moved inward from doReplyTransfer */
    tag = messageInfoFromWord(getRegister(sender, msgInfoRegister));
    label = message_info_get_msgLabel(tag);
    length = message_info_get_msgLength(tag);
    fault = receiver->tcbFault;

    switch (fault_get_faultType(fault)) {
    case fault_cap_fault:
        return true;

    case fault_vm_fault:
        return true;

    case fault_unknown_syscall: {
        unsigned int i;
        register_t   r;
        word_t       v;
        word_t*      sendBuf;

        sendBuf = lookupIPCBuffer(false, sender);

        /* Assumes n_syscallMessage > n_msgRegisters */
        for (i = 0; i < length && i < n_msgRegisters; i++) {
            r = syscallMessage[i];
            v = getRegister(sender, msgRegisters[i]);
            setRegister(receiver, r, sanitiseRegister(r, v));
        }

        if (sendBuf) {
            for (; i < length && i < n_syscallMessage; i++) {
                r = syscallMessage[i];
                v = sendBuf[i + 1];
                setRegister(receiver, r, sanitiseRegister(r, v));
            }
        }
        /* HACK: Copy NextEIP to FaultEIP because FaultEIP will be copied */
        /* back to NextEIP later on (and we don't wanna lose NextEIP)     */
        setRegister(receiver, FaultEIP, getRegister(receiver, NextEIP));
    }
    return (label == 0);

    case fault_user_exception: {
        unsigned int i;
        register_t   r;
        word_t       v;
        word_t*      sendBuf;

        sendBuf = lookupIPCBuffer(false, sender);

        /* Assumes n_exceptionMessage > n_msgRegisters */
        for (i = 0; i < length && i < n_msgRegisters; i++) {
            r = exceptionMessage[i];
            v = getRegister(sender, msgRegisters[i]);
            setRegister(receiver, r, sanitiseRegister(r, v));
        }

        if (sendBuf) {
            for (; i < length && i < n_exceptionMessage; i++) {
                r = exceptionMessage[i];
                v = sendBuf[i + 1];
                setRegister(receiver, r, sanitiseRegister(r, v));
            }
        }
    }
    return (label == 0);

    default:
        fail("Invalid fault");
    }
}

#ifdef DEBUG

void handleKernelException(
    uint32_t vector,
    uint32_t errcode,
    uint32_t eip,
    uint32_t esp,
    uint32_t eflags,
    uint32_t cr0,
    uint32_t cr2,
    uint32_t cr3,
    uint32_t cr4
);

VISIBLE
void handleKernelException(
    uint32_t vector,
    uint32_t errcode,
    uint32_t eip,
    uint32_t esp,
    uint32_t eflags,
    uint32_t cr0,
    uint32_t cr2,
    uint32_t cr3,
    uint32_t cr4
)
{
    unsigned int i;

    printf("\n========== KERNEL EXCEPTION ==========\n");
    printf("Vector:  0x%x\n", vector);
    printf("ErrCode: 0x%x\n", errcode);
    printf("EIP:     0x%x\n", eip);
    printf("ESP:     0x%x\n", esp);
    printf("EFLAGS:  0x%x\n", eflags);
    printf("CR0:     0x%x\n", cr0);
    printf("CR2:     0x%x (page-fault address)\n", cr2);
    printf("CR3:     0x%x (page-directory physical address)\n", cr3);
    printf("CR4:     0x%x\n", cr4);
    printf("\nStack Dump:\n");
    for (i = 0; i < 20; i++) {
        printf("*0x%x == 0x%x\n", esp + i * 4, *(uint32_t*)(esp + i * 4));
    }
    printf("\nHalting...\n");
}

#endif
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/c_traps.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>
#include <model/statedata.h>
#include <arch/kernel/lock.h>
#include <arch/machine/fpu.h>
#include <arch/fastpath/fastpath.h>
#ifdef CONFIG_VTX
#include <arch/object/vtx.h>
#include <arch/object/vcpu.h>
#endif

#include <api/syscall.h>

void __attribute__((noreturn)) __attribute__((externally_visible)) restore_user_context(void);

#ifdef CONFIG_VTX

void __attribute__((noreturn)) __attribute__((externally_visible)) vmlaunch_failed(void);
void __attribute__((noreturn)) __attribute__((externally_visible)) vmlaunch_failed(void)
{
    handleVmEntryFail();
    restore_user_context();
}

static inline void __attribute__((noreturn)) restore_vmx(void)
{
    restoreVMCS();
    if (ksCurThread->tcbArch.vcpu->launched) {
        /* attempt to do a vmresume */
        asm volatile(
            // Set our stack pointer to the top of the tcb so we can efficiently pop
            "movl %0, %%esp\n"
            "popl %%eax\n"
            "popl %%ebx\n"
            "popl %%ecx\n"
            "popl %%edx\n"
            "popl %%esi\n"
            "popl %%edi\n"
            "popl %%ebp\n"
            // Now do the vmresume
            "vmresume\n"
            // if we get here we failed
            "leal _kernel_stack_top, %%esp\n"
            "jmp vmlaunch_failed\n"
            :
            : "r"(&ksCurThread->tcbArch.vcpu->gp_registers[EAX])
            // Clobber memory so the compiler is forced to complete all stores
            // before running this assembler
            : "memory"
        );
    } else {
        /* attempt to do a vmlaunch */
        asm volatile(
            // Set our stack pointer to the top of the tcb so we can efficiently pop
            "movl %0, %%esp\n"
            "popl %%eax\n"
            "popl %%ebx\n"
            "popl %%ecx\n"
            "popl %%edx\n"
            "popl %%esi\n"
            "popl %%edi\n"
            "popl %%ebp\n"
            // Now do the vmresume
            "vmlaunch\n"
            // if we get here we failed
            "leal _kernel_stack_top, %%esp\n"
            "jmp vmlaunch_failed\n"
            :
            : "r"(&ksCurThread->tcbArch.vcpu->gp_registers[EAX])
            // Clobber memory so the compiler is forced to complete all stores
            // before running this assembler
            : "memory"
        );
    }
    while (1);
}
#endif

void __attribute__((noreturn)) __attribute__((externally_visible)) restore_user_context(void)
{
    /* set the tss.esp0 */
    tss_ptr_set_esp0(&ia32KStss, ((uint32_t)ksCurThread) + 0x4c);
#ifdef CONFIG_VTX
    if (thread_state_ptr_get_tsType(&ksCurThread->tcbState) == ThreadState_RunningVM) {
        restore_vmx();
    }
#endif
    if (unlikely(ksCurThread == ia32KSfpuOwner)) {
        /* We are using the FPU, make sure it is enabled */
        enableFpu();
    } else if (unlikely(ia32KSfpuOwner)) {
        /* Someone is using the FPU and it might be enabled */
        disableFpu();
    } else {
        /* No-one (including us) is using the FPU, so we assume it
         * is currently disabled */
    }
    /* see if we entered via syscall */
    if (likely(ksCurThread->tcbArch.tcbContext.registers[Error] == -1)) {
        ksCurThread->tcbArch.tcbContext.registers[EFLAGS] &= ~0x200;
        asm volatile(
            // Set our stack pointer to the top of the tcb so we can efficiently pop
            "movl %0, %%esp\n"
            // restore syscall number
            "popl %%eax\n"
            // cap/badge register
            "popl %%ebx\n"
            // skip ecx and edx, these will contain esp and nexteip due to sysenter/sysexit convention
            "addl $8, %%esp\n"
            // message info register
            "popl %%esi\n"
            // message register
            "popl %%edi\n"
            // message register
            "popl %%ebp\n"
            //ds (if changed)
            "cmpl $0x23, (%%esp)\n"
            "je 1f\n"
            "popl %%ds\n"
            "jmp 2f\n"
            "1: addl $4, %%esp\n"
            "2:\n"
            //es (if changed)
            "cmpl $0x23, (%%esp)\n"
            "je 1f\n"
            "popl %%es\n"
            "jmp 2f\n"
            "1: addl $4, %%esp\n"
            "2:\n"
            //have to reload other selectors
            "popl %%fs\n"
            "popl %%gs\n"
            // skip faulteip, tls_base and error (these are fake registers)
            "addl $12, %%esp\n"
            // restore nexteip
            "popl %%edx\n"
            // skip cs
            "addl $4,  %%esp\n"
            "popfl\n"
            // reset interrupt bit
            "orl $0x200, -4(%%esp)\n"
            // restore esp
            "pop %%ecx\n"
            "sti\n"
            "sysexit\n"
            :
            : "r"(&ksCurThread->tcbArch.tcbContext.registers[EAX])
            // Clobber memory so the compiler is forced to complete all stores
            // before running this assembler
            : "memory"
        );
    } else {
        asm volatile(
            // Set our stack pointer to the top of the tcb so we can efficiently pop
            "movl %0, %%esp\n"
            "popl %%eax\n"
            "popl %%ebx\n"
            "popl %%ecx\n"
            "popl %%edx\n"
            "popl %%esi\n"
            "popl %%edi\n"
            "popl %%ebp\n"
            "popl %%ds\n"
            "popl %%es\n"
            "popl %%fs\n"
            "popl %%gs\n"
            // skip faulteip, tls_base, error
            "addl $12, %%esp\n"
            "iret\n"
            :
            : "r"(&ksCurThread->tcbArch.tcbContext.registers[EAX])
            // Clobber memory so the compiler is forced to complete all stores
            // before running this assembler
            : "memory"
        );
    }
    while (1);
}

void __attribute__((fastcall)) __attribute__((externally_visible)) c_handle_interrupt(int irq, int syscall);
void __attribute__((fastcall)) __attribute__((externally_visible)) c_handle_interrupt(int irq, int syscall)
{
    if (irq == int_unimpl_dev) {
        handleUnimplementedDevice();
    } else if (irq == int_page_fault) {
        /* Error code is in Error. Pull out bit 5, which is whether it was instruction or data */
        handleVMFaultEvent((ksCurThread->tcbArch.tcbContext.registers[Error] >> 4) & 1);
    } else if (irq < int_irq_min) {
        handleUserLevelFault(irq, ksCurThread->tcbArch.tcbContext.registers[Error]);
    } else if (likely(irq < int_trap_min)) {
        ia32KScurInterrupt = irq;
        handleInterruptEntry();
    } else if (irq == int_spurious) {
        /* fall through to restore_user_context and do nothing */
    } else {
        /* Interpret a trap as an unknown syscall */
        /* Adjust FaultEIP to point to trapping INT
         * instruction by subtracting 2 */
        int sys_num;
        ksCurThread->tcbArch.tcbContext.registers[FaultEIP] -= 2;
        /* trap number is MSBs of the syscall number and the LSBS of EAX */
        sys_num = (irq << 24) | (syscall & 0x00ffffff);
        handleUnknownSyscall(sys_num);
    }
    restore_user_context();
}

void __attribute__((noreturn))
slowpath(syscall_t syscall)
{
    ia32KScurInterrupt = -1;
    /* increment nextEIP to skip sysenter */
    ksCurThread->tcbArch.tcbContext.registers[NextEIP] += 2;
    /* check for undefined syscall */
    if (unlikely(syscall < SYSCALL_MIN || syscall > SYSCALL_MAX)) {
        handleUnknownSyscall(syscall);
    } else {
        handleSyscall(syscall);
    }
    restore_user_context();
}

void __attribute__((externally_visible)) c_handle_syscall(syscall_t syscall, word_t cptr, word_t msgInfo);
void __attribute__((externally_visible)) c_handle_syscall(syscall_t syscall, word_t cptr, word_t msgInfo)
{
#ifdef FASTPATH
    if (syscall == SysCall) {
        fastpath_call(cptr, msgInfo);
    } else if (syscall == SysReplyWait) {
        fastpath_reply_wait(cptr, msgInfo);
    }
#endif
#ifdef CONFIG_VTX
    if (syscall == SysVMEnter) {
        vcpu_update_vmenter_state(ksCurThread->tcbArch.vcpu);
        ksCurThread->tcbArch.tcbContext.registers[NextEIP] += 2;
        if (ksCurThread->boundAsyncEndpoint && async_endpoint_ptr_get_state(ksCurThread->boundAsyncEndpoint) == AEPState_Active) {
            completeAsyncIPC(ksCurThread->boundAsyncEndpoint, ksCurThread);
            setRegister(ksCurThread, msgInfoRegister, 0);
            /* Any guest state that we should return is in the same
             * register position as sent to us, so we can just return
             * and let the user pick up the values they put in */
            restore_user_context();
        } else {
            setThreadState(ksCurThread, ThreadState_RunningVM);
            restore_vmx();
        }
    }
#endif

    slowpath(syscall);
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/fastpath/fastpath.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <util.h>
#include <api/syscall.h>
#include <kernel/thread.h>
#include <machine/io.h>
#include <machine/profiler.h>
#include <machine/registerset.h>
#include <model/statedata.h>
#include <object/cnode.h>
#include <object/structures.h>
#include <config.h>
#include <assert.h>
#include <arch/fastpath/fastpath.h>
#include <arch/kernel/thread.h>
#include <arch/machine/fpu.h>

/* Fastpath cap lookup.  Returns a null_cap on failure. */
static inline cap_t
lookup_fp(cap_t cap, cptr_t cptr)
{
    word_t cptr2;
    cte_t *slot;
    word_t radixBits, bits;
    word_t radix;

    if (unlikely(!cap_capType_equals(cap, cap_cnode_cap))) {
        return cap_null_cap_new();
    }

    bits = cap_cnode_cap_get_capCNodeGuardSize(cap);

    do {
        radixBits = cap_cnode_cap_get_capCNodeRadix(cap);
        cptr2 = cptr << bits;

        radix = cptr2  >> (wordBits - radixBits);
        slot = CTE_PTR(cap_cnode_cap_get_capCNodePtr(cap)) + radix;

        cap = slot->cap;
        bits += radixBits;

    } while (unlikely(bits < wordBits && cap_capType_equals(cap, cap_cnode_cap)));

    if (unlikely(bits > wordBits)) {
        /* Depth mismatch. We've overshot 32 bits. The lookup we've done is
           safe, but wouldn't be allowed by the slowpath. */
        return cap_null_cap_new();
    }

    return cap;
}

static inline void FASTCALL
switchToThread_fp(tcb_t *thread, pde_t *pd)
{
    word_t base;
    uint32_t new_pd = pptr_to_paddr(pd);

    if (likely(getCurrentPD() != new_pd)) {
        setCurrentPD(new_pd);
    }

    /* Code equivalent to in Arch_switchToThread, see arch/object/structures.bf
     * for layout of gdt_data */
    /* update the GDT_TLS entry with the thread's TLS_BASE address */
    base = getRegister(thread, TLS_BASE);
    gdt_entry_gdt_data_ptr_set_base_low(ia32KSgdt + GDT_TLS, base);
    gdt_entry_gdt_data_ptr_set_base_mid(ia32KSgdt + GDT_TLS,  (base >> 16) & 0xFF);
    gdt_entry_gdt_data_ptr_set_base_high(ia32KSgdt + GDT_TLS, (base >> 24) & 0xFF);

    /* update the GDT_IPCBUF entry with the thread's IPC buffer address */
    base = thread->tcbIPCBuffer;
    gdt_entry_gdt_data_ptr_set_base_low(ia32KSgdt + GDT_IPCBUF, base);
    gdt_entry_gdt_data_ptr_set_base_mid(ia32KSgdt + GDT_IPCBUF,  (base >> 16) & 0xFF);
    gdt_entry_gdt_data_ptr_set_base_high(ia32KSgdt + GDT_IPCBUF, (base >> 24) & 0xFF);

    ksCurThread = thread;
}

/* Custom implementation of functions for manipulating some data structures generated
   from arch/object/structures.bf */

static inline void
thread_state_ptr_set_tsType_np(thread_state_t *ts_ptr, word_t tsType)
{
    ts_ptr->words[0] = tsType;
}

static inline void
thread_state_ptr_mset_blockingIPCEndpoint_tsType(thread_state_t *ts_ptr,
                                                 word_t ep_ref,
                                                 word_t tsType)
{
    ts_ptr->words[0] = ep_ref | tsType;
}

static inline void
thread_state_ptr_set_blockingIPCDiminish_np(thread_state_t *ts_ptr, word_t dim)
{
    ts_ptr->words[2] &= BIT(0);
    ts_ptr->words[1] = dim;
}

static inline void
cap_reply_cap_ptr_new_np(cap_t *cap_ptr, word_t capCallerSlot)
{
    /* 1 is capReplyMaster */
    cap_ptr->words[1] = CTE_REF(capCallerSlot) | 1;
    cap_ptr->words[0] = cap_reply_cap;
}

static inline void
cap_reply_cap_ptr_new_np2(cap_t *cap_ptr, word_t isMaster, word_t capTCBPtr)
{
    cap_ptr->words[0] = TCB_REF(capTCBPtr) | cap_reply_cap;
    cap_ptr->words[1] = isMaster;
}

static inline void
endpoint_ptr_mset_epQueue_tail_state(endpoint_t *ep_ptr, word_t epQueue_tail,
                                     word_t state)
{
    ep_ptr->words[0] = epQueue_tail | state;
}

static inline void
endpoint_ptr_set_epQueue_head_np(endpoint_t *ep_ptr, word_t epQueue_head)
{
    ep_ptr->words[1] = epQueue_head;
}


static inline bool_t
isValidNativeRoot_fp(cap_t pd_cap)
{
#ifdef CONFIG_PAE_PAGING
    return cap_capType_equals(pd_cap, cap_pdpt_cap);
#else
    return cap_capType_equals(pd_cap, cap_page_directory_cap);
#endif
}

static inline void
fastpath_copy_mrs(unsigned int length, tcb_t *src, tcb_t *dest)
{
    if (length == 2) {
        setRegister(dest, EBP, getRegister(src, EBP));
    }
    if (length == 2 || length == 1) {
        setRegister(dest, EDI, getRegister(src, EDI));
    }
}

/* This is an accelerated check that msgLength, which appears
   in the bottom of the msgInfo word, is <= 2 and that msgExtraCaps
   which appears above it is zero. We are assuming that n_msgRegisters == 2
   for this check to be useful.*/
compile_assert (n_msgRegisters_eq_2, n_msgRegisters == 2)
static inline int
fastpath_mi_check(word_t msgInfo)
{
    return (msgInfo & MASK(seL4_MsgLengthBits + seL4_MsgExtraCapBits)) > 2;
}

static inline bool_t hasDefaultSelectors(tcb_t *thread)
{
    return thread->tcbArch.tcbContext.registers[DS] == SEL_DS_3   &&
           thread->tcbArch.tcbContext.registers[ES] == SEL_DS_3;
}

static inline void FASTCALL NORETURN
fastpath_restore(word_t badge, word_t msgInfo)
{
    if (unlikely(ksCurThread == ia32KSfpuOwner)) {
        /* We are using the FPU, make sure it is enabled */
        enableFpu();
    } else if (unlikely(ia32KSfpuOwner)) {
        /* Someone is using the FPU and it might be enabled */
        disableFpu();
    } else {
        /* No-one (including us) is using the FPU, so we assume it
         * is currently disabled */
    }
    tss_ptr_set_esp0(&ia32KStss, ((uint32_t)ksCurThread) + 0x4c);
    ksCurThread->tcbArch.tcbContext.registers[EFLAGS] &= ~0x200;
    if (likely(hasDefaultSelectors(ksCurThread))) {
        asm volatile("\
                movl %%ecx, %%esp \n\
                popl %%edi \n\
                popl %%ebp \n\
                addl $8, %%esp \n\
                popl %%fs \n\
                popl %%gs \n\
                addl $20, %%esp \n\
                popfl \n\
                orl $0x200, 44(%%ecx) \n\
                movl 36(%%ecx), %%edx \n\
                pop %%ecx \n\
                sti \n\
                sysexit \n\
            "
                     :
                     : "c"(&ksCurThread->tcbArch.tcbContext.registers[EDI]),
                     "a" (ksCurThread->tcbArch.tcbContext.registers[EAX]),
                     "b" (badge),
                     "S" (msgInfo)
                     : "memory"
                    );
    } else {
        asm volatile("\
                movl %%ecx, %%esp \n\
                popl %%edi \n\
                popl %%ebp \n\
                popl %%ds \n\
                popl %%es \n\
                popl %%fs \n\
                popl %%gs \n\
                addl $20, %%esp \n\
                popfl \n\
                orl $0x200, 44(%%ecx) \n\
                movl 36(%%ecx), %%edx \n\
                pop %%ecx \n\
                sti \n\
                sysexit \n\
            "
                     :
                     : "c"(&ksCurThread->tcbArch.tcbContext.registers[EDI]),
                     "a" (ksCurThread->tcbArch.tcbContext.registers[EAX]),
                     "b" (badge),
                     "S" (msgInfo)
                     : "memory"
                    );
    }
    /* This function is marked NORETURN, but gcc is not aware that the previous assembly
       block will return to user level. This loop prevents gcc complaining, and also helps
       it optimize register usage in this function (since gcc knows it can clobber everything
       as it will not be returning or calling anything else */
    while (1);
}

void FASTCALL NORETURN
fastpath_call(word_t cptr, word_t msgInfo)
{
    message_info_t info;
    cap_t ep_cap;
    endpoint_t *ep_ptr;
    unsigned int length;
    tcb_t *dest;
    word_t badge;
    cte_t *replySlot, *callerSlot;
    cap_t newVTable;
    void *vspace;
    uint32_t fault_type;

    /* Get message info, length, and fault type. */
    info = messageInfoFromWord(msgInfo);
    length = message_info_get_msgLength(info);
    fault_type = fault_get_faultType(ksCurThread->tcbFault);

    /* Check there's no extra caps, the length is ok and there's no
     * saved fault. */
    if (unlikely(fastpath_mi_check(msgInfo) ||
                 fault_type != fault_null_fault)) {
        slowpath(SysCall);
    }

    /* Check there is nothing waiting on the async endpoint */
    if (ksCurThread->boundAsyncEndpoint &&
            async_endpoint_ptr_get_state(ksCurThread->boundAsyncEndpoint) == AEPState_Active) {
        slowpath(SysCall);
    }

    /* Lookup the cap */
    ep_cap = lookup_fp(TCB_PTR_CTE_PTR(ksCurThread, tcbCTable)->cap, cptr);

    /* Check it's an endpoint */
    if (unlikely(!cap_capType_equals(ep_cap, cap_endpoint_cap) ||
                 !cap_endpoint_cap_get_capCanSend(ep_cap))) {
        slowpath(SysCall);
    }

    /* Get the endpoint address */
    ep_ptr = EP_PTR(cap_endpoint_cap_get_capEPPtr(ep_cap));

    /* Get the destination thread, which is only going to be valid
     * if the endpoint is valid. */
    dest = TCB_PTR(endpoint_ptr_get_epQueue_head(ep_ptr));

    /* Check that there's a thread waiting to receive */
    if (unlikely(endpoint_ptr_get_state(ep_ptr) != EPState_Recv)) {
        slowpath(SysCall);
    }

    /* Get destination thread.*/
    newVTable = TCB_PTR_CTE_PTR(dest, tcbVTable)->cap;

    /* Get vspace root. */
#ifdef CONFIG_PAE_PAGING
    vspace = PDE_PTR(cap_pdpt_cap_get_capPDPTBasePtr(newVTable));
#else
    vspace = PDE_PTR(cap_page_directory_cap_get_capPDBasePtr(newVTable));
#endif

    /* Ensure that the destination has a valid VTable. */
    if (unlikely(! isValidNativeRoot_fp(newVTable))) {
        slowpath(SysCall);
    }

    /* Ensure the destination has a higher/equal priority to us. */
    if (unlikely(dest->tcbPriority < ksCurThread->tcbPriority)) {
        slowpath(SysCall);
    }

    /* Ensure that the endpoint has standard non-diminishing rights. */
    if (unlikely(!cap_endpoint_cap_get_capCanGrant(ep_cap) ||
                 thread_state_ptr_get_blockingIPCDiminishCaps(&dest->tcbState))) {
        slowpath(SysCall);
    }

    /* Ensure the original caller is in the current domain and can be scheduled directly. */
    if (CONFIG_NUM_DOMAINS > 1 && unlikely(dest->tcbDomain != ksCurDomain)) {
        slowpath(SysCall);
    }

    /*
     * --- POINT OF NO RETURN ---
     *
     * At this stage, we have committed to performing the IPC.
     */

    /* Need to update NextEIP in the calling thread */
    setRegister(ksCurThread, NextEIP, getRegister(ksCurThread, NextEIP) + 2);

    /* Dequeue the destination. */
    endpoint_ptr_set_epQueue_head_np(ep_ptr, TCB_REF(dest->tcbEPNext));
    if (unlikely(dest->tcbEPNext)) {
        dest->tcbEPNext->tcbEPPrev = NULL;
    } else {
        endpoint_ptr_mset_epQueue_tail_state(ep_ptr, 0, EPState_Idle);
    }

    badge = cap_endpoint_cap_get_capEPBadge(ep_cap);

    /* Block sender */
    thread_state_ptr_set_tsType_np(&ksCurThread->tcbState,
                                   ThreadState_BlockedOnReply);

    /* Get sender reply slot */
    replySlot = TCB_PTR_CTE_PTR(ksCurThread, tcbReply);

    /* Get dest caller slot */
    callerSlot = TCB_PTR_CTE_PTR(dest, tcbCaller);

    /* Insert reply cap */
    cap_reply_cap_ptr_new_np2(&callerSlot->cap, 0, TCB_REF(ksCurThread));
    cap_reply_cap_ptr_new_np(&replySlot->cap, CTE_REF(callerSlot));

    fastpath_copy_mrs (length, ksCurThread, dest);

    /* Dest thread is set Running, but not queued. */
    thread_state_ptr_set_tsType_np(&dest->tcbState,
                                   ThreadState_Running);
    switchToThread_fp(dest, vspace);

    msgInfo = wordFromMessageInfo(message_info_set_msgCapsUnwrapped(info, 0));
    fastpath_restore(badge, msgInfo);
}

void FASTCALL
fastpath_reply_wait(word_t cptr, word_t msgInfo)
{
    message_info_t info;
    cap_t ep_cap;
    endpoint_t *ep_ptr;
    unsigned int length;
    cte_t *callerSlot;
    cte_t *replySlot;
    cap_t callerCap;
    tcb_t *caller;
    word_t badge;
    tcb_t *endpointTail;
    uint32_t fault_type;

    cap_t newVTable;
    void *vspace;

    /* Get message info and length */
    info = messageInfoFromWord(msgInfo);
    length = message_info_get_msgLength(info);
    fault_type = fault_get_faultType(ksCurThread->tcbFault);

    /* Check there's no extra caps, the length is ok and there's no
     * saved fault. */
    if (unlikely(fastpath_mi_check(msgInfo) ||
                 fault_type != fault_null_fault)) {
        slowpath(SysReplyWait);
    }

    /* Lookup the cap */
    ep_cap = lookup_fp(TCB_PTR_CTE_PTR(ksCurThread, tcbCTable)->cap,
                       cptr);

    /* Check it's an endpoint */
    if (unlikely(!cap_capType_equals(ep_cap, cap_endpoint_cap) ||
                 !cap_endpoint_cap_get_capCanReceive(ep_cap))) {
        slowpath(SysReplyWait);
    }

    /* Check there is nothing waiting on the async endpoint */
    if (ksCurThread->boundAsyncEndpoint &&
            async_endpoint_ptr_get_state(ksCurThread->boundAsyncEndpoint) == AEPState_Active) {
        slowpath(SysReplyWait);
    }

    /* Get the endpoint address */
    ep_ptr = EP_PTR(cap_endpoint_cap_get_capEPPtr(ep_cap));

    /* Check that there's not a thread waiting to send */
    if (unlikely(endpoint_ptr_get_state(ep_ptr) == EPState_Send)) {
        slowpath(SysReplyWait);
    }

    /* Only reply if the reply cap is valid. */
    callerSlot = TCB_PTR_CTE_PTR(ksCurThread, tcbCaller);
    callerCap = callerSlot->cap;
    if (unlikely(!cap_capType_equals(callerCap, cap_reply_cap))) {
        slowpath(SysReplyWait);
    }

    /* Determine who the caller is. */
    caller = TCB_PTR(cap_reply_cap_get_capTCBPtr(callerCap));

    /* Get reply slot from the caller */
    replySlot = TCB_PTR_CTE_PTR(caller, tcbReply);

    /* Check that the caller has not faulted, in which case a fault
       reply is generated instead. */
    fault_type = fault_get_faultType(caller->tcbFault);
    if (unlikely(fault_type != fault_null_fault)) {
        slowpath(SysReplyWait);
    }

    /* Get destination thread.*/
    newVTable = TCB_PTR_CTE_PTR(caller, tcbVTable)->cap;

    /* Get vspace root. */
#ifdef CONFIG_PAE_PAGING
    vspace = PDE_PTR(cap_pdpt_cap_get_capPDPTBasePtr(newVTable));
#else
    vspace = PDE_PTR(cap_page_directory_cap_get_capPDBasePtr(newVTable));
#endif

    /* Ensure that the destination has a valid MMU. */
    if (unlikely(! isValidNativeRoot_fp (newVTable))) {
        slowpath(SysReplyWait);
    }

    /* Ensure the original caller can be scheduled directly. */
    if (unlikely(caller->tcbPriority < ksCurThread->tcbPriority)) {
        slowpath(SysReplyWait);
    }

    /* Ensure the original caller is in the current domain and can be scheduled directly. */
    if (CONFIG_NUM_DOMAINS > 1 && unlikely(caller->tcbDomain != ksCurDomain)) {
        slowpath(SysReplyWait);
    }

    /*
     * --- POINT OF NO RETURN ---
     *
     * At this stage, we have committed to performing the IPC.
     */

    /* Need to update NextEIP in the calling thread */
    setRegister(ksCurThread, NextEIP, getRegister(ksCurThread, NextEIP) + 2);

    /* Set thread state to BlockedOnReceive */
    thread_state_ptr_mset_blockingIPCEndpoint_tsType(
        &ksCurThread->tcbState, (word_t)ep_ptr, ThreadState_BlockedOnReceive);
    thread_state_ptr_set_blockingIPCDiminish_np(
        &ksCurThread->tcbState, ! cap_endpoint_cap_get_capCanSend(ep_cap));

    /* Place the thread in the endpoint queue */
    endpointTail = TCB_PTR(endpoint_ptr_get_epQueue_tail(ep_ptr));
    if (likely(!endpointTail)) {
        ksCurThread->tcbEPPrev = NULL;
        ksCurThread->tcbEPNext = NULL;

        /* Set head/tail of queue and endpoint state. */
        endpoint_ptr_set_epQueue_head_np(ep_ptr, TCB_REF(ksCurThread));
        endpoint_ptr_mset_epQueue_tail_state(ep_ptr, TCB_REF(ksCurThread),
                                             EPState_Recv);
    } else {
        /* Append current thread onto the queue. */
        endpointTail->tcbEPNext = ksCurThread;
        ksCurThread->tcbEPPrev = endpointTail;
        ksCurThread->tcbEPNext = NULL;

        /* Update tail of queue. */
        endpoint_ptr_mset_epQueue_tail_state(ep_ptr, TCB_REF(ksCurThread),
                                             EPState_Recv);
    }

    /* Delete the reply cap. */
    cap_reply_cap_ptr_new_np(&replySlot->cap, CTE_REF(NULL));
    callerSlot->cap = cap_null_cap_new();

    /* I know there's no fault, so straight to the transfer. */

    /* Replies don't have a badge. */
    badge = 0;

    fastpath_copy_mrs (length, ksCurThread, caller);

    /* Dest thread is set Running, but not queued. */
    thread_state_ptr_set_tsType_np(&caller->tcbState,
                                   ThreadState_Running);
    switchToThread_fp(caller, vspace);

    msgInfo = wordFromMessageInfo(message_info_set_msgCapsUnwrapped(info, 0));
    fastpath_restore(badge, msgInfo);
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/kernel/apic.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>
#include <machine/io.h>
#include <arch/machine.h>
#include <arch/kernel/apic.h>
#include <arch/linker.h>
#include <plat/machine/devices.h>
#include <plat/machine/pit.h>

typedef enum _apic_reg_t {
    APIC_ID             = 0x020,
    APIC_VERSION        = 0x030,
    APIC_TASK_PRIO      = 0x080,
    APIC_ARBITR_PRIO    = 0x090,
    APIC_PROC_PRIO      = 0x0A0,
    APIC_EOI            = 0x0B0,
    APIC_LOCAL_DEST     = 0x0D0,
    APIC_DEST_FORMAT    = 0x0E0,
    APIC_SVR            = 0x0F0,
    APIC_ISR_BASE       = 0x100,
    APIC_TMR_BASE       = 0x180,
    APIC_IRR_BASE       = 0x200,
    APIC_ERR_STATUS     = 0x280,
    APIC_ICR1           = 0x300,
    APIC_ICR2           = 0x310,
    APIC_LVT_TIMER      = 0x320,
    APIC_LVT_THERMAL    = 0x330,
    APIC_LVT_PERF_CNTR  = 0x340,
    APIC_LVT_LINT0      = 0x350,
    APIC_LVT_LINT1      = 0x360,
    APIC_LVT_ERROR      = 0x370,
    APIC_TIMER_COUNT    = 0x380,
    APIC_TIMER_CURRENT  = 0x390,
    APIC_TIMER_DIVIDE   = 0x3E0
} apic_reg_t;

PHYS_CODE
static inline uint32_t FORCE_INLINE
apic_read_reg_(uint32_t addr, apic_reg_t reg)
{
    return *(volatile uint32_t*)(addr + reg);
}

PHYS_CODE
static inline void FORCE_INLINE
apic_write_reg_(uint32_t addr, apic_reg_t reg, uint32_t val)
{
    *(volatile uint32_t*)(addr + reg) = val;
}

static inline uint32_t
apic_read_reg(apic_reg_t reg)
{
    return *(volatile uint32_t*)(PPTR_APIC + reg);
}

static inline void
apic_write_reg(apic_reg_t reg, uint32_t val)
{
    *(volatile uint32_t*)(PPTR_APIC + reg) = val;
}

PHYS_CODE VISIBLE uint32_t
apic_measure_freq(paddr_t paddr_apic)
{
    pit_init();
    /* wait for 1st PIT wraparound */
    pit_wait_wraparound();

    /* start APIC timer countdown */
    apic_write_reg_(paddr_apic, APIC_TIMER_DIVIDE, 0xb); /* divisor = 1 */
    apic_write_reg_(paddr_apic, APIC_TIMER_COUNT, 0xffffffff);

    /* wait for 2nd PIT wraparound */
    pit_wait_wraparound();

    /* calculate APIC/bus cycles per ms = frequency in kHz */
    return (0xffffffff - apic_read_reg_(paddr_apic, APIC_TIMER_CURRENT)) / PIT_WRAPAROUND_MS;
}

BOOT_CODE paddr_t
apic_get_base_paddr(void)
{
    apic_base_msr_t apic_base_msr;

    apic_base_msr.words[0] = ia32_rdmsr_low(IA32_APIC_BASE_MSR);
    if (!apic_base_msr_get_enabled(apic_base_msr)) {
        printf("APIC: Enabled bit not set\n");
    }

    return apic_base_msr_get_base_addr(apic_base_msr);
}

BOOT_CODE bool_t
apic_init(uint32_t apic_khz, bool_t mask_legacy_irqs)
{
    apic_version_t apic_version;
    uint32_t num_lvt_entries;

    apic_version.words[0] = apic_read_reg(APIC_VERSION);

    /* check for correct version: 0x1X */
    if (apic_version_get_version(apic_version) >> 4 != 1) {
        printf("APIC: apic_version must be 0x1X\n");
        return false;
    }

    /* check for correct number of LVT entries */
    num_lvt_entries = apic_version_get_max_lvt_entry(apic_version) + 1;
    if (num_lvt_entries < 3) {
        printf("APIC: number of LVT entries: %d\n", num_lvt_entries);
        printf("APIC: number of LVT entries must be >= 3\n");
        return false;
    }

    /* initialise APIC timer */
    apic_write_reg(APIC_TIMER_DIVIDE, 0xb); /* divisor = 1 */
    apic_write_reg(APIC_TIMER_COUNT, apic_khz * CONFIG_TIMER_TICK_MS);

    /* enable APIC using SVR register */
    apic_write_reg(
        APIC_SVR,
        apic_svr_new(
            0,           /* focus_processor_chk */
            1,           /* enabled             */
            int_spurious /* spurious_vector     */
        ).words[0]
    );

    /* mask/unmask LINT0 (used for legacy IRQ delivery) */
    apic_write_reg(
        APIC_LVT_LINT0,
        apic_lvt_new(
            0,                /* timer_mode      */
            mask_legacy_irqs, /* masked          */
            0,                /* trigger_mode    */
            0,                /* remote_irr      */
            0,                /* pin_polarity    */
            0,                /* delivery_status */
            7,                /* delivery_mode   */
            0                 /* vector          */
        ).words[0]
    );

    /* mask LINT1 (used for NMI delivery) */
    apic_write_reg(
        APIC_LVT_LINT1,
        apic_lvt_new(
            0,  /* timer_mode      */
            1,  /* masked          */
            0,  /* trigger_mode    */
            0,  /* remote_irr      */
            0,  /* pin_polarity    */
            0,  /* delivery_status */
            0,  /* delivery_mode   */
            0   /* vector          */
        ).words[0]
    );

    /* initialise timer */
    apic_write_reg(
        APIC_LVT_TIMER,
        apic_lvt_new(
            1,        /* timer_mode      */
            0,        /* masked          */
            0,        /* trigger_mode    */
            0,        /* remote_irr      */
            0,        /* pin_polarity    */
            0,        /* delivery_status */
            0,        /* delivery_mode   */
            int_timer /* vector          */
        ).words[0]
    );

    /*
    printf("APIC: ID=0x%x\n", apic_read_reg(APIC_ID) >> 24);
    printf("APIC: SVR=0x%x\n", apic_read_reg(APIC_SVR));
    printf("APIC: LVT_TIMER=0x%x\n", apic_read_reg(APIC_LVT_TIMER));
    printf("APIC: LVT_LINT0=0x%x\n", apic_read_reg(APIC_LVT_LINT0));
    printf("APIC: LVT_LINT1=0x%x\n", apic_read_reg(APIC_LVT_LINT1));
    printf("APIC: LVT_ERROR=0x%x\n", apic_read_reg(APIC_LVT_ERROR));
    printf("APIC: LVT_PERF_CNTR=0x%x\n", apic_read_reg(APIC_LVT_PERF_CNTR));
    printf("APIC: LVT_THERMAL=0x%x\n", apic_read_reg(APIC_LVT_THERMAL));
    */
    return true;
}

bool_t apic_is_interrupt_pending(void)
{
    unsigned int i;

    /* read 256-bit register: each 32-bit word is 16 byte aligned */
    assert(int_irq_min % 32 == 0);
    for (i = int_irq_min; i <= int_irq_max; i += 32) {
        if (apic_read_reg(APIC_IRR_BASE + i / 2) != 0) {
            return true;
        }
    }
    return false;
}

void apic_ack_active_interrupt(void)
{
    apic_write_reg(APIC_EOI, 0);
}

BOOT_CODE void
apic_send_init_ipi(cpu_id_t cpu_id)
{
    apic_write_reg(
        APIC_ICR2,
        apic_icr2_new(
            cpu_id /* dest */
        ).words[0]
    );
    apic_write_reg(
        APIC_ICR1,
        apic_icr1_new(
            0,  /* dest_shorthand  */
            1,  /* trigger_mode    */
            1,  /* level           */
            0,  /* delivery_status */
            0,  /* dest_mode       */
            5,  /* delivery_mode   */
            0   /* vector          */
        ).words[0]
    );

    apic_write_reg(
        APIC_ICR2,
        apic_icr2_new(
            cpu_id /* dest */
        ).words[0]
    );
    apic_write_reg(
        APIC_ICR1,
        apic_icr1_new(
            0,  /* dest_shorthand  */
            1,  /* trigger_mode    */
            0,  /* level           */
            0,  /* delivery_status */
            0,  /* dest_mode       */
            5,  /* delivery_mode   */
            0   /* vector          */
        ).words[0]
    );
}

BOOT_CODE void
apic_send_startup_ipi(cpu_id_t cpu_id, paddr_t startup_addr)
{
    /* check if 4K aligned */
    assert(IS_ALIGNED(startup_addr, PAGE_BITS));
    /* check if startup_addr < 640K */
    assert(startup_addr < 0xa0000);
    startup_addr >>= PAGE_BITS;

    apic_write_reg(
        APIC_ICR2,
        apic_icr2_new(
            cpu_id /* dest */
        ).words[0]
    );
    apic_write_reg(
        APIC_ICR1,
        apic_icr1_new(
            0,           /* dest_shorthand  */
            0,           /* trigger_mode    */
            0,           /* level           */
            0,           /* delivery_status */
            0,           /* dest_mode       */
            6,           /* delivery_mode   */
            startup_addr /* vector          */
        ).words[0]
    );
}

void apic_send_ipi(cpu_id_t cpu_id, interrupt_t vector)
{
    apic_write_reg(
        APIC_ICR2,
        apic_icr2_new(
            cpu_id /* dest */
        ).words[0]
    );
    apic_write_reg(
        APIC_ICR1,
        apic_icr1_new(
            0,     /* dest_shorthand  */
            0,     /* trigger_mode    */
            0,     /* level           */
            0,     /* delivery_status */
            0,     /* dest_mode       */
            0,     /* delivery_mode   */
            vector /* vector          */
        ).words[0]
    );
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/kernel/boot.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>
#include <kernel/boot.h>
#include <machine.h>
#include <machine/io.h>
#include <model/statedata.h>
#include <object/interrupt.h>
#include <arch/machine.h>
#include <arch/kernel/apic.h>
#include <arch/kernel/boot.h>
#include <arch/kernel/bootinfo.h>
#include <arch/kernel/boot_sys.h>
#include <arch/kernel/vspace.h>
#include <arch/machine/fpu.h>
#include <arch/object/ioport.h>
#include <arch/linker.h>
#include <util.h>

#ifdef CONFIG_IOMMU
#include <plat/machine/intel-vtd.h>
#endif

#ifdef CONFIG_VTX
#include <arch/object/vtx.h>
#endif

/* functions exactly corresponding to abstract specification */

BOOT_CODE static void
init_irqs(cap_t root_cnode_cap, bool_t mask_irqs)
{
    irq_t i;

    for (i = 0; i <= maxIRQ; i++) {
        if (i == irq_timer) {
            setIRQState(IRQTimer, i);
        } else if (i == irq_iommu) {
            setIRQState(IRQReserved, i);
#ifdef CONFIG_IRQ_PIC
        } else if (i == 2) {
            /* cascaded legacy PIC */
            setIRQState(IRQReserved, i);
#endif
        } else if (i >= irq_controller_min && i <= irq_controller_max)
            if (mask_irqs)
                /* Don't use setIRQState() here because it implicitly also enables */
                /* the IRQ on the interrupt controller which only node 0 is allowed to do. */
            {
                intStateIRQTable[i] = IRQReserved;
            } else {
                setIRQState(IRQInactive, i);
            }
        else if (i >= irq_msi_min && i <= irq_msi_max) {
            setIRQState(IRQInactive, i);
        } else if (i >= irq_ipi_min && i <= irq_ipi_max) {
            setIRQState(IRQInactive, i);
        }
    }

    /* provide the IRQ control cap */
    write_slot(SLOT_PTR(pptr_of_cap(root_cnode_cap), BI_CAP_IRQ_CTRL), cap_irq_control_cap_new());
}

/* Create a frame cap for the initial thread. */

BOOT_CODE cap_t
create_unmapped_it_frame_cap(pptr_t pptr, bool_t use_large)
{
    vm_page_size_t frame_size;

    if (use_large) {
        frame_size = IA32_LargePage;
    } else {
        frame_size = IA32_SmallPage;
    }
    return
        cap_frame_cap_new(
            frame_size,                    /* capFSize           */
            0,                             /* capFMappedObject   */
            0,                             /* capFMappedIndex    */
            IA32_MAPPING_PD,               /* capFMappedType     */
            wordFromVMRights(VMReadWrite), /* capFVMRights       */
            pptr                           /* capFBasePtr        */
        );
}

BOOT_CODE cap_t
create_mapped_it_frame_cap(cap_t vspace_cap, pptr_t pptr, vptr_t vptr, bool_t use_large, bool_t executable)
{
    cap_t cap;
    int shift = PT_BITS + PD_BITS + PAGE_BITS;
    pde_t *pd;
    uint32_t pd_index;
    if (shift == 32) {
        pd = PD_PTR(cap_page_directory_cap_get_capPDBasePtr(vspace_cap));
    } else {
        uint32_t pdpt_index = vptr >> shift;
        pdpte_t *pdpt = PDPT_PTR(cap_pdpt_cap_get_capPDPTBasePtr(vspace_cap));
        pd = paddr_to_pptr(pdpte_get_pd_base_address(pdpt[pdpt_index]));
    }
    pd_index = vptr >> (PT_BITS + PAGE_BITS);

    if (use_large) {
        cap = cap_frame_cap_new(
                  IA32_LargePage,                /* capFSize           */
                  PD_REF(pd),                    /* capFMappedObject   */
                  pd_index,                      /* capFMappedIndex    */
                  IA32_MAPPING_PD,               /* capFMappedType     */
                  wordFromVMRights(VMReadWrite), /* capFVMRights       */
                  pptr                           /* capFBasePtr        */
              );
    } else {
        uint32_t pt_index = (vptr >> PAGE_BITS) & MASK(PT_BITS);
        pte_t *pt = paddr_to_pptr(pde_pde_small_get_pt_base_address(pd[pd_index]));
        cap = cap_frame_cap_new(
                  IA32_SmallPage,                /* capFSize           */
                  PT_REF(pt),                    /* capFMappedObject   */
                  pt_index,                      /* capFMappedIndex    */
                  IA32_MAPPING_PD,               /* capFMappedType     */
                  wordFromVMRights(VMReadWrite), /* capFVMRights       */
                  pptr                           /* capFBasePtr        */
              );
    }
    map_it_frame_cap(cap);
    return cap;
}

/* Create a page table for the initial thread */

static BOOT_CODE cap_t
create_it_page_table_cap(cap_t vspace_cap, pptr_t pptr, vptr_t vptr)
{
    cap_t cap;
    int shift = PT_BITS + PD_BITS + PAGE_BITS;
    pde_t *pd;
    uint32_t pd_index;
    if (shift == 32) {
        pd = PD_PTR(cap_page_directory_cap_get_capPDBasePtr(vspace_cap));
    } else {
        uint32_t pdpt_index = vptr >> shift;
        pdpte_t *pdpt = PDPT_PTR(cap_pdpt_cap_get_capPDPTBasePtr(vspace_cap));
        pd = paddr_to_pptr(pdpte_get_pd_base_address(pdpt[pdpt_index]));
    }
    pd_index = vptr >> (PT_BITS + PAGE_BITS);
    cap = cap_page_table_cap_new(
              PD_REF(pd),   /* capPTMappedObject */
              pd_index,     /* capPTMappedIndex  */
              pptr          /* capPTBasePtr      */
          );
    map_it_pt_cap(cap);
    return cap;
}

static BOOT_CODE cap_t
create_it_page_directory_cap(cap_t vspace_cap, pptr_t pptr, vptr_t vptr)
{
    cap_t cap;
    int shift = PT_BITS + PD_BITS + PAGE_BITS;
    uint32_t pdpt_index;
    pdpte_t *pdpt;
    if (shift == 32) {
        pdpt = NULL;
        pdpt_index = 0;
    } else {
        pdpt = PDPT_PTR(cap_pdpt_cap_get_capPDPTBasePtr(vspace_cap));
        pdpt_index = vptr >> shift;
    }
    cap = cap_page_directory_cap_new(
              PDPT_REF(pdpt),   /* capPDMappedObject */
              pdpt_index,       /* capPDMappedIndex  */
              pptr              /* capPDBasePtr      */
          );
    if (cap_get_capType(vspace_cap) != cap_null_cap) {
        map_it_pd_cap(cap);
    }
    return cap;
}

/* Create an address space for the initial thread.
 * This includes page directory and page tables */
BOOT_CODE static cap_t
create_it_address_space(cap_t root_cnode_cap, v_region_t it_v_reg)
{
    cap_t      vspace_cap;
    vptr_t     vptr;
    pptr_t     pptr;
    slot_pos_t slot_pos_before;
    slot_pos_t slot_pos_after;

    slot_pos_before = ndks_boot.slot_pos_cur;
    if (PDPT_BITS == 0) {
        cap_t pd_cap;
        pptr_t pd_pptr;
        /* just create single PD obj and cap */
        pd_pptr = alloc_region(PD_SIZE_BITS);
        if (!pd_pptr) {
            return cap_null_cap_new();
        }
        memzero(PDE_PTR(pd_pptr), 1 << PD_SIZE_BITS);
        copyGlobalMappings(PDE_PTR(pd_pptr));
        pd_cap = create_it_page_directory_cap(cap_null_cap_new(), pd_pptr, 0);
        if (!provide_cap(root_cnode_cap, pd_cap)) {
            return cap_null_cap_new();
        }
        write_slot(SLOT_PTR(pptr_of_cap(root_cnode_cap), BI_CAP_IT_VSPACE), pd_cap);
        vspace_cap = pd_cap;
    } else {
        cap_t pdpt_cap;
        pptr_t pdpt_pptr;
        unsigned int i;
        /* create a PDPT obj and cap */
        pdpt_pptr = alloc_region(PDPT_SIZE_BITS);
        if (!pdpt_pptr) {
            return cap_null_cap_new();
        }
        memzero(PDPTE_PTR(pdpt_pptr), 1 << PDPT_SIZE_BITS);
        pdpt_cap = cap_pdpt_cap_new(
                       pdpt_pptr        /* capPDPTBasePtr */
                   );
        /* create all PD objs and caps necessary to cover userland image. For simplicity
         * to ensure we also cover the kernel window we create all PDs */
        for (i = 0; i < BIT(PDPT_BITS); i++) {
            /* The compiler is under the mistaken belief here that this shift could be
             * undefined. However, in the case that it would be undefined this code path
             * is not reachable because PDPT_BITS == 0 (see if statement at the top of
             * this function), so to work around it we must both put in a redundant
             * if statement AND place the shift in a variable. While the variable
             * will get compiled away it prevents the compiler from evaluating
             * the 1 << 32 as a constant when it shouldn't
             * tl;dr gcc evaluates constants even if code is unreachable */
            int shift = (PD_BITS + PT_BITS + PAGE_BITS);
            if (shift != 32) {
                vptr = i << shift;
            } else {
                return cap_null_cap_new();
            }

            pptr = alloc_region(PD_SIZE_BITS);
            if (!pptr) {
                return cap_null_cap_new();
            }
            memzero(PDE_PTR(pptr), 1 << PD_SIZE_BITS);
            if (!provide_cap(root_cnode_cap,
                             create_it_page_directory_cap(pdpt_cap, pptr, vptr))
               ) {
                return cap_null_cap_new();
            }
        }
        /* now that PDs exist we can copy the global mappings */
        copyGlobalMappings(PDPTE_PTR(pdpt_pptr));
        write_slot(SLOT_PTR(pptr_of_cap(root_cnode_cap), BI_CAP_IT_VSPACE), pdpt_cap);
        vspace_cap = pdpt_cap;
    }

    slot_pos_after = ndks_boot.slot_pos_cur;
    ndks_boot.bi_frame->ui_pd_caps = (slot_region_t) {
        slot_pos_before, slot_pos_after
    };
    /* create all PT objs and caps necessary to cover userland image */
    slot_pos_before = ndks_boot.slot_pos_cur;

    for (vptr = ROUND_DOWN(it_v_reg.start, PT_BITS + PAGE_BITS);
            vptr < it_v_reg.end;
            vptr += BIT(PT_BITS + PAGE_BITS)) {
        pptr = alloc_region(PT_SIZE_BITS);
        if (!pptr) {
            return cap_null_cap_new();
        }
        memzero(PTE_PTR(pptr), 1 << PT_SIZE_BITS);
        if (!provide_cap(root_cnode_cap,
                         create_it_page_table_cap(vspace_cap, pptr, vptr))
           ) {
            return cap_null_cap_new();
        }
    }

    slot_pos_after = ndks_boot.slot_pos_cur;
    ndks_boot.bi_frame->ui_pt_caps = (slot_region_t) {
        slot_pos_before, slot_pos_after
    };

    return vspace_cap;
}

BOOT_CODE static bool_t
create_device_untypeds(
    cap_t root_cnode_cap,
    dev_p_regs_t *dev_p_regs)
{
    slot_pos_t     slot_pos_before;
    slot_pos_t     slot_pos_after;
    uint32_t       i;

    slot_pos_before = ndks_boot.slot_pos_cur;
    for (i = 0; i < dev_p_regs->count; i++) {
        if (!create_untypeds_for_region(root_cnode_cap, true, paddr_to_pptr_reg(dev_p_regs->list[i]), ndks_boot.bi_frame->ut_obj_caps.start)) {
            return false;
        }
    }
    slot_pos_after = ndks_boot.slot_pos_cur;
    ndks_boot.bi_frame->ut_device_obj_caps = (slot_region_t) {
        slot_pos_before, slot_pos_after
    };
    return true;
}

BOOT_CODE static void
create_ia32_bootinfo(ia32_bootinfo_frame_t *bootinfo, vesa_info_t *vesa_info, ia32_mem_region_t* mem_regions)
{
    int i;
    bootinfo->vbe_control_info = vesa_info->vbe_control_info;
    bootinfo->vbe_mode_info = vesa_info->vbe_mode_info;
    bootinfo->vbe_mode = vesa_info->vbe_mode;
    bootinfo->vbe_interface_seg = vesa_info->vbe_interface_seg;
    bootinfo->vbe_interface_off = vesa_info->vbe_interface_off;
    bootinfo->vbe_interface_len = vesa_info->vbe_interface_len;
    for (i = 0; i < CONFIG_MAX_MEM_REGIONS; i++) {
        bootinfo->mem_regions[i] = mem_regions[i];
    }
}

BOOT_CODE static pptr_t
create_arch_bi_frame_cap(
    cap_t root_cnode_cap,
    cap_t pd_cap,
    vptr_t vptr
)
{
    pptr_t pptr;
    cap_t cap;

    pptr = alloc_region(PAGE_BITS);
    if (!pptr) {
        printf("Kernel init failed: could not allocate arch bootinfo frame\n");
        return 0;
    }
    clearMemory((void*)pptr, PAGE_BITS);

    /* create a cap and write it into the root cnode */
    cap = create_mapped_it_frame_cap(pd_cap, pptr, vptr, false, false);
    write_slot(SLOT_PTR(pptr_of_cap(root_cnode_cap), BI_CAP_BI_ARCH_FRAME), cap);

    return pptr;
}

/* This function initialises a node's kernel state. It does NOT initialise the CPU. */

BOOT_CODE bool_t
init_node_state(
    p_region_t    avail_p_reg,
    p_region_t    sh_p_reg,
    dev_p_regs_t* dev_p_regs,
    ui_info_t     ui_info,
    p_region_t    boot_mem_reuse_p_reg,
    node_id_t     node_id,
    uint32_t      num_nodes,
    cpu_id_t*     cpu_list,
    /* parameters below not modeled in abstract specification */
    pdpte_t*      kernel_pdpt,
    pde_t*        kernel_pd,
    pte_t*        kernel_pt,
    vesa_info_t*  vesa_info,
    ia32_mem_region_t* mem_regions
#ifdef CONFIG_IOMMU
    , cpu_id_t      cpu_id,
    uint32_t      num_drhu,
    paddr_t*      drhu_list,
    acpi_rmrr_list_t *rmrr_list
#endif
)
{
    cap_t         root_cnode_cap;
    vptr_t        arch_bi_frame_vptr;
    vptr_t        bi_frame_vptr;
    vptr_t        ipcbuf_vptr;
    cap_t         it_vspace_cap;
    cap_t         ipcbuf_cap;
    pptr_t        bi_frame_pptr;
    pptr_t        arch_bi_frame_pptr;
    create_frames_of_region_ret_t create_frames_ret;
    int i;
#ifdef CONFIG_BENCHMARK
    vm_attributes_t buffer_attr = {{ 0 }};
    uint32_t paddr;
    pde_t pde;
#endif /* CONFIG_BENCHMARK */

    /* convert from physical addresses to kernel pptrs */
    region_t avail_reg          = paddr_to_pptr_reg(avail_p_reg);
    region_t ui_reg             = paddr_to_pptr_reg(ui_info.p_reg);
    region_t sh_reg             = paddr_to_pptr_reg(sh_p_reg);
    region_t boot_mem_reuse_reg = paddr_to_pptr_reg(boot_mem_reuse_p_reg);

    /* convert from physical addresses to userland vptrs */
    v_region_t ui_v_reg;
    v_region_t it_v_reg;
    ui_v_reg.start = ui_info.p_reg.start - ui_info.pv_offset;
    ui_v_reg.end   = ui_info.p_reg.end   - ui_info.pv_offset;

    ipcbuf_vptr = ui_v_reg.end;
    bi_frame_vptr = ipcbuf_vptr + BIT(PAGE_BITS);
    arch_bi_frame_vptr = bi_frame_vptr + BIT(PAGE_BITS);

    /* The region of the initial thread is the user image + ipcbuf + boot info and arch boot info */
    it_v_reg.start = ui_v_reg.start;
    it_v_reg.end = arch_bi_frame_vptr + BIT(PAGE_BITS);

    /* make the free memory available to alloc_region() */
    ndks_boot.freemem[0] = avail_reg;
    for (i = 1; i < MAX_NUM_FREEMEM_REG; i++) {
        ndks_boot.freemem[i] = REG_EMPTY;
    }

    /* initialise virtual-memory-related data structures (not in abstract spec) */
    if (!init_vm_state(kernel_pdpt, kernel_pd, kernel_pt)) {
        return false;
    }

#ifdef CONFIG_BENCHMARK
    /* allocate and create the log buffer */
    buffer_attr.words[0] = IA32_PAT_MT_WRITE_THROUGH;

    paddr = pptr_to_paddr((void *) alloc_region(pageBitsForSize(IA32_LargePage)));

    /* allocate a large frame for logging */
    pde = pde_pde_large_new(
              paddr,                                   /* page_base_address    */
              vm_attributes_get_ia32PATBit(buffer_attr),      /* pat                  */
              0,                                       /* avl_cte_depth        */
              1,                                       /* global               */
              0,                                       /* dirty                */
              0,                                       /* accessed             */
              vm_attributes_get_ia32PCDBit(buffer_attr),      /* cache_disabled       */
              vm_attributes_get_ia32PWTBit(buffer_attr),      /* write_through        */
              0,                                       /* super_user           */
              1,                                       /* read_write           */
              1                                        /* present              */
          );

    /* TODO this shouldn't be hardcoded */
    ia32KSkernelPD[IA32_KSLOG_IDX] = pde;


    /* flush the tlb */
    invalidatePageStructureCache();

    /* if we crash here, the log isn't working */
#ifdef CONFIG_DEBUG_BUILD
    printf("Testing log\n");
    ksLog[0] = 0xdeadbeef;
    printf("Wrote to ksLog %x\n", ksLog[0]);
    assert(ksLog[0] == 0xdeadbeef);
#endif /* CONFIG_DEBUG_BUILD */
#endif /* CONFIG_BENCHMARK */

    /* create the root cnode */
    root_cnode_cap = create_root_cnode();

    /* create the IO port cap */
    write_slot(
        SLOT_PTR(pptr_of_cap(root_cnode_cap), BI_CAP_IO_PORT),
        cap_io_port_cap_new(
            0,                /* first port */
            NUM_IO_PORTS - 1 /* last port  */
        )
    );

    /* create the cap for managing thread domains */
    create_domain_cap(root_cnode_cap);

    /* create the IRQ CNode */
    if (!create_irq_cnode()) {
        return false;
    }

    /* initialise the IRQ states and provide the IRQ control cap */
    init_irqs(root_cnode_cap, node_id != 0);

    /* create the bootinfo frame */
    bi_frame_pptr = allocate_bi_frame(node_id, num_nodes, ipcbuf_vptr);
    if (!bi_frame_pptr) {
        return false;
    }

    /* Construct an initial address space with enough virtual addresses
     * to cover the user image + ipc buffer and bootinfo frames */
    it_vspace_cap = create_it_address_space(root_cnode_cap, it_v_reg);
    if (cap_get_capType(it_vspace_cap) == cap_null_cap) {
        return false;
    }

    /* Create and map bootinfo frame cap */
    create_bi_frame_cap(
        root_cnode_cap,
        it_vspace_cap,
        bi_frame_pptr,
        bi_frame_vptr
    );

    /* Create and map arch bootinfo frame cap */
    arch_bi_frame_pptr = create_arch_bi_frame_cap(
                             root_cnode_cap,
                             it_vspace_cap,
                             arch_bi_frame_vptr
                         );

    /* create the initial thread's IPC buffer */
    ipcbuf_cap = create_ipcbuf_frame(root_cnode_cap, it_vspace_cap, ipcbuf_vptr);
    if (cap_get_capType(ipcbuf_cap) == cap_null_cap) {
        return false;
    }

    /* create all userland image frames */
    create_frames_ret =
        create_frames_of_region(
            root_cnode_cap,
            it_vspace_cap,
            ui_reg,
            true,
            ui_info.pv_offset
        );
    if (!create_frames_ret.success) {
        return false;
    }
    ndks_boot.bi_frame->ui_frame_caps = create_frames_ret.region;

    /*
     * Initialise the NULL FPU state. This is different from merely zero'ing it
     * out (i.e., the NULL FPU state is non-zero), and must be performed before
     * the first thread is created.
     */
    resetFpu();
    saveFpuState(&ia32KSnullFpuState);
    ia32KSfpuOwner = NULL;

    /* create the idle thread */
    if (!create_idle_thread()) {
        return false;
    }

    /* create the initial thread */
    if (!create_initial_thread(
                root_cnode_cap,
                it_vspace_cap,
                ui_info.v_entry,
                bi_frame_vptr,
                ipcbuf_vptr,
                ipcbuf_cap
            )) {
        return false;
    }

#ifdef CONFIG_IOMMU
    /* initialise VTD-related data structures and the IOMMUs */
    if (!vtd_init(cpu_id, num_drhu, rmrr_list)) {
        return false;
    }

    /* write number of IOMMU PT levels into bootinfo */
    ndks_boot.bi_frame->num_iopt_levels = ia32KSnumIOPTLevels;

    /* write IOSpace master cap */
    if (ia32KSnumDrhu != 0) {
        write_slot(SLOT_PTR(pptr_of_cap(root_cnode_cap), BI_CAP_IO_SPACE), master_iospace_cap());
    }
#endif

#ifdef CONFIG_VTX
    /* allow vtx to allocate any memory it may need before we give
       the rest away */
    if (!vtx_allocate()) {
        return false;
    }
#endif

    /* convert the remaining free memory into UT objects and provide the caps */
    if (!create_untypeds(root_cnode_cap, boot_mem_reuse_reg)) {
        return false;
    }
    /* WARNING: alloc_region() must not be called anymore after here! */

    /* create device frames */
    if (!create_device_untypeds(root_cnode_cap, dev_p_regs)) {
        return false;
    }

    /* create all shared frames */
    create_frames_ret =
        create_frames_of_region(
            root_cnode_cap,
            it_vspace_cap,
            sh_reg,
            false,
            0
        );
    if (!create_frames_ret.success) {
        return false;
    }
    ndks_boot.bi_frame->sh_frame_caps = create_frames_ret.region;;

    /* create ia32 specific bootinfo frame */
    create_ia32_bootinfo( (ia32_bootinfo_frame_t*)arch_bi_frame_pptr, vesa_info, mem_regions);

    /* finalise the bootinfo frame */
    bi_finalise();

#if defined DEBUG || defined RELEASE_PRINTF
    ia32KSconsolePort = console_port_of_node(node_id);
    ia32KSdebugPort = debug_port_of_node(node_id);
#endif

    ia32KSNodeID = node_id;
    ia32KSNumNodes = num_nodes;
    ia32KSCPUList = cpu_list;

    /* write IPI cap */
    write_slot(SLOT_PTR(pptr_of_cap(root_cnode_cap), BI_CAP_IPI), cap_ipi_cap_new());

    return true;
}

/* This function initialises the CPU. It does NOT initialise any kernel state. */

BOOT_CODE bool_t
init_node_cpu(
    uint32_t apic_khz,
    bool_t   mask_legacy_irqs
)
{
    /* initialise CPU's descriptor table registers (GDTR, IDTR, LDTR, TR) */
    init_dtrs();

    /* initialise MSRs (needs an initialised TSS) */
    init_sysenter_msrs();

    /* setup additional PAT MSR */
    if (!init_pat_msr()) {
        return false;
    }

    /* initialise floating-point unit */
    Arch_initFpu();

    /* initialise local APIC */
    if (!apic_init(apic_khz, mask_legacy_irqs)) {
        return false;
    }

#ifdef CONFIG_VTX
    /* initialise Intel VT-x extensions */
    vtx_enable();
#endif

#ifdef CONFIG_DEBUG_DISABLE_PREFETCHERS
    if (!disablePrefetchers()) {
        return false;
    }
#endif

    return true;
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/kernel/boot_sys.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>
#include <util.h>
#include <machine/io.h>
#include <arch/machine.h>
#include <arch/kernel/apic.h>
#include <arch/kernel/cmdline.h>
#include <arch/kernel/boot.h>
#include <arch/kernel/boot_sys.h>
#include <arch/kernel/vspace.h>
#include <arch/kernel/elf.h>
#include <arch/linker.h>
#include <plat/machine/acpi.h>
#include <plat/machine/devices.h>
#include <plat/machine/pic.h>
#include <plat/machine/ioapic.h>

/* addresses defined in linker script */
/* need a fake array to get the pointer from the linker script */

/* start/end of CPU boot code */
extern char _boot_cpu_start[1];
extern char _boot_cpu_end[1];

/* start/end of boot stack */
extern char _boot_stack_bottom[1];
extern char _boot_stack_top[1];

/* locations in kernel image */
extern char ki_boot_end[1];
extern char ki_end[1];

#ifdef DEBUG
/* start/end of .ndks section */
extern char _ndks_start[1];
extern char _ndks_end[1];

/* start/end of kernel stack */
extern char _kernel_stack_bottom[1];
extern char _kernel_stack_top[1];

/* kernel entry point */
extern char _start[1];
#endif

/* constants */

#define MULTIBOOT_HIGHMEM_START 0x100000
#define BOOT_NODE_PADDR 0x80000
#define NDKS_SIZE 0x3000
compile_assert(align_ndks_size, IS_ALIGNED(NDKS_SIZE, PAGE_BITS))
compile_assert(max_ndks_size, NDKS_SIZE <= PPTR_KDEV - PPTR_NDKS)

/* type definitions (directly corresponding to abstract specification) */

typedef struct glks {
    p_region_t   avail_p_reg; /* region of available physical memory on platform */
    p_region_t   ki_p_reg;    /* region where the kernel image is in */
    p_region_t   sh_p_reg;    /* region shared between nodes */
    uint32_t     num_nodes;   /* number of nodes */
    cpu_id_t     cpu_list       [CONFIG_MAX_NUM_NODES]; /* CPUs assigned to nodes */
    ui_info_t    ui_info_list   [CONFIG_MAX_NUM_NODES]; /* info about userland images */
    dev_p_regs_t dev_p_regs;  /* device memory regions */
    uint32_t     apic_khz;    /* frequency of APIC/bus */
    uint32_t     num_ioapic;  /* number of IOAPICs detected */
    paddr_t      ioapic_paddr[CONFIG_MAX_NUM_IOAPIC];
    vesa_info_t  vesa_info;   /* vesa information given by multiboot */
    ia32_mem_region_t mem_regions[CONFIG_MAX_MEM_REGIONS]; /* memory regions as given by multiboot */
#ifdef CONFIG_IOMMU
    uint32_t     num_drhu; /* number of IOMMUs */
    paddr_t      drhu_list[MAX_NUM_DRHU]; /* list of physical addresses of the IOMMUs */
    acpi_rmrr_list_t rmrr_list;
#endif
} glks_t;

typedef char ndks_t[NDKS_SIZE];

/* global variables (called var_glks, var_ndks_list in abstract specification) */

BOOT_DATA_GLOB
glks_t glks;

DATA_GLOB ALIGN(BIT(PAGE_BITS))
ndks_t ndks_list[CONFIG_MAX_NUM_NODES];

/* The kernel stack is actually allocated per-node as part of ndks_list, above.
 * The following definition, in conjunction with the linker script, tells the
 * linker to reserve space in virtual memory at the start of the NDKS section.
 */
SECTION(".ndks.stack") ALIGN(BIT(PAGE_BITS))
char kernel_stack_alloc[4096];

/* global variables (not covered by abstract specification) */

BOOT_DATA_GLOB
cmdline_opt_t cmdline_opt;

#ifdef CONFIG_PAE_PAGING
DATA_GLOB ALIGN(BIT(PDPT_BITS))
uint64_t kernel_pdpt_list[CONFIG_MAX_NUM_NODES][BIT(PDPT_BITS)];
#define paging_structure_t uint64_t
#else
/* In non PAE paging we define the pdpt to be the pd. This is just to
 * allow for there to be common boot code for paging structures on
 * both platforms. This common code detects if it is passed a pdpt
 * and pd at the same address, and ignores the pdpt if this happens
 */
#define kernel_pdpt_list kernel_pd_list
#define paging_structure_t uint32_t
#endif

/* the array type is explicit instead of pde_t due to a c-parser limitation */
DATA_GLOB ALIGN(BIT(PD_SIZE_BITS))
paging_structure_t kernel_pd_list[CONFIG_MAX_NUM_NODES][BIT(PD_BITS + PDPT_BITS)];

/* the array type is explicit instead of pte_t due to a c-parser limitation */
DATA_GLOB ALIGN(BIT(PT_SIZE_BITS))
paging_structure_t kernel_pt_list[CONFIG_MAX_NUM_NODES][BIT(PT_BITS)];

#if defined DEBUG || defined RELEASE_PRINTF

/* Determine whether we are in bootstrapping phase or runtime phase.
 * Is currently only needed to determine console port in debug mode.
 */
bool_t
in_boot_phase()
{
    paddr_t esp = pptr_to_paddr(get_current_esp());

    return (esp <= BOOT_NODE_PADDR ||
            (esp <= (paddr_t)_boot_stack_top && esp > (paddr_t)_boot_stack_bottom));
}

BOOT_CODE uint16_t
console_port_of_node(node_id_t node_id)
{
    return cmdline_opt.console_port[node_id];
}

BOOT_CODE uint16_t
debug_port_of_node(node_id_t node_id)
{
    return cmdline_opt.debug_port[node_id];
}
#endif

/* functions not modeled in abstract specification */

BOOT_CODE static paddr_t
load_boot_module(node_id_t node, multiboot_module_t* boot_module, paddr_t load_paddr)
{
    Elf32_Header_t* elf_file = (Elf32_Header_t*)boot_module->start;
    v_region_t v_reg;

    if (!elf32_checkFile(elf_file)) {
        printf("Boot module does not contain a valid ELF32 image\n");
        return 0;
    }

    v_reg = elf32_getMemoryBounds(elf_file);

    if (v_reg.end == 0) {
        printf("ELF32 image in boot module does not contain any segments\n");
        return 0;
    }
    v_reg.end = ROUND_UP(v_reg.end, PAGE_BITS);

    printf("size=0x%x v_entry=0x%x v_start=0x%x v_end=0x%x ",
           v_reg.end - v_reg.start,
           elf_file->e_entry,
           v_reg.start,
           v_reg.end
          );

    if (!IS_ALIGNED(v_reg.start, PAGE_BITS)) {
        printf("Userland image virtual start address must be 4KB-aligned\n");
        return 0;
    }
    if (v_reg.end + 2 * BIT(PAGE_BITS) > PPTR_USER_TOP) {
        /* for IPC buffer frame and bootinfo frame, need 2*4K of additional userland virtual memory */
        printf("Userland image virtual end address too high\n");
        return 0;
    }
    if ((elf_file->e_entry < v_reg.start) || (elf_file->e_entry >= v_reg.end)) {
        printf("Userland image entry point does not lie within userland image\n");
        return 0;
    }

    /* fill ui_info struct */
    glks.ui_info_list[node].pv_offset = load_paddr - v_reg.start;
    glks.ui_info_list[node].p_reg.start = load_paddr;
    load_paddr += v_reg.end - v_reg.start;
    glks.ui_info_list[node].p_reg.end = load_paddr;
    glks.ui_info_list[node].v_entry = elf_file->e_entry;

    printf("p_start=0x%x p_end=0x%x\n",
           glks.ui_info_list[node].p_reg.start,
           glks.ui_info_list[node].p_reg.end
          );

    if (load_paddr > glks.avail_p_reg.end) {
        printf("End of loaded userland image lies outside of usable physical memory\n");
        return 0;
    }

    /* initialise all initial userland memory and load potentially sparse ELF image */
    memzero(
        (void*)glks.ui_info_list[node].p_reg.start,
        glks.ui_info_list[node].p_reg.end - glks.ui_info_list[node].p_reg.start
    );
    elf32_load(elf_file, glks.ui_info_list[node].pv_offset);

    return load_paddr;
}

BOOT_CODE void
insert_dev_p_reg(p_region_t reg)
{
    if (glks.dev_p_regs.count < sizeof(glks.dev_p_regs.list) / sizeof(glks.dev_p_regs.list[0])) {
        glks.dev_p_regs.list[glks.dev_p_regs.count] = reg;
        glks.dev_p_regs.count++;
        printf("\n");
    } else {
        printf(" -> IGNORED! (too many)\n");
    }
}

/* functions directly corresponding to abstract specification */

BOOT_CODE cpu_id_t
cur_cpu_id(void)
{
    cpu_id_t cpu_id;
    paddr_t  esp = pptr_to_paddr(get_current_esp());

    if (esp <= (paddr_t)_boot_stack_top && esp > (paddr_t)_boot_stack_bottom) {
        cpu_id = glks.cpu_list[0];
    } else {
        cpu_id = esp >> 11;
    }

    return cpu_id;
}

BOOT_CODE node_id_t
node_of_cpu(cpu_id_t cpu_id)
{
    node_id_t i;

    for (i = 0; i < glks.num_nodes;  i++) {
        if (glks.cpu_list[i] == cpu_id) {
            return i;
        }
    }
    /* Is it even possible for this to happen? */
    fail("Couldn't find node of CPU");
}

/* split a region of physical memory into n mutually disjoint pieces */

BOOT_CODE static p_region_t
split_region(unsigned int i, unsigned int n, p_region_t reg)
{
    uint32_t offset;
    uint32_t total_frames = (reg.end - reg.start) >> PAGE_BITS;
    uint32_t frames_div = total_frames / n;
    uint32_t frames_mod = total_frames % n;

    if (i < frames_mod) {
        offset = (i * (frames_div + 1)) << PAGE_BITS;
        return (p_region_t) {
            .start = reg.start + offset,
             .end   = reg.start + offset + ((frames_div + 1) << PAGE_BITS)
        };
    } else {
        offset = (frames_mod * (frames_div + 1) + (i - frames_mod) * frames_div) << PAGE_BITS;
        return (p_region_t) {
            .start = reg.start + offset,
             .end   = reg.start + offset + (frames_div << PAGE_BITS)
        };
    }
}

BOOT_CODE static bool_t
lift_ndks(node_id_t node_id)
{
    p_region_t ndks_p_reg;

    ndks_p_reg.start = pptr_to_paddr(ndks_list[node_id]);
    ndks_p_reg.end = ndks_p_reg.start + NDKS_SIZE;

    if (!map_kernel_window(
                (pdpte_t*)kernel_pdpt_list[node_id],
                (pde_t*)kernel_pd_list[node_id],
                (pte_t*)kernel_pt_list[node_id],
                ndks_p_reg
#ifdef CONFIG_IRQ_IOAPIC
                , glks.num_ioapic,
                glks.ioapic_paddr
#endif
#ifdef CONFIG_IOMMU
                , node_id == 0 ? glks.num_drhu : 0,
                glks.drhu_list
#endif
            )) {
        return false;
    }
    write_cr3(pptr_to_paddr(kernel_pdpt_list[node_id]));
    /* Sync up the compilers view of the world here to force the PD to actually
     * be set *right now* instead of delayed */
    asm volatile("" ::: "memory");
    return true;
}

static BOOT_CODE bool_t
try_boot_node(void)
{
    p_region_t boot_mem_reuse_p_reg;

    cpu_id_t   cpu_id  = cur_cpu_id();
    node_id_t  node_id = node_of_cpu(cpu_id);

    uint32_t      num_nodes  = glks.num_nodes;
    ui_info_t     ui_info    = glks.ui_info_list[node_id];
    dev_p_regs_t* dev_p_regs = &glks.dev_p_regs;

    /* calculate this node's available physical memory */
    p_region_t this_avail_p_reg = split_region(node_id, num_nodes, glks.avail_p_reg);

    /* if we only boot up one node, we can reuse boot code/data memory */
    if (num_nodes == 1) {
        boot_mem_reuse_p_reg.start = PADDR_LOAD;
        boot_mem_reuse_p_reg.end = (paddr_t)ki_boot_end - BASE_OFFSET;
    } else {
        boot_mem_reuse_p_reg = P_REG_EMPTY;
    }

    /* map NDKS (node kernel state) into PD/PT and activate PD */
    if (!lift_ndks(node_id)) {
        return false;
    }

    /* initialise NDKS and kernel heap */
    if (!init_node_state(
                this_avail_p_reg,
                glks.sh_p_reg,
                dev_p_regs,
                ui_info,
                boot_mem_reuse_p_reg,
                node_id,
                num_nodes,
                glks.cpu_list,
                /* parameters below not modeled in abstract specification */
                (pdpte_t*)kernel_pdpt_list[node_id],
                (pde_t*)kernel_pd_list[node_id],
                (pte_t*)kernel_pt_list[node_id],
                &glks.vesa_info,
                glks.mem_regions
#ifdef CONFIG_IOMMU
                , cpu_id,
                node_id == 0 ? glks.num_drhu : 0,
                glks.drhu_list,
                &glks.rmrr_list
#endif
            )) {
        return false;
    }

    /* initialise the CPU */
    if (!init_node_cpu(
                glks.apic_khz,
#ifdef CONFIG_IRQ_IOAPIC
                1
#else
                node_id != 0
#endif
            )) {
        return false;
    }
    return true;
}

/* This is the entry function for SMP nodes. Node 0 calls
 * try_boot_node directly */
BOOT_CODE VISIBLE void
boot_node(void)
{
    bool_t result;
    result = try_boot_node();
    if (!result) {
        fail("Failed to start node :(\n");
    }
}

BOOT_CODE static void
start_cpu(cpu_id_t cpu_id, paddr_t boot_fun_paddr)
{
    /* memory fence needed before starting the other CPU */
    ia32_mfence();

    /* starting the other CPU */
    apic_send_init_ipi(cpu_id);
    apic_send_startup_ipi(cpu_id, boot_fun_paddr);
}

static BOOT_CODE bool_t
try_boot_sys(
    unsigned long multiboot_magic,
    multiboot_info_t* mbi,
    uint32_t apic_khz
)
{
    /* ==== following code corresponds to the "select" in abstract specification ==== */

    acpi_rsdt_t* acpi_rsdt; /* physical address of ACPI root */
    paddr_t mods_end_paddr; /* physical address where boot modules end */
    paddr_t load_paddr;
    unsigned int i;
    p_region_t ui_p_regs;
    multiboot_module_t *modules = (multiboot_module_t*)mbi->mod_list;

    glks.num_nodes = 1; /* needed to enable console output */

    if (multiboot_magic != MULTIBOOT_MAGIC) {
        printf("Boot loader not multiboot compliant\n");
        return false;
    }
    cmdline_parse((const char *)mbi->cmdline, &cmdline_opt);

    /* assert correct NDKS location and size */
    assert((uint32_t)_ndks_start == PPTR_NDKS);
    assert(_ndks_end - _ndks_start <= NDKS_SIZE);

    if ((mbi->flags & MULTIBOOT_INFO_MEM_FLAG) == 0) {
        printf("Boot loader did not provide information about physical memory size\n");
        return false;
    }

    assert(_boot_cpu_end - _boot_cpu_start < 0x400);
    if ((mbi->mem_lower << 10) < BOOT_NODE_PADDR + 0x400) {
        printf("Need at least 513K of available lower physical memory\n");
        return false;
    }

    /* copy VESA information from multiboot header */
    if ((mbi->flags & MULTIBOOT_INFO_GRAPHICS_FLAG) == 0) {
        glks.vesa_info.vbe_mode = -1;
        printf("Multiboot gave us no video information :(\n");
    } else {
        glks.vesa_info.vbe_control_info = *mbi->vbe_control_info;
        glks.vesa_info.vbe_mode_info = *mbi->vbe_mode_info;
        glks.vesa_info.vbe_mode = mbi->vbe_mode;
        printf("Got VBE info in multiboot. Current video mode is %d\n", mbi->vbe_mode);
        glks.vesa_info.vbe_interface_seg = mbi->vbe_interface_seg;
        glks.vesa_info.vbe_interface_off = mbi->vbe_interface_off;
        glks.vesa_info.vbe_interface_len = mbi->vbe_interface_len;
    }
    /* copy memory map from multiboot header */
    if ((mbi->flags & MULTIBOOT_INFO_MEM_MAP) != 0) {
        multiboot_memory_map_t *map = (multiboot_memory_map_t*)((uint32_t)mbi->mmap_addr);
        multiboot_memory_map_t *map_end = (multiboot_memory_map_t*)((uint32_t)mbi->mmap_addr + mbi->mmap_length);
        i = 0;
        while (map < map_end && i < CONFIG_MAX_MEM_REGIONS) {
            if (map->type == MULTIBOOT_MEMORY_AVAILABLE) {
                /* We will freely describe memory in the kernel window and leave it up
                 * to userland to not use it. Also taunt the user with
                 * memory that is >4gb that they cannot yet use */
                glks.mem_regions[i].paddr = map->addr;
                glks.mem_regions[i].len = map->len;
                i++;
                printf("Found memory at 0x%x:0x%x - 0x%x:0x%x\n", (uint32_t)(map->addr >> 32), (uint32_t)map->addr, (uint32_t)( (map->addr + map->len) >> 32), (uint32_t)(map->addr + map->len));
            }
            /* The 'size' element in the multiboot struct is technically at offset -4 in the struct
             * so we need to add 4 here for everything to work. Please don't think on this too hard */
            map = (multiboot_memory_map_t*)((uint32_t)map + map->size + 4);
        }
        if (map < map_end) {
            printf("Found > %d memory regions. Consider increasing CONFIG_MAX_MEM_REGIONS\n", CONFIG_MAX_MEM_REGIONS);
        }
    } else {
        printf("Multiboot gave us no memory map :(\n");
        i = 0;
    }
    while (i < CONFIG_MAX_MEM_REGIONS) {
        glks.mem_regions[i].paddr = 0;
        glks.mem_regions[i].len = 0;
        i++;
    }

    /* copy CPU bootup code to lower memory */
    memcpy((void*)BOOT_NODE_PADDR, _boot_cpu_start, _boot_cpu_end - _boot_cpu_start);

    printf("Physical high memory given to seL4: start=0x%x end=0x%x size=0x%x\n",
           MULTIBOOT_HIGHMEM_START,
           mbi->mem_upper << 10,
           (mbi->mem_upper << 10) - MULTIBOOT_HIGHMEM_START);
    /* calculate available physical memory (above 1M) */
    glks.avail_p_reg.start = MULTIBOOT_HIGHMEM_START;
    glks.avail_p_reg.end = ROUND_DOWN(glks.avail_p_reg.start + (mbi->mem_upper << 10), PAGE_BITS);
    if (glks.avail_p_reg.end > PADDR_TOP) {
        glks.avail_p_reg.end = PADDR_TOP;
    }

    printf("Physical memory usable by seL4 (kernel): start=0x%x end=0x%x size=0x%x\n",
           glks.avail_p_reg.start,
           glks.avail_p_reg.end,
           glks.avail_p_reg.end - glks.avail_p_reg.start
          );

    glks.ki_p_reg.start = PADDR_LOAD;
    glks.ki_p_reg.end = pptr_to_paddr(ki_end);

    printf("Kernel loaded to: start=0x%x end=0x%x size=0x%x entry=0x%x\n",
           glks.ki_p_reg.start,
           glks.ki_p_reg.end,
           glks.ki_p_reg.end - glks.ki_p_reg.start,
           (paddr_t)_start
          );
    printf("Kernel stack size: 0x%x\n", _kernel_stack_top - _kernel_stack_bottom);

    glks.apic_khz = apic_khz;
    printf("APIC: Bus frequency is %d MHz\n", glks.apic_khz / 1000);

    /* remapping legacy IRQs to their correct vectors */
    pic_remap_irqs(IRQ_INT_OFFSET);
#ifdef CONFIG_IRQ_IOAPIC
    /* Disable the PIC so that it does not generate any interrupts. We need to
     * do this *before* we initialize the apic */
    pic_disable();
#endif

    /* Prepare for accepting device regions from here on */
    glks.dev_p_regs.count = 0;

    /* get ACPI root table */
    acpi_rsdt = acpi_init();
    if (!acpi_rsdt) {
        return false;
    }

#ifdef CONFIG_IOMMU
    if (cmdline_opt.disable_iommu) {
        glks.num_drhu = 0;
    } else {
        /* query available IOMMUs from ACPI */
        acpi_dmar_scan(
            acpi_rsdt,
            glks.drhu_list,
            &glks.num_drhu,
            MAX_NUM_DRHU,
            &glks.rmrr_list
        );
    }
#endif

    /* query available CPUs from ACPI */
    glks.num_nodes = acpi_madt_scan(acpi_rsdt, glks.cpu_list, CONFIG_MAX_NUM_NODES, &glks.num_ioapic, glks.ioapic_paddr);
    if (glks.num_nodes == 0) {
        printf("No CPUs detected\n");
        return false;
    }
#ifdef CONFIG_IRQ_IOAPIC
    if (glks.num_ioapic == 0) {
        printf("No IOAPICs detected\n");
        return false;
    }
#else
    if (glks.num_ioapic > 0) {
        printf("Detected %d IOAPICs, but configured to use PIC instead\n", glks.num_ioapic);
    }
#endif

    if (glks.num_nodes > cmdline_opt.max_num_nodes) {
        glks.num_nodes = cmdline_opt.max_num_nodes;
    }
    printf("Will boot up %d seL4 node(s)\n", glks.num_nodes);

    if (!(mbi->flags & MULTIBOOT_INFO_MODS_FLAG)) {
        printf("Boot loader did not provide information about boot modules\n");
        return false;
    }

    printf("Detected %d boot module(s):\n", mbi->mod_count);
    mods_end_paddr = 0;

    for (i = 0; i < mbi->mod_count; i++) {
        printf(
            "  module #%d: start=0x%x end=0x%x size=0x%x name='%s'\n",
            i,
            modules[i].start,
            modules[i].end,
            modules[i].end - modules[i].start,
            modules[i].name
        );
        if ((int32_t)(modules[i].end - modules[i].start) <= 0) {
            printf("Invalid boot module size! Possible cause: boot module file not found by QEMU\n");
            return false;
        }
        if (mods_end_paddr < modules[i].end) {
            mods_end_paddr = modules[i].end;
        }
    }
    mods_end_paddr = ROUND_UP(mods_end_paddr, PAGE_BITS);
    assert(mods_end_paddr > glks.ki_p_reg.end);

    if (mbi->mod_count < 1) {
        printf("Expect at least one boot module (containing a userland image)\n");
        return false;
    }

    printf("ELF-loading userland images from boot modules:\n");
    load_paddr = mods_end_paddr;

    for (i = 0; i < mbi->mod_count && i < glks.num_nodes; i++) {
        printf("  module #%d for node #%d: ", i, i);
        load_paddr = load_boot_module(i, modules + i, load_paddr);
        if (!load_paddr) {
            return false;
        }
    }

    for (i = mbi->mod_count; i < glks.num_nodes; i++) {
        printf("  module #%d for node #%d: ", mbi->mod_count - 1, i);
        load_paddr = load_boot_module(i, modules + mbi->mod_count - 1, load_paddr);
        if (!load_paddr) {
            return false;
        }
    }

    /* calculate final location of userland images */
    ui_p_regs.start = glks.ki_p_reg.end;
    ui_p_regs.end = ui_p_regs.start + load_paddr - mods_end_paddr;

    printf(
        "Moving loaded userland images to final location: from=0x%x to=0x%x size=0x%x\n",
        mods_end_paddr,
        ui_p_regs.start,
        ui_p_regs.end - ui_p_regs.start
    );
    memcpy((void*)ui_p_regs.start, (void*)mods_end_paddr, ui_p_regs.end - ui_p_regs.start);

    for (i = 0; i < glks.num_nodes; i++) {
        /* adjust p_reg and pv_offset to final load address */
        glks.ui_info_list[i].p_reg.start -= mods_end_paddr - ui_p_regs.start;
        glks.ui_info_list[i].p_reg.end   -= mods_end_paddr - ui_p_regs.start;
        glks.ui_info_list[i].pv_offset   -= mods_end_paddr - ui_p_regs.start;
    }

    /* ==== following code corresponds to abstract specification after "select" ==== */

    /* exclude kernel image from available memory */
    assert(glks.avail_p_reg.start == glks.ki_p_reg.start);
    glks.avail_p_reg.start = glks.ki_p_reg.end;

    /* exclude userland images from available memory */
    assert(glks.avail_p_reg.start == ui_p_regs.start);
    glks.avail_p_reg.start = ui_p_regs.end;

    /* choose shared memory */
    glks.sh_p_reg.start = glks.avail_p_reg.start;
    glks.sh_p_reg.end = glks.sh_p_reg.start + (cmdline_opt.num_sh_frames << PAGE_BITS);
    if (glks.sh_p_reg.end > glks.avail_p_reg.end || glks.sh_p_reg.end < glks.sh_p_reg.start) {
        printf("Not enough usable physical memory to allocate shared region\n");
        return false;
    }

    /* exclude shared region from available memory */
    assert(glks.avail_p_reg.start == glks.sh_p_reg.start);
    glks.avail_p_reg.start = glks.sh_p_reg.end;

    /* Add in all the memory except for the kernel window as device memory.
     * This is UNSAFE as we are giving user level access to regions of memory,
     * such as the APIC, that it realy should not be able to use. This needs to
     * be fixed by blacklisting regions.
     * We also need to make sure we do not use the address that will get translated
     * to the NULL pptr, or that will be considered a NULL physical address */
    insert_dev_p_reg( (p_region_t) {
        .start = 0x1000, .end = 0x100000
    } );
    insert_dev_p_reg( (p_region_t) {
        .start = glks.avail_p_reg.end, .end = pptr_to_paddr(0)
    });
    /* Specifying zero here for the .end really is correct */
    insert_dev_p_reg( (p_region_t) {
        .start = pptr_to_paddr(0) + 0x1000, .end = 0
    });

    printf("Starting node #0\n");
    if (!try_boot_node()) {
        return false;
    }

#ifdef CONFIG_IRQ_IOAPIC
    /* Now that NDKS have been lifted we can access the IOAPIC and program it */
    ioapic_init(glks.num_nodes, glks.cpu_list, glks.num_ioapic);
#endif

    /* start up other CPUs and initialise their nodes */
    for (i = 1; i < glks.num_nodes; i++) {
        printf("Starting node #%d\n", i);
        start_cpu(glks.cpu_list[i], BOOT_NODE_PADDR);
    }
    return true;
}

BOOT_CODE VISIBLE void
boot_sys(
    unsigned long multiboot_magic,
    multiboot_info_t* mbi,
    uint32_t apic_khz)
{
    bool_t result;
    result = try_boot_sys(multiboot_magic, mbi, apic_khz);

    if (!result) {
        fail("boot_sys failed for some reason :(\n");
    }
}

#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/kernel/cmdline.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>
#include <util.h>
#include <machine/io.h>
#include <arch/kernel/cmdline.h>
#include <arch/kernel/boot_sys.h>
#include <arch/linker.h>

/* 'cmdline_val' is declared globally because of a C-subset restriction.
 * It is only used in cmdline_parse(), which therefore is non-reentrant.
 */
#define MAX_CMDLINE_VAL_LEN 1000
BOOT_DATA_GLOB
char cmdline_val[MAX_CMDLINE_VAL_LEN];

/* workaround because string literals are not supported by C parser */
const char cmdline_str_max_num_nodes[]  = {'m', 'a', 'x', '_', 'n', 'u', 'm', '_', 'n', 'o', 'd', 'e', 's', 0};
const char cmdline_str_num_sh_frames[]  = {'n', 'u', 'm', '_', 's', 'h', '_', 'f', 'r', 'a', 'm', 'e', 's', 0};
const char cmdline_str_disable_iommu[]  = {'d', 'i', 's', 'a', 'b', 'l', 'e', '_', 'i', 'o', 'm', 'm', 'u', 0};

static int is_space(char c)
{
    return c <= ' ';
}

static int parse_opt(const char *cmdline, const char *opt, char *value, int bufsize)
{
    int len = -1;
    const char *optptr = NULL;

    while (true) {
        for (; is_space(*cmdline) && (*cmdline != 0); cmdline++);
        if (*cmdline == 0) {
            break;
        }

        for (optptr = opt; *optptr && *cmdline && (*cmdline != '=') && !is_space(*cmdline) && (*optptr == *cmdline); optptr++, cmdline++);

        if (*optptr == '\0' && *cmdline == '=') {
            cmdline++;

            for (len = 0; !is_space(*cmdline) && (len < bufsize - 1); cmdline++, len++) {
                value[len] = *cmdline;
            }
            if (bufsize) {
                value[len] = '\0';
            }
        }
        for (; !is_space(*cmdline); cmdline++);
    }

    return len;
}

#ifdef CONFIG_IOMMU
static int parse_bool(const char *cmdline, const char *opt)
{
    const char *optptr = NULL;

    while (1) {
        for (; is_space(*cmdline) && (*cmdline != 0); cmdline++);
        if (*cmdline == 0) {
            return 0;
        }

        for (optptr = opt; *optptr && *cmdline && !is_space(*cmdline) && (*optptr == *cmdline); optptr++, cmdline++);

        if (*optptr == '\0' && is_space(*cmdline)) {
            return 1;
        } else {
            for (; !is_space(*cmdline); cmdline++);
        }
    }
}
#endif

#if defined DEBUG || defined RELEASE_PRINTF
static void parse_uint16_array(char* str, uint16_t* array, int array_size)
{
    char* last;
    int   i = 0;
    int   v;

    while (str && i < array_size) {
        for (last = str; *str && *str != ','; str++);
        if (*str == 0) {
            str = 0;
        } else {
            *str = 0;
            str++;
        }
        v = str_to_int(last);
        if (v == -1) {
            array[i] = 0;
        } else {
            array[i] = v;
        }
        i++;
    }
}
#endif

void cmdline_parse(const char *cmdline, cmdline_opt_t* cmdline_opt)
{
    int  i;

#if defined DEBUG || defined RELEASE_PRINTF
    /* initialise to default */
    for (i = 0; i < CONFIG_MAX_NUM_NODES; i++) {
        cmdline_opt->console_port[i] = 0;
        cmdline_opt->debug_port[i] = 0;
    }
    cmdline_opt->console_port[0] = 0x3f8;
    cmdline_opt->debug_port[0] = 0x3f8;

    if (parse_opt(cmdline, "console_port", cmdline_val, MAX_CMDLINE_VAL_LEN) != -1) {
        parse_uint16_array(cmdline_val, cmdline_opt->console_port, CONFIG_MAX_NUM_NODES);
    }

    /* initialise console ports to enable debug output */
    for (i = 0; i < CONFIG_MAX_NUM_NODES; i++) {
        if (cmdline_opt->console_port[i]) {
            serial_init(cmdline_opt->console_port[i]);
        }
    }

    /* only start printing here after having parsed/set/initialised the console_port */
    printf("\nBoot config: parsing cmdline '%s'\n", cmdline);

    for (i = 0; i < CONFIG_MAX_NUM_NODES; i++)
        if (cmdline_opt->console_port[i]) {
            printf("Boot config: console_port of node #%d = 0x%x\n", i, cmdline_opt->console_port[i]);
        }

    if (parse_opt(cmdline, "debug_port", cmdline_val, MAX_CMDLINE_VAL_LEN) != -1) {
        parse_uint16_array(cmdline_val, cmdline_opt->debug_port, CONFIG_MAX_NUM_NODES);
    }

    /* initialise debug ports */
    for (i = 0; i < CONFIG_MAX_NUM_NODES; i++) {
        if (cmdline_opt->debug_port[i]) {
            serial_init(cmdline_opt->debug_port[i]);
            printf("Boot config: debug_port of node #%d = 0x%x\n", i, cmdline_opt->debug_port[i]);
        }
    }
#endif

#ifdef CONFIG_IOMMU
    cmdline_opt->disable_iommu = parse_bool(cmdline, cmdline_str_disable_iommu);
    printf("Boot config: disable_iommu = %s\n", cmdline_opt->disable_iommu ? "true" : "false");
#endif

    /* parse max_num_nodes option */
    cmdline_opt->max_num_nodes = 1; /* default */
    if (parse_opt(cmdline, cmdline_str_max_num_nodes, cmdline_val, MAX_CMDLINE_VAL_LEN) != -1) {
        i = str_to_int(cmdline_val);
        if (i > 0 && i <= CONFIG_MAX_NUM_NODES) {
            cmdline_opt->max_num_nodes = i;
        }
    }
    printf("Boot config: max_num_nodes = %d\n", cmdline_opt->max_num_nodes);

    /* parse num_sh_frames option */
    cmdline_opt->num_sh_frames = 0; /* default */
    if (parse_opt(cmdline, cmdline_str_num_sh_frames, cmdline_val, MAX_CMDLINE_VAL_LEN) != -1) {
        i = str_to_int(cmdline_val);
        if (i >= 0 && i < BIT(32 - PAGE_BITS)) {
            cmdline_opt->num_sh_frames = i;
        }
    }
    printf("Boot config: num_sh_frames = 0x%x\n", cmdline_opt->num_sh_frames);
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/kernel/elf.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <arch/kernel/elf.h>
#include <arch/linker.h>

/* minimal ELF functionality for loading GRUB boot module */

BOOT_CODE bool_t
elf32_checkFile(Elf32_Header_t* elfFile)
{
    return (
               elfFile->e_ident[0] == '\177' &&
               elfFile->e_ident[1] == 'E'    &&
               elfFile->e_ident[2] == 'L'    &&
               elfFile->e_ident[3] == 'F'    &&
               elfFile->e_ident[4] == 1
           );
}

BOOT_CODE v_region_t
elf32_getMemoryBounds(Elf32_Header_t* elfFile)
{
    Elf32_Phdr_t* phdr = (Elf32_Phdr_t*)((paddr_t)elfFile + elfFile->e_phoff);
    v_region_t elf_reg;
    vptr_t     sect_start;
    vptr_t     sect_end;
    uint32_t   i;

    elf_reg.start = 0xffffffff;
    elf_reg.end = 0;

    /* loop through all program headers (segments) and record start/end address */
    for (i = 0; i < elfFile->e_phnum; i++) {
        if (phdr[i].p_memsz > 0) {
            sect_start = phdr[i].p_vaddr;
            sect_end = sect_start + phdr[i].p_memsz;
            if (sect_start < elf_reg.start) {
                elf_reg.start = sect_start;
            }
            if (sect_end > elf_reg.end) {
                elf_reg.end = sect_end;
            }
        }
    }

    return elf_reg;
}

BOOT_CODE void
elf32_load(Elf32_Header_t* elfFile, int32_t offset)
{
    Elf32_Phdr_t* phdr = (Elf32_Phdr_t*)((paddr_t)elfFile + elfFile->e_phoff);
    paddr_t       src;
    paddr_t       dst;
    uint32_t      len;
    uint32_t      i;

    /* loop through all program headers (segments) and load them */
    for (i = 0; i < elfFile->e_phnum; i++) {
        src = (paddr_t)elfFile + phdr[i].p_offset;
        dst = phdr[i].p_vaddr + offset;
        len = phdr[i].p_filesz;
        memcpy((void*)dst, (char*)src, len);
        dst += len;
        memset((void*)dst, 0, phdr[i].p_memsz - len);
    }
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/kernel/lock.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#if defined DEBUG || defined RELEASE_PRINTF

#include <arch/kernel/lock.h>
#include <arch/linker.h>

/* global spinlocks */
lock_t lock_debug DATA_GLOB;

#endif /* DEBUG */
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/kernel/thread.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <object.h>
#include <machine.h>
#include <arch/model/statedata.h>
#include <arch/kernel/vspace.h>
#include <arch/kernel/thread.h>
#include <arch/linker.h>

void
Arch_switchToThread(tcb_t* tcb)
{
    word_t base;

    /* set PD */
    setVMRoot(tcb);

    /* update the GDT_TLS entry with the thread's TLS_BASE address */
    base = getRegister(tcb, TLS_BASE);
    gdt_entry_gdt_data_ptr_set_base_low(ia32KSgdt + GDT_TLS, base);
    gdt_entry_gdt_data_ptr_set_base_mid(ia32KSgdt + GDT_TLS,  (base >> 16) & 0xFF);
    gdt_entry_gdt_data_ptr_set_base_high(ia32KSgdt + GDT_TLS, (base >> 24) & 0xFF);

    /* update the GDT_IPCBUF entry with the thread's IPC buffer address */
    base = tcb->tcbIPCBuffer;
    gdt_entry_gdt_data_ptr_set_base_low(ia32KSgdt + GDT_IPCBUF, base);
    gdt_entry_gdt_data_ptr_set_base_mid(ia32KSgdt + GDT_IPCBUF,  (base >> 16) & 0xFF);
    gdt_entry_gdt_data_ptr_set_base_high(ia32KSgdt + GDT_IPCBUF, (base >> 24) & 0xFF);
}

BOOT_CODE void
Arch_configureIdleThread(tcb_t* tcb)
{
    setRegister(tcb, EFLAGS, BIT(9) | BIT(1)); /* enable interrupts and set bit 1 which is always 1 */
    setRegister(tcb, NextEIP, (uint32_t)idleThreadStart);
    setRegister(tcb, CS, SEL_CS_0);
    setRegister(tcb, DS, SEL_DS_0);
    setRegister(tcb, ES, SEL_DS_0);
    setRegister(tcb, FS, SEL_DS_0);
    setRegister(tcb, GS, SEL_DS_0);
    setRegister(tcb, SS, SEL_DS_0);
}

void
Arch_switchToIdleThread(void)
{
    /* Don't need to do anything */
}

void CONST
Arch_activateIdleThread(tcb_t* tcb)
{
    /* Don't need to do anything */
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/kernel/vspace.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>
#include <api/syscall.h>
#include <machine/io.h>
#include <kernel/boot.h>
#include <kernel/cdt.h>
#include <model/statedata.h>
#include <arch/kernel/vspace.h>
#include <arch/api/invocation.h>
#include <util.h>

#ifdef CONFIG_VTX
#include <arch/object/vtx.h>

static exception_t decodeIA32EPTFrameMap(cap_t pdptCap, cte_t *cte, cap_t cap, vm_rights_t vmRights, vm_attributes_t vmAttr, word_t vaddr);

#endif

struct lookupPTSlot_ret {
    exception_t status;
    pte_t*          ptSlot;
    pte_t*          pt;
    unsigned int    ptIndex;
};
typedef struct lookupPTSlot_ret lookupPTSlot_ret_t;

/* 'gdt_idt_ptr' is declared globally because of a C-subset restriction.
 * It is only used in init_drts(), which therefore is non-reentrant.
 */
gdt_idt_ptr_t gdt_idt_ptr;

/* initialise the Task State Segment (TSS) */

BOOT_CODE static void
init_tss(tss_t* tss)
{
    tss_ptr_new(
        tss,
        0,              /* io_map_base  */
        0,              /* trap         */
        SEL_NULL,       /* sel_ldt      */
        SEL_NULL,       /* gs           */
        SEL_NULL,       /* fs           */
        SEL_NULL,       /* ds           */
        SEL_NULL,       /* ss           */
        SEL_NULL,       /* cs           */
        SEL_NULL,       /* es           */
        0,              /* edi          */
        0,              /* esi          */
        0,              /* ebp          */
        0,              /* esp          */
        0,              /* ebx          */
        0,              /* edx          */
        0,              /* ecx          */
        0,              /* eax          */
        0,              /* eflags       */
        0,              /* eip          */
        0,              /* cr3          */
        SEL_NULL,       /* ss2          */
        0,              /* esp2         */
        SEL_NULL,       /* ss1          */
        0,              /* esp1         */
        SEL_DS_0,       /* ss0          */
        0,              /* esp0         */
        0               /* prev_task    */
    );
}
/* initialise Global Descriptor Table (GDT) */

BOOT_CODE static void
init_gdt(gdt_entry_t* gdt, tss_t* tss)
{
    uint32_t tss_addr = (uint32_t)tss;

    /* Set the NULL descriptor */
    gdt[GDT_NULL] = gdt_entry_gdt_null_new();

    /* 4GB flat kernel code segment on ring 0 descriptor */
    gdt[GDT_CS_0] = gdt_entry_gdt_code_new(
                        0,      /* Base high 8 bits             */
                        1,      /* Granularity                  */
                        1,      /* Operation size               */
                        0,      /* Available                    */
                        0xf,    /* Segment limit high 4 bits    */
                        1,      /* Present                      */
                        0,      /* Descriptor privilege level   */
                        1,      /* readable                     */
                        1,      /* accessed                     */
                        0,      /* Base middle 8 bits           */
                        0,      /* Base low 16 bits             */
                        0xffff  /* Segment limit low 16 bits    */
                    );

    /* 4GB flat kernel data segment on ring 0 descriptor */
    gdt[GDT_DS_0] = gdt_entry_gdt_data_new(
                        0,      /* Base high 8 bits             */
                        1,      /* Granularity                  */
                        1,      /* Operation size               */
                        0,      /* Available                    */
                        0xf,    /* Segment limit high 4 bits    */
                        1,      /* Present                      */
                        0,      /* Descriptor privilege level   */
                        1,      /* writable                     */
                        1,      /* accessed                     */
                        0,      /* Base middle 8 bits           */
                        0,      /* Base low 16 bits             */
                        0xffff  /* Segment limit low 16 bits    */
                    );

    /* 4GB flat userland code segment on ring 3 descriptor */
    gdt[GDT_CS_3] = gdt_entry_gdt_code_new(
                        0,      /* Base high 8 bits             */
                        1,      /* Granularity                  */
                        1,      /* Operation size               */
                        0,      /* Available                    */
                        0xf,    /* Segment limit high 4 bits    */
                        1,      /* Present                      */
                        3,      /* Descriptor privilege level   */
                        1,      /* readable                     */
                        1,      /* accessed                     */
                        0,      /* Base middle 8 bits           */
                        0,      /* Base low 16 bits             */
                        0xffff  /* Segment limit low 16 bits    */
                    );

    /* 4GB flat userland data segment on ring 3 descriptor */
    gdt[GDT_DS_3] = gdt_entry_gdt_data_new(
                        0,      /* Base high 8 bits             */
                        1,      /* Granularity                  */
                        1,      /* Operation size               */
                        0,      /* Available                    */
                        0xf,    /* Segment limit high 4 bits    */
                        1,      /* Present                      */
                        3,      /* Descriptor privilege level   */
                        1,      /* writable                     */
                        1,      /* accessed                     */
                        0,      /* Base middle 8 bits           */
                        0,      /* Base low 16 bits             */
                        0xffff  /* Segment limit low 16 bits    */
                    );

    /* Task State Segment (TSS) descriptor */
    gdt[GDT_TSS] = gdt_entry_gdt_tss_new(
                       tss_addr >> 24,            /* base_high 8 bits     */
                       0,                           /* granularity          */
                       0,                           /* avl                  */
                       0,                           /* limit_high 4 bits    */
                       1,                           /* present              */
                       0,                           /* dpl                  */
                       0,                           /* busy                 */
                       1,                           /* always_true          */
                       (tss_addr >> 16) & 0xff,     /* base_mid 8 bits      */
                       (tss_addr & 0xffff),         /* base_low 16 bits     */
                       sizeof(tss_t) - 1            /* limit_low 16 bits    */
                   );

    /* pre-init the userland data segment used for TLS */
    gdt[GDT_TLS] = gdt_entry_gdt_data_new(
                       0,      /* Base high 8 bits             */
                       1,      /* Granularity                  */
                       1,      /* Operation size               */
                       0,      /* Available                    */
                       0xf,    /* Segment limit high 4 bits    */
                       1,      /* Present                      */
                       3,      /* Descriptor privilege level   */
                       1,      /* writable                     */
                       1,      /* accessed                     */
                       0,      /* Base middle 8 bits           */
                       0,      /* Base low 16 bits             */
                       0xffff  /* Segment limit low 16 bits    */
                   );

    /* pre-init the userland data segment used for the IPC buffer */
    gdt[GDT_IPCBUF] = gdt_entry_gdt_data_new(
                          0,      /* Base high 8 bits             */
                          1,      /* Granularity                  */
                          1,      /* Operation size               */
                          0,      /* Available                    */
                          0xf,    /* Segment limit high 4 bits    */
                          1,      /* Present                      */
                          3,      /* Descriptor privilege level   */
                          1,      /* writable                     */
                          1,      /* accessed                     */
                          0,      /* Base middle 8 bits           */
                          0,      /* Base low 16 bits             */
                          0xffff  /* Segment limit low 16 bits    */
                      );
}

/* initialise the Interrupt Descriptor Table (IDT) */

BOOT_CODE static void
init_idt_entry(idt_entry_t* idt, interrupt_t interrupt, void(*handler)(void))
{
    uint32_t handler_addr = (uint32_t)handler;
    uint32_t dpl = 3;

    if (interrupt < int_trap_min) {
        dpl = 0;
    }

    idt[interrupt] = idt_entry_interrupt_gate_new(
                         handler_addr >> 16,   /* offset_high  */
                         1,                    /* present      */
                         dpl,                  /* dpl          */
                         1,                    /* gate_size    */
                         SEL_CS_0,             /* seg_selector */
                         handler_addr & 0xffff /* offset_low   */
                     );
}

BOOT_CODE static void
init_idt(idt_entry_t* idt)
{
    init_idt_entry(idt, 0x00, int_00);
    init_idt_entry(idt, 0x01, int_01);
    init_idt_entry(idt, 0x02, int_02);
    init_idt_entry(idt, 0x03, int_03);
    init_idt_entry(idt, 0x04, int_04);
    init_idt_entry(idt, 0x05, int_05);
    init_idt_entry(idt, 0x06, int_06);
    init_idt_entry(idt, 0x07, int_07);
    init_idt_entry(idt, 0x08, int_08);
    init_idt_entry(idt, 0x09, int_09);
    init_idt_entry(idt, 0x0a, int_0a);
    init_idt_entry(idt, 0x0b, int_0b);
    init_idt_entry(idt, 0x0c, int_0c);
    init_idt_entry(idt, 0x0d, int_0d);
    init_idt_entry(idt, 0x0e, int_0e);
    init_idt_entry(idt, 0x0f, int_0f);

    init_idt_entry(idt, 0x10, int_10);
    init_idt_entry(idt, 0x11, int_11);
    init_idt_entry(idt, 0x12, int_12);
    init_idt_entry(idt, 0x13, int_13);
    init_idt_entry(idt, 0x14, int_14);
    init_idt_entry(idt, 0x15, int_15);
    init_idt_entry(idt, 0x16, int_16);
    init_idt_entry(idt, 0x17, int_17);
    init_idt_entry(idt, 0x18, int_18);
    init_idt_entry(idt, 0x19, int_19);
    init_idt_entry(idt, 0x1a, int_1a);
    init_idt_entry(idt, 0x1b, int_1b);
    init_idt_entry(idt, 0x1c, int_1c);
    init_idt_entry(idt, 0x1d, int_1d);
    init_idt_entry(idt, 0x1e, int_1e);
    init_idt_entry(idt, 0x1f, int_1f);

    init_idt_entry(idt, 0x20, int_20);
    init_idt_entry(idt, 0x21, int_21);
    init_idt_entry(idt, 0x22, int_22);
    init_idt_entry(idt, 0x23, int_23);
    init_idt_entry(idt, 0x24, int_24);
    init_idt_entry(idt, 0x25, int_25);
    init_idt_entry(idt, 0x26, int_26);
    init_idt_entry(idt, 0x27, int_27);
    init_idt_entry(idt, 0x28, int_28);
    init_idt_entry(idt, 0x29, int_29);
    init_idt_entry(idt, 0x2a, int_2a);
    init_idt_entry(idt, 0x2b, int_2b);
    init_idt_entry(idt, 0x2c, int_2c);
    init_idt_entry(idt, 0x2d, int_2d);
    init_idt_entry(idt, 0x2e, int_2e);
    init_idt_entry(idt, 0x2f, int_2f);

    init_idt_entry(idt, 0x30, int_30);
    init_idt_entry(idt, 0x31, int_31);
    init_idt_entry(idt, 0x32, int_32);
    init_idt_entry(idt, 0x33, int_33);
    init_idt_entry(idt, 0x34, int_34);
    init_idt_entry(idt, 0x35, int_35);
    init_idt_entry(idt, 0x36, int_36);
    init_idt_entry(idt, 0x37, int_37);
    init_idt_entry(idt, 0x38, int_38);
    init_idt_entry(idt, 0x39, int_39);
    init_idt_entry(idt, 0x3a, int_3a);
    init_idt_entry(idt, 0x3b, int_3b);
    init_idt_entry(idt, 0x3c, int_3c);
    init_idt_entry(idt, 0x3d, int_3d);
    init_idt_entry(idt, 0x3e, int_3e);
    init_idt_entry(idt, 0x3f, int_3f);

    init_idt_entry(idt, 0x40, int_40);
    init_idt_entry(idt, 0x41, int_41);
    init_idt_entry(idt, 0x42, int_42);
    init_idt_entry(idt, 0x43, int_43);
    init_idt_entry(idt, 0x44, int_44);
    init_idt_entry(idt, 0x45, int_45);
    init_idt_entry(idt, 0x46, int_46);
    init_idt_entry(idt, 0x47, int_47);
    init_idt_entry(idt, 0x48, int_48);
    init_idt_entry(idt, 0x49, int_49);
    init_idt_entry(idt, 0x4a, int_4a);
    init_idt_entry(idt, 0x4b, int_4b);
    init_idt_entry(idt, 0x4c, int_4c);
    init_idt_entry(idt, 0x4d, int_4d);
    init_idt_entry(idt, 0x4e, int_4e);
    init_idt_entry(idt, 0x4f, int_4f);

    init_idt_entry(idt, 0x50, int_50);
    init_idt_entry(idt, 0x51, int_51);
    init_idt_entry(idt, 0x52, int_52);
    init_idt_entry(idt, 0x53, int_53);
    init_idt_entry(idt, 0x54, int_54);
    init_idt_entry(idt, 0x55, int_55);
    init_idt_entry(idt, 0x56, int_56);
    init_idt_entry(idt, 0x57, int_57);
    init_idt_entry(idt, 0x58, int_58);
    init_idt_entry(idt, 0x59, int_59);
    init_idt_entry(idt, 0x5a, int_5a);
    init_idt_entry(idt, 0x5b, int_5b);
    init_idt_entry(idt, 0x5c, int_5c);
    init_idt_entry(idt, 0x5d, int_5d);
    init_idt_entry(idt, 0x5e, int_5e);
    init_idt_entry(idt, 0x5f, int_5f);

    init_idt_entry(idt, 0x60, int_60);
    init_idt_entry(idt, 0x61, int_61);
    init_idt_entry(idt, 0x62, int_62);
    init_idt_entry(idt, 0x63, int_63);
    init_idt_entry(idt, 0x64, int_64);
    init_idt_entry(idt, 0x65, int_65);
    init_idt_entry(idt, 0x66, int_66);
    init_idt_entry(idt, 0x67, int_67);
    init_idt_entry(idt, 0x68, int_68);
    init_idt_entry(idt, 0x69, int_69);
    init_idt_entry(idt, 0x6a, int_6a);
    init_idt_entry(idt, 0x6b, int_6b);
    init_idt_entry(idt, 0x6c, int_6c);
    init_idt_entry(idt, 0x6d, int_6d);
    init_idt_entry(idt, 0x6e, int_6e);
    init_idt_entry(idt, 0x6f, int_6f);

    init_idt_entry(idt, 0x70, int_70);
    init_idt_entry(idt, 0x71, int_71);
    init_idt_entry(idt, 0x72, int_72);
    init_idt_entry(idt, 0x73, int_73);
    init_idt_entry(idt, 0x74, int_74);
    init_idt_entry(idt, 0x75, int_75);
    init_idt_entry(idt, 0x76, int_76);
    init_idt_entry(idt, 0x77, int_77);
    init_idt_entry(idt, 0x78, int_78);
    init_idt_entry(idt, 0x79, int_79);
    init_idt_entry(idt, 0x7a, int_7a);
    init_idt_entry(idt, 0x7b, int_7b);
    init_idt_entry(idt, 0x7c, int_7c);
    init_idt_entry(idt, 0x7d, int_7d);
    init_idt_entry(idt, 0x7e, int_7e);
    init_idt_entry(idt, 0x7f, int_7f);

    init_idt_entry(idt, 0x80, int_80);
    init_idt_entry(idt, 0x81, int_81);
    init_idt_entry(idt, 0x82, int_82);
    init_idt_entry(idt, 0x83, int_83);
    init_idt_entry(idt, 0x84, int_84);
    init_idt_entry(idt, 0x85, int_85);
    init_idt_entry(idt, 0x86, int_86);
    init_idt_entry(idt, 0x87, int_87);
    init_idt_entry(idt, 0x88, int_88);
    init_idt_entry(idt, 0x89, int_89);
    init_idt_entry(idt, 0x8a, int_8a);
    init_idt_entry(idt, 0x8b, int_8b);
    init_idt_entry(idt, 0x8c, int_8c);
    init_idt_entry(idt, 0x8d, int_8d);
    init_idt_entry(idt, 0x8e, int_8e);
    init_idt_entry(idt, 0x8f, int_8f);

    init_idt_entry(idt, 0x90, int_90);
    init_idt_entry(idt, 0x91, int_91);
    init_idt_entry(idt, 0x92, int_92);
    init_idt_entry(idt, 0x93, int_93);
    init_idt_entry(idt, 0x94, int_94);
    init_idt_entry(idt, 0x95, int_95);
    init_idt_entry(idt, 0x96, int_96);
    init_idt_entry(idt, 0x97, int_97);
    init_idt_entry(idt, 0x98, int_98);
    init_idt_entry(idt, 0x99, int_99);
    init_idt_entry(idt, 0x9a, int_9a);
    init_idt_entry(idt, 0x9b, int_9b);
    init_idt_entry(idt, 0x9c, int_9c);
    init_idt_entry(idt, 0x9d, int_9d);
    init_idt_entry(idt, 0x9e, int_9e);
    init_idt_entry(idt, 0x9f, int_9f);

    init_idt_entry(idt, 0xa0, int_a0);
    init_idt_entry(idt, 0xa1, int_a1);
    init_idt_entry(idt, 0xa2, int_a2);
    init_idt_entry(idt, 0xa3, int_a3);
    init_idt_entry(idt, 0xa4, int_a4);
    init_idt_entry(idt, 0xa5, int_a5);
    init_idt_entry(idt, 0xa6, int_a6);
    init_idt_entry(idt, 0xa7, int_a7);
    init_idt_entry(idt, 0xa8, int_a8);
    init_idt_entry(idt, 0xa9, int_a9);
    init_idt_entry(idt, 0xaa, int_aa);
    init_idt_entry(idt, 0xab, int_ab);
    init_idt_entry(idt, 0xac, int_ac);
    init_idt_entry(idt, 0xad, int_ad);
    init_idt_entry(idt, 0xae, int_ae);
    init_idt_entry(idt, 0xaf, int_af);

    init_idt_entry(idt, 0xb0, int_b0);
    init_idt_entry(idt, 0xb1, int_b1);
    init_idt_entry(idt, 0xb2, int_b2);
    init_idt_entry(idt, 0xb3, int_b3);
    init_idt_entry(idt, 0xb4, int_b4);
    init_idt_entry(idt, 0xb5, int_b5);
    init_idt_entry(idt, 0xb6, int_b6);
    init_idt_entry(idt, 0xb7, int_b7);
    init_idt_entry(idt, 0xb8, int_b8);
    init_idt_entry(idt, 0xb9, int_b9);
    init_idt_entry(idt, 0xba, int_ba);
    init_idt_entry(idt, 0xbb, int_bb);
    init_idt_entry(idt, 0xbc, int_bc);
    init_idt_entry(idt, 0xbd, int_bd);
    init_idt_entry(idt, 0xbe, int_be);
    init_idt_entry(idt, 0xbf, int_bf);

    init_idt_entry(idt, 0xc0, int_c0);
    init_idt_entry(idt, 0xc1, int_c1);
    init_idt_entry(idt, 0xc2, int_c2);
    init_idt_entry(idt, 0xc3, int_c3);
    init_idt_entry(idt, 0xc4, int_c4);
    init_idt_entry(idt, 0xc5, int_c5);
    init_idt_entry(idt, 0xc6, int_c6);
    init_idt_entry(idt, 0xc7, int_c7);
    init_idt_entry(idt, 0xc8, int_c8);
    init_idt_entry(idt, 0xc9, int_c9);
    init_idt_entry(idt, 0xca, int_ca);
    init_idt_entry(idt, 0xcb, int_cb);
    init_idt_entry(idt, 0xcc, int_cc);
    init_idt_entry(idt, 0xcd, int_cd);
    init_idt_entry(idt, 0xce, int_ce);
    init_idt_entry(idt, 0xcf, int_cf);

    init_idt_entry(idt, 0xd0, int_d0);
    init_idt_entry(idt, 0xd1, int_d1);
    init_idt_entry(idt, 0xd2, int_d2);
    init_idt_entry(idt, 0xd3, int_d3);
    init_idt_entry(idt, 0xd4, int_d4);
    init_idt_entry(idt, 0xd5, int_d5);
    init_idt_entry(idt, 0xd6, int_d6);
    init_idt_entry(idt, 0xd7, int_d7);
    init_idt_entry(idt, 0xd8, int_d8);
    init_idt_entry(idt, 0xd9, int_d9);
    init_idt_entry(idt, 0xda, int_da);
    init_idt_entry(idt, 0xdb, int_db);
    init_idt_entry(idt, 0xdc, int_dc);
    init_idt_entry(idt, 0xdd, int_dd);
    init_idt_entry(idt, 0xde, int_de);
    init_idt_entry(idt, 0xdf, int_df);

    init_idt_entry(idt, 0xe0, int_e0);
    init_idt_entry(idt, 0xe1, int_e1);
    init_idt_entry(idt, 0xe2, int_e2);
    init_idt_entry(idt, 0xe3, int_e3);
    init_idt_entry(idt, 0xe4, int_e4);
    init_idt_entry(idt, 0xe5, int_e5);
    init_idt_entry(idt, 0xe6, int_e6);
    init_idt_entry(idt, 0xe7, int_e7);
    init_idt_entry(idt, 0xe8, int_e8);
    init_idt_entry(idt, 0xe9, int_e9);
    init_idt_entry(idt, 0xea, int_ea);
    init_idt_entry(idt, 0xeb, int_eb);
    init_idt_entry(idt, 0xec, int_ec);
    init_idt_entry(idt, 0xed, int_ed);
    init_idt_entry(idt, 0xee, int_ee);
    init_idt_entry(idt, 0xef, int_ef);

    init_idt_entry(idt, 0xf0, int_f0);
    init_idt_entry(idt, 0xf1, int_f1);
    init_idt_entry(idt, 0xf2, int_f2);
    init_idt_entry(idt, 0xf3, int_f3);
    init_idt_entry(idt, 0xf4, int_f4);
    init_idt_entry(idt, 0xf5, int_f5);
    init_idt_entry(idt, 0xf6, int_f6);
    init_idt_entry(idt, 0xf7, int_f7);
    init_idt_entry(idt, 0xf8, int_f8);
    init_idt_entry(idt, 0xf9, int_f9);
    init_idt_entry(idt, 0xfa, int_fa);
    init_idt_entry(idt, 0xfb, int_fb);
    init_idt_entry(idt, 0xfc, int_fc);
    init_idt_entry(idt, 0xfd, int_fd);
    init_idt_entry(idt, 0xfe, int_fe);
    init_idt_entry(idt, 0xff, int_ff);
}

BOOT_CODE bool_t
map_kernel_window(
    pdpte_t*   pdpt,
    pde_t*     pd,
    pte_t*     pt,
    p_region_t ndks_p_reg
#ifdef CONFIG_IRQ_IOAPIC
    , uint32_t num_ioapic,
    paddr_t*   ioapic_paddrs
#endif
#ifdef CONFIG_IOMMU
    , uint32_t   num_drhu,
    paddr_t*   drhu_list
#endif
)
{
    paddr_t  phys;
    uint32_t idx;
    pde_t    pde;
    pte_t    pte;
    unsigned int UNUSED i;

    if ((void*)pdpt != (void*)pd) {
        for (idx = 0; idx < BIT(PDPT_BITS); idx++) {
            pdpte_ptr_new(pdpt + idx,
                          pptr_to_paddr(pd + (idx * BIT(PD_BITS))),
                          0, /* avl*/
                          0, /* cache_disabled */
                          0, /* write_through */
                          1  /* present */
                         );
        }
    }

    /* Mapping of PPTR_BASE (virtual address) to kernel's PADDR_BASE
     * up to end of virtual address space except for the last large page.
     */
    phys = PADDR_BASE;
    idx = PPTR_BASE >> LARGE_PAGE_BITS;

#ifdef CONFIG_BENCHMARK
    /* steal the last large for logging */
    while (idx < BIT(PD_BITS + PDPT_BITS) - 2) {
#else
    while (idx < BIT(PD_BITS + PDPT_BITS) - 1) {
#endif /* CONFIG_BENCHMARK */
        pde = pde_pde_large_new(
                  phys,   /* page_base_address    */
                  0,      /* pat                  */
                  0,      /* avl_cte_depth        */
                  1,      /* global               */
                  0,      /* dirty                */
                  0,      /* accessed             */
                  0,      /* cache_disabled       */
                  0,      /* write_through        */
                  0,      /* super_user           */
                  1,      /* read_write           */
                  1       /* present              */
              );
        pd[idx] = pde;
        phys += BIT(LARGE_PAGE_BITS);
        idx++;
    }

    /* crosscheck whether we have mapped correctly so far */
    assert(phys == PADDR_TOP);

#ifdef CONFIG_BENCHMARK
    /* mark the address of the log. We will map it
        * in later with the correct attributes, but we need
        * to wait until we can call alloc_region. */
    ksLog = (word_t *) paddr_to_pptr(phys);
    phys += BIT(LARGE_PAGE_BITS);
    assert(idx == IA32_KSLOG_IDX);
    idx++;
#endif /* CONFIG_BENCHMARK */

    /* map page table of last 4M of virtual address space to page directory */
    pde = pde_pde_small_new(
              pptr_to_paddr(pt), /* pt_base_address  */
              0,                 /* avl_cte_Depth    */
              0,                 /* accessed         */
              0,                 /* cache_disabled   */
              0,                 /* write_through    */
              1,                 /* super_user       */
              1,                 /* read_write       */
              1                  /* present          */
          );
    pd[idx] = pde;

    /* Start with an empty guard page preceding the stack. */
    idx = 0;
    pte = pte_new(
              0,      /* page_base_address    */
              0,      /* avl                  */
              0,      /* global               */
              0,      /* pat                  */
              0,      /* dirty                */
              0,      /* accessed             */
              0,      /* cache_disabled       */
              0,      /* write_through        */
              0,      /* super_user           */
              0,      /* read_write           */
              0       /* present              */
          );
    pt[idx] = pte;
    idx++;

    /* establish NDKS (node kernel state) mappings in page table */
    phys = ndks_p_reg.start;
    while (idx - 1 < (ndks_p_reg.end - ndks_p_reg.start) >> PAGE_BITS) {
        pte = pte_new(
                  phys,   /* page_base_address    */
                  0,      /* avl_cte_depth        */
                  1,      /* global               */
                  0,      /* pat                  */
                  0,      /* dirty                */
                  0,      /* accessed             */
                  0,      /* cache_disabled       */
                  0,      /* write_through        */
                  0,      /* super_user           */
                  1,      /* read_write           */
                  1       /* present              */
              );
        pt[idx] = pte;
        phys += BIT(PAGE_BITS);
        idx++;
    }

    /* null mappings up to PPTR_KDEV */

    while (idx < (PPTR_KDEV & MASK(LARGE_PAGE_BITS)) >> PAGE_BITS) {
        pte = pte_new(
                  0,      /* page_base_address    */
                  0,      /* avl_cte_depth        */
                  0,      /* global               */
                  0,      /* pat                  */
                  0,      /* dirty                */
                  0,      /* accessed             */
                  0,      /* cache_disabled       */
                  0,      /* write_through        */
                  0,      /* super_user           */
                  0,      /* read_write           */
                  0       /* present              */
              );
        pt[idx] = pte;
        phys += BIT(PAGE_BITS);
        idx++;
    }

    /* map kernel devices (devices only used by the kernel) */

    /* map kernel devices: APIC */
    phys = apic_get_base_paddr();
    if (!phys) {
        return false;
    }
    pte = pte_new(
              phys,   /* page_base_address    */
              0,      /* avl_cte_depth        */
              1,      /* global               */
              0,      /* pat                  */
              0,      /* dirty                */
              0,      /* accessed             */
              1,      /* cache_disabled       */
              1,      /* write_through        */
              0,      /* super_user           */
              1,      /* read_write           */
              1       /* present              */
          );

    assert(idx == (PPTR_APIC & MASK(LARGE_PAGE_BITS)) >> PAGE_BITS);
    pt[idx] = pte;
    idx++;

#ifdef CONFIG_IRQ_IOAPIC
    for (i = 0; i < num_ioapic; i++) {
        phys = ioapic_paddrs[i];
        pte = pte_new(
                  phys,   /* page_base_address    */
                  0,      /* avl                  */
                  1,      /* global               */
                  0,      /* pat                  */
                  0,      /* dirty                */
                  0,      /* accessed             */
                  1,      /* cache_disabled       */
                  1,      /* write_through        */
                  0,      /* super_user           */
                  1,      /* read_write           */
                  1       /* present              */
              );
        assert(idx == ( (PPTR_IOAPIC_START + i * BIT(PAGE_BITS)) & MASK(LARGE_PAGE_BITS)) >> PAGE_BITS);
        pt[idx] = pte;
        idx++;
        if (idx == BIT(PT_BITS)) {
            return false;
        }
    }
    /* put in null mappings for any extra IOAPICs */
    for (; i < CONFIG_MAX_NUM_IOAPIC; i++) {
        pte = pte_new(
                  0,      /* page_base_address    */
                  0,      /* avl                  */
                  0,      /* global               */
                  0,      /* pat                  */
                  0,      /* dirty                */
                  0,      /* accessed             */
                  0,      /* cache_disabled       */
                  0,      /* write_through        */
                  0,      /* super_user           */
                  0,      /* read_write           */
                  0       /* present              */
              );
        assert(idx == ( (PPTR_IOAPIC_START + i * BIT(PAGE_BITS)) & MASK(LARGE_PAGE_BITS)) >> PAGE_BITS);
        pt[idx] = pte;
        idx++;
    }
#endif

#ifdef CONFIG_IOMMU
    /* map kernel devices: IOMMUs */
    for (i = 0; i < num_drhu; i++) {
        phys = (paddr_t)drhu_list[i];
        pte = pte_new(
                  phys,   /* page_base_address    */
                  0,      /* avl_cte_depth        */
                  1,      /* global               */
                  0,      /* pat                  */
                  0,      /* dirty                */
                  0,      /* accessed             */
                  1,      /* cache_disabled       */
                  1,      /* write_through        */
                  0,      /* super_user           */
                  1,      /* read_write           */
                  1       /* present              */
              );

        assert(idx == ((PPTR_DRHU_START + i * BIT(PAGE_BITS)) & MASK(LARGE_PAGE_BITS)) >> PAGE_BITS);
        pt[idx] = pte;
        idx++;
        if (idx == BIT(PT_BITS)) {
            return false;
        }
    }
#endif

    /* mark unused kernel-device pages as 'not present' */
    while (idx < BIT(PT_BITS)) {
        pte = pte_new(
                  0,      /* page_base_address    */
                  0,      /* avl_cte_depth        */
                  0,      /* global               */
                  0,      /* pat                  */
                  0,      /* dirty                */
                  0,      /* accessed             */
                  0,      /* cache_disabled       */
                  0,      /* write_through        */
                  0,      /* super_user           */
                  0,      /* read_write           */
                  0       /* present              */
              );
        pt[idx] = pte;
        idx++;
    }

    /* Check we haven't added too many kernel-device mappings.*/
    assert(idx == BIT(PT_BITS));

    invalidatePageStructureCache();
    return true;
}

BOOT_CODE void
map_it_pt_cap(cap_t pt_cap)
{
    pde_t* pd   = PDE_PTR(cap_page_table_cap_get_capPTMappedObject(pt_cap));
    pte_t* pt   = PTE_PTR(cap_page_table_cap_get_capPTBasePtr(pt_cap));
    uint32_t index = cap_page_table_cap_get_capPTMappedIndex(pt_cap);

    /* Assume the capabilities for the initial page tables are at a depth of 0 */
    pde_pde_small_ptr_new(
        pd + index,
        pptr_to_paddr(pt), /* pt_base_address */
        0,                 /* avl_cte_depth   */
        0,                 /* accessed        */
        0,                 /* cache_disabled  */
        0,                 /* write_through   */
        1,                 /* super_user      */
        1,                 /* read_write      */
        1                  /* present         */
    );
    invalidatePageStructureCache();
}

BOOT_CODE void
map_it_frame_cap(cap_t frame_cap)
{
    pte_t *pt;
    pte_t *targetSlot;
    uint32_t index;
    void *frame = (void*)cap_frame_cap_get_capFBasePtr(frame_cap);

    pt = PT_PTR(cap_frame_cap_get_capFMappedObject(frame_cap));
    index = cap_frame_cap_get_capFMappedIndex(frame_cap);
    targetSlot = pt + index;
    /* Assume the capabilities for the inital frames are at a depth of 0 */
    pte_ptr_new(
        targetSlot,
        pptr_to_paddr(frame), /* page_base_address */
        0,                    /* avl_cte_depth     */
        0,                    /* global            */
        0,                    /* pat               */
        0,                    /* dirty             */
        0,                    /* accessed          */
        0,                    /* cache_disabled    */
        0,                    /* write_through     */
        1,                    /* super_user        */
        1,                    /* read_write        */
        1                     /* present           */
    );
    invalidatePageStructureCache();
}

/* Note: this function will invalidate any pointers previously returned from this function */
BOOT_CODE void*
map_temp_boot_page(void* entry, uint32_t large_pages)
{
    void* replacement_vaddr;
    unsigned int i;
    unsigned int offset_in_page;

    unsigned int phys_pg_start = (unsigned int)(entry) & ~MASK(LARGE_PAGE_BITS);
    unsigned int virt_pd_start = (PPTR_BASE >> LARGE_PAGE_BITS) - large_pages;
    unsigned int virt_pg_start = PPTR_BASE - (large_pages << LARGE_PAGE_BITS);

    for (i = 0; i < large_pages; ++i) {
        unsigned int pg_offset = i << LARGE_PAGE_BITS; // num pages since start * page size

        pde_pde_large_ptr_new(get_boot_pd() + virt_pd_start + i,
                              phys_pg_start + pg_offset, /* physical address */
                              0, /* pat            */
                              0, /* avl            */
                              1, /* global         */
                              0, /* dirty          */
                              0, /* accessed       */
                              0, /* cache_disabled */
                              0, /* write_through  */
                              0, /* super_user     */
                              1, /* read_write     */
                              1  /* present        */
                             );
        invalidateTLBentry(virt_pg_start + pg_offset);
    }

    // assign replacement virtual addresses page
    offset_in_page = (unsigned int)(entry) & MASK(LARGE_PAGE_BITS);
    replacement_vaddr = (void*)(virt_pg_start + offset_in_page);

    invalidatePageStructureCache();

    return replacement_vaddr;
}

BOOT_CODE bool_t
init_vm_state(pdpte_t *kernel_pdpt, pde_t* kernel_pd, pte_t* kernel_pt)
{
    ia32KScacheLineSizeBits = getCacheLineSizeBits();
    if (!ia32KScacheLineSizeBits) {
        return false;
    }
    ia32KSkernelPDPT = kernel_pdpt;
    ia32KSkernelPD = kernel_pd;
    ia32KSkernelPT = kernel_pt;
    init_tss(&ia32KStss);
    init_gdt(ia32KSgdt, &ia32KStss);
    init_idt(ia32KSidt);
    return true;
}

/* initialise CPU's descriptor table registers (GDTR, IDTR, LDTR, TR) */

BOOT_CODE void
init_dtrs(void)
{
    /* setup the GDT pointer and limit and load into GDTR */
    gdt_idt_ptr.limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) - 1;
    gdt_idt_ptr.basel = (uint32_t)ia32KSgdt;
    gdt_idt_ptr.baseh = (uint16_t)((uint32_t)ia32KSgdt >> 16);
    ia32_install_gdt(&gdt_idt_ptr);

    /* setup the IDT pointer and limit and load into IDTR */
    gdt_idt_ptr.limit = (sizeof(idt_entry_t) * (int_max + 1)) - 1;
    gdt_idt_ptr.basel = (uint32_t)ia32KSidt;
    gdt_idt_ptr.baseh = (uint16_t)((uint32_t)ia32KSidt >> 16);
    ia32_install_idt(&gdt_idt_ptr);

    /* load NULL LDT selector into LDTR */
    ia32_install_ldt(SEL_NULL);

    /* load TSS selector into Task Register (TR) */
    ia32_install_tss(SEL_TSS);
}

BOOT_CODE bool_t
init_pat_msr(void)
{
    ia32_pat_msr_t pat_msr;
    /* First verify PAT is supported by the machine.
     *      See section 11.12.1 of Volume 3 of the Intel manual */
    if ( (ia32_cpuid_edx(0x1, 0x0) & BIT(16)) == 0) {
        printf("PAT support not found\n");
        return false;
    }
    pat_msr.words[0] = ia32_rdmsr_low(IA32_PAT_MSR);
    pat_msr.words[1] = ia32_rdmsr_high(IA32_PAT_MSR);
    /* Set up the PAT MSR to the Intel defaults, just in case
     * they have been changed but a bootloader somewhere along the way */
    ia32_pat_msr_ptr_set_pa0(&pat_msr, IA32_PAT_MT_WRITE_BACK);
    ia32_pat_msr_ptr_set_pa1(&pat_msr, IA32_PAT_MT_WRITE_THROUGH);
    ia32_pat_msr_ptr_set_pa2(&pat_msr, IA32_PAT_MT_UNCACHED);
    ia32_pat_msr_ptr_set_pa3(&pat_msr, IA32_PAT_MT_UNCACHEABLE);
    /* Add the WriteCombining cache type to the PAT */
    ia32_pat_msr_ptr_set_pa4(&pat_msr, IA32_PAT_MT_WRITE_COMBINING);
    ia32_wrmsr(IA32_PAT_MSR, pat_msr.words[1], pat_msr.words[0]);
    return true;
}

/* ==================== BOOT CODE FINISHES HERE ==================== */

static uint32_t CONST WritableFromVMRights(vm_rights_t vm_rights)
{
    switch (vm_rights) {
    case VMReadOnly:
        return 0;

    case VMKernelOnly:
    case VMReadWrite:
        return 1;

    default:
        fail("Invalid VM rights");
    }
}

static uint32_t CONST SuperUserFromVMRights(vm_rights_t vm_rights)
{
    switch (vm_rights) {
    case VMKernelOnly:
        return 0;

    case VMReadOnly:
    case VMReadWrite:
        return 1;

    default:
        fail("Invalid VM rights");
    }
}

static pde_t CONST makeUserPDE(paddr_t paddr, vm_attributes_t vm_attr, vm_rights_t vm_rights, uint32_t avl)
{
    return pde_pde_large_new(
               paddr,                                          /* page_base_address    */
               vm_attributes_get_ia32PATBit(vm_attr),          /* pat                  */
               avl,                                            /* avl_cte_depth        */
               0,                                              /* global               */
               0,                                              /* dirty                */
               0,                                              /* accessed             */
               vm_attributes_get_ia32PCDBit(vm_attr),          /* cache_disabled       */
               vm_attributes_get_ia32PWTBit(vm_attr),          /* write_through        */
               SuperUserFromVMRights(vm_rights),               /* super_user           */
               WritableFromVMRights(vm_rights),                /* read_write           */
               1                                               /* present              */
           );
}

static pte_t CONST makeUserPTE(paddr_t paddr, vm_attributes_t vm_attr, vm_rights_t vm_rights, uint32_t avl)
{
    return pte_new(
               paddr,                                          /* page_base_address    */
               avl,                                            /* avl_cte_depth        */
               0,                                              /* global               */
               vm_attributes_get_ia32PATBit(vm_attr),          /* pat                  */
               0,                                              /* dirty                */
               0,                                              /* accessed             */
               vm_attributes_get_ia32PCDBit(vm_attr),          /* cache_disabled       */
               vm_attributes_get_ia32PWTBit(vm_attr),          /* write_through        */
               SuperUserFromVMRights(vm_rights),               /* super_user           */
               WritableFromVMRights(vm_rights),                /* read_write           */
               1                                               /* present              */
           );
}

word_t* PURE lookupIPCBuffer(bool_t isReceiver, tcb_t *thread)
{
    word_t      w_bufferPtr;
    cap_t       bufferCap;
    vm_rights_t vm_rights;

    w_bufferPtr = thread->tcbIPCBuffer;
    bufferCap = TCB_PTR_CTE_PTR(thread, tcbBuffer)->cap;

    if (cap_get_capType(bufferCap) != cap_frame_cap) {
        return NULL;
    }

    vm_rights = cap_frame_cap_get_capFVMRights(bufferCap);
    if (vm_rights == VMReadWrite || (!isReceiver && vm_rights == VMReadOnly)) {
        word_t basePtr;
        unsigned int pageBits;

        basePtr = cap_frame_cap_get_capFBasePtr(bufferCap);
        pageBits = pageBitsForSize(cap_frame_cap_get_capFSize(bufferCap));
        return (word_t *)(basePtr + (w_bufferPtr & MASK(pageBits)));
    } else {
        return NULL;
    }
}

static lookupPTSlot_ret_t lookupPTSlot(void *vspace, vptr_t vptr)
{
    lookupPTSlot_ret_t ret;
    lookupPDSlot_ret_t pdSlot;

    pdSlot = lookupPDSlot(vspace, vptr);
    if (pdSlot.status != EXCEPTION_NONE) {
        ret.ptSlot = NULL;
        ret.status = pdSlot.status;
        return ret;
    }

    if ((pde_ptr_get_page_size(pdSlot.pdSlot) != pde_pde_small) ||
            !pde_pde_small_ptr_get_present(pdSlot.pdSlot)) {
        current_lookup_fault = lookup_fault_missing_capability_new(PAGE_BITS + PT_BITS);

        ret.ptSlot = NULL;
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    } else {
        pte_t* pt;
        pte_t* ptSlot;
        unsigned int ptIndex;

        pt = paddr_to_pptr(pde_pde_small_ptr_get_pt_base_address(pdSlot.pdSlot));
        ptIndex = (vptr >> PAGE_BITS) & MASK(PT_BITS);
        ptSlot = pt + ptIndex;

        ret.pt = pt;
        ret.ptIndex = ptIndex;
        ret.ptSlot = ptSlot;
        ret.status = EXCEPTION_NONE;
        return ret;
    }
}

exception_t handleVMFault(tcb_t* thread, vm_fault_type_t vm_faultType)
{
    uint32_t addr;
    uint32_t fault;

    addr = getFaultAddr();
    fault = getRegister(thread, Error);

    switch (vm_faultType) {
    case IA32DataFault:
        current_fault = fault_vm_fault_new(addr, fault, false);
        return EXCEPTION_FAULT;

    case IA32InstructionFault:
        current_fault = fault_vm_fault_new(addr, fault, true);
        return EXCEPTION_FAULT;

    default:
        fail("Invalid VM fault type");
    }
}

exception_t checkValidIPCBuffer(vptr_t vptr, cap_t cap)
{
    if (cap_get_capType(cap) != cap_frame_cap) {
        userError("IPC Buffer is an invalid cap.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (!inKernelWindow((void *)cap_frame_cap_get_capFBasePtr(cap))) {
        userError("IPC Buffer must in the kernel window.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (!IS_ALIGNED(vptr, 9)) {
        userError("IPC Buffer vaddr 0x%x is not aligned.", (int)vptr);
        current_syscall_error.type = seL4_AlignmentError;
        return EXCEPTION_SYSCALL_ERROR;
    }

    return EXCEPTION_NONE;
}

vm_rights_t CONST maskVMRights(vm_rights_t vm_rights, cap_rights_t cap_rights_mask)
{
    if (vm_rights == VMReadOnly && cap_rights_get_capAllowRead(cap_rights_mask)) {
        return VMReadOnly;
    }
    if (vm_rights == VMReadWrite && cap_rights_get_capAllowRead(cap_rights_mask)) {
        if (!cap_rights_get_capAllowWrite(cap_rights_mask)) {
            return VMReadOnly;
        } else {
            return VMReadWrite;
        }
    }
    return VMKernelOnly;
}

static void flushTable(void *vspace, uint32_t pdIndex, pte_t *pt)
{
    cap_t        threadRoot;

    /* check if page table belongs to current address space */
    threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
    if (isValidNativeRoot(threadRoot) && (void*)pptr_of_cap(threadRoot) == vspace) {
        invalidateTLB();
        invalidatePageStructureCache();
    }
}

void setVMRoot(tcb_t* tcb)
{
    cap_t threadRoot;
    void *vspace_root;

    threadRoot = TCB_PTR_CTE_PTR(tcb, tcbVTable)->cap;

    vspace_root = getValidNativeRoot(threadRoot);
    if (!vspace_root) {
        setCurrentPD(pptr_to_paddr(ia32KSkernelPDPT));
        return;
    }

    /* only set PD if we change it, otherwise we flush the TLB needlessly */
    if (getCurrentPD() != pptr_to_paddr(vspace_root)) {
        setCurrentPD(pptr_to_paddr(vspace_root));
    }
}

void unmapAllPageTables(pde_t *pd)
{
    uint32_t i, max;
    assert(pd);

#ifdef CONFIG_PAE_PAGING
    max = BIT(PD_BITS);
#else
    max = PPTR_USER_TOP >> LARGE_PAGE_BITS;
#endif
    for (i = 0; i < max; i++) {
        if (pde_ptr_get_page_size(pd + i) == pde_pde_small && pde_pde_small_ptr_get_present(pd + i)) {
            pte_t *pt = PT_PTR(paddr_to_pptr(pde_pde_small_ptr_get_pt_base_address(pd + i)));
            cte_t *ptCte;
            cap_t ptCap;
            ptCte = cdtFindAtDepth(cap_page_table_cap_new(PD_REF(pd), i, PT_REF(pt)), pde_pde_small_ptr_get_avl_cte_depth(pd + i));
            assert(ptCte);

            ptCap = ptCte->cap;
            ptCap = cap_page_table_cap_set_capPTMappedObject(ptCap, 0);
            cdtUpdate(ptCte, ptCap);
        } else if (pde_ptr_get_page_size(pd + i) == pde_pde_large && pde_pde_large_ptr_get_present(pd + i)) {
            void *frame = paddr_to_pptr(pde_pde_large_ptr_get_page_base_address(pd + i));
            cte_t *frameCte;
            cap_t frameCap;
            frameCte = cdtFindAtDepth(cap_frame_cap_new(IA32_LargePage, PD_REF(pd), i, IA32_MAPPING_PD, 0, (uint32_t)frame), pde_pde_large_ptr_get_avl_cte_depth(pd + i));
            assert(frameCte);
            frameCap = cap_frame_cap_set_capFMappedObject(frameCte->cap, 0);
            cdtUpdate(frameCte, frameCap);
        }
    }
}

void unmapAllPages(pte_t *pt)
{
    cte_t* frameCte;
    cap_t newCap;
    uint32_t i;

    for (i = 0; i < BIT(PT_BITS); i++) {
        if (pte_ptr_get_present(pt + i)) {
            frameCte = cdtFindAtDepth(cap_frame_cap_new(IA32_SmallPage, PT_REF(pt), i, IA32_MAPPING_PD, 0, (uint32_t)paddr_to_pptr(pte_ptr_get_page_base_address(pt + i))), pte_ptr_get_avl_cte_depth(pt + i));
            assert(frameCte);
            newCap = cap_frame_cap_set_capFMappedObject(frameCte->cap, 0);
            cdtUpdate(frameCte, newCap);
        }
    }
}

void unmapPageTable(pde_t* pd, uint32_t pdIndex)
{
    pd[pdIndex] = pde_pde_small_new(
                      0,  /* pt_base_address  */
                      0,  /* avl_cte_Depth    */
                      0,  /* accessed         */
                      0,  /* cache_disabled   */
                      0,  /* write_through    */
                      0,  /* super_user       */
                      0,  /* read_write       */
                      0   /* present          */
                  );
}

void unmapPageSmall(pte_t *pt, uint32_t ptIndex)
{
    pt[ptIndex] = pte_new(
                      0,      /* page_base_address    */
                      0,      /* avl_cte_depth        */
                      0,      /* global               */
                      0,      /* pat                  */
                      0,      /* dirty                */
                      0,      /* accessed             */
                      0,      /* cache_disabled       */
                      0,      /* write_through        */
                      0,      /* super_user           */
                      0,      /* read_write           */
                      0       /* present              */
                  );
}

void unmapPageLarge(pde_t *pd, uint32_t pdIndex)
{
    pd[pdIndex] = pde_pde_large_new(
                      0,      /* page_base_address    */
                      0,      /* pat                  */
                      0,      /* avl_cte_depth        */
                      0,      /* global               */
                      0,      /* dirty                */
                      0,      /* accessed             */
                      0,      /* cache_disabled       */
                      0,      /* write_through        */
                      0,      /* super_user           */
                      0,      /* read_write           */
                      0       /* present              */
                  );
}

static exception_t
performPageGetAddress(void *vbase_ptr)
{
    paddr_t capFBasePtr;

    /* Get the physical address of this frame. */
    capFBasePtr = pptr_to_paddr(vbase_ptr);

    /* return it in the first message register */
    setRegister(ksCurThread, msgRegisters[0], capFBasePtr);
    setRegister(ksCurThread, msgInfoRegister,
                wordFromMessageInfo(message_info_new(0, 0, 0, 1)));

    return EXCEPTION_NONE;
}

static inline bool_t
checkVPAlignment(vm_page_size_t sz, word_t w)
{
    return IS_ALIGNED(w, pageBitsForSize(sz));
}

static exception_t
decodeIA32PageTableInvocation(
    word_t label,
    unsigned int length,
    cte_t* cte, cap_t cap,
    extra_caps_t extraCaps,
    word_t* buffer
)
{
    word_t          vaddr;
    vm_attributes_t attr;
    lookupPDSlot_ret_t pdSlot;
    cap_t           vspaceCap;
    void*           vspace;
    pde_t           pde;
    pde_t *         pd;
    paddr_t         paddr;

    if (label == IA32PageTableUnmap) {
        setThreadState(ksCurThread, ThreadState_Restart);

        pd = PDE_PTR(cap_page_table_cap_get_capPTMappedObject(cap));
        if (pd) {
            pte_t *pt = PTE_PTR(cap_page_table_cap_get_capPTBasePtr(cap));
            uint32_t pdIndex = cap_page_table_cap_get_capPTMappedIndex(cap);
            unmapPageTable(pd, pdIndex);
            flushTable(pd, pdIndex, pt);
            clearMemory((void *)pt, cap_get_capSizeBits(cap));
        }
        cdtUpdate(cte, cap_page_table_cap_set_capPTMappedObject(cap, 0));

        return EXCEPTION_NONE;
    }

    if (label != IA32PageTableMap ) {
        userError("IA32PageTable: Illegal operation.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (length < 2 || extraCaps.excaprefs[0] == NULL) {
        userError("IA32PageTable: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (cdtFindWithExtra(cap)) {
        userError("IA32PageTable: Page table is already mapped to a page directory.");
        current_syscall_error.type =
            seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;

        return EXCEPTION_SYSCALL_ERROR;
    }

    vaddr = getSyscallArg(0, buffer) & (~MASK(PT_BITS + PAGE_BITS));
    attr = vmAttributesFromWord(getSyscallArg(1, buffer));
    vspaceCap = extraCaps.excaprefs[0]->cap;

    if (!isValidNativeRoot(vspaceCap)) {
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 1;

        return EXCEPTION_SYSCALL_ERROR;
    }

    vspace = (void*)pptr_of_cap(vspaceCap);

    if (vaddr >= PPTR_USER_TOP) {
        userError("IA32PageTable: Mapping address too high.");
        current_syscall_error.type = seL4_InvalidArgument;
        current_syscall_error.invalidArgumentNumber = 0;

        return EXCEPTION_SYSCALL_ERROR;
    }

    pdSlot = lookupPDSlot(vspace, vaddr);
    if (pdSlot.status != EXCEPTION_NONE) {
        current_syscall_error.type = seL4_FailedLookup;
        current_syscall_error.failedLookupWasSource = false;

        return EXCEPTION_SYSCALL_ERROR;
    }

    if (((pde_ptr_get_page_size(pdSlot.pdSlot) == pde_pde_small) && pde_pde_small_ptr_get_present(pdSlot.pdSlot)) ||
            ((pde_ptr_get_page_size(pdSlot.pdSlot) == pde_pde_large) && pde_pde_large_ptr_get_present(pdSlot.pdSlot))) {
        current_syscall_error.type = seL4_DeleteFirst;

        return EXCEPTION_SYSCALL_ERROR;
    }

    paddr = pptr_to_paddr(PTE_PTR(cap_page_table_cap_get_capPTBasePtr(cap)));
    pde = pde_pde_small_new(
              paddr,                                      /* pt_base_address  */
              mdb_node_get_cdtDepth(cte->cteMDBNode),     /* avl_cte_depth    */
              0,                                          /* accessed         */
              vm_attributes_get_ia32PCDBit(attr),         /* cache_disabled   */
              vm_attributes_get_ia32PWTBit(attr),         /* write_through    */
              1,                                          /* super_user       */
              1,                                          /* read_write       */
              1                                           /* present          */
          );

    cap = cap_page_table_cap_set_capPTMappedObject(cap, PD_REF(pdSlot.pd));
    cap = cap_page_table_cap_set_capPTMappedIndex(cap, pdSlot.pdIndex);

    cdtUpdate(cte, cap);
    *pdSlot.pdSlot = pde;

    setThreadState(ksCurThread, ThreadState_Restart);
    invalidatePageStructureCache();
    return EXCEPTION_NONE;
}

static exception_t
decodeIA32PDFrameMap(
    cap_t vspaceCap,
    cte_t *cte,
    cap_t cap,
    vm_rights_t vmRights,
    vm_attributes_t vmAttr,
    word_t vaddr)
{
    void * vspace;
    word_t vtop;
    paddr_t paddr;
    vm_page_size_t  frameSize;

    frameSize = cap_frame_cap_get_capFSize(cap);

    vtop = vaddr + BIT(pageBitsForSize(frameSize));

    vspace = (void*)pptr_of_cap(vspaceCap);

    if (vtop > PPTR_USER_TOP) {
        userError("IA32Frame: Mapping address too high.");
        current_syscall_error.type = seL4_InvalidArgument;
        current_syscall_error.invalidArgumentNumber = 0;

        return EXCEPTION_SYSCALL_ERROR;
    }

    paddr = pptr_to_paddr((void*)cap_frame_cap_get_capFBasePtr(cap));

    switch (frameSize) {
        /* PTE mappings */
    case IA32_SmallPage: {
        lookupPTSlot_ret_t lu_ret;

        lu_ret = lookupPTSlot(vspace, vaddr);

        if (lu_ret.status != EXCEPTION_NONE) {
            current_syscall_error.type = seL4_FailedLookup;
            current_syscall_error.failedLookupWasSource = false;
            /* current_lookup_fault will have been set by lookupPTSlot */
            return EXCEPTION_SYSCALL_ERROR;
        }
        if (pte_get_present(*lu_ret.ptSlot)) {
            userError("IA32FrameMap: Mapping already present");
            current_syscall_error.type = seL4_DeleteFirst;
            return EXCEPTION_SYSCALL_ERROR;
        }

        cap = cap_frame_cap_set_capFMappedObject(cap, PT_REF(lu_ret.pt));
        cap = cap_frame_cap_set_capFMappedIndex(cap, lu_ret.ptIndex);
        cap = cap_frame_cap_set_capFMappedType(cap, IA32_MAPPING_PD);
        cdtUpdate(cte, cap);
        *lu_ret.ptSlot = makeUserPTE(paddr, vmAttr, vmRights, mdb_node_get_cdtDepth(cte->cteMDBNode));

        invalidatePageStructureCache();
        setThreadState(ksCurThread, ThreadState_Restart);
        return EXCEPTION_NONE;
    }

    /* PDE mappings */
    case IA32_LargePage: {
        lookupPDSlot_ret_t lu_ret;

        lu_ret = lookupPDSlot(vspace, vaddr);
        if (lu_ret.status != EXCEPTION_NONE) {
            current_syscall_error.type = seL4_FailedLookup;
            current_syscall_error.failedLookupWasSource = false;
            /* current_lookup_fault will have been set by lookupPDSlot */
            return EXCEPTION_SYSCALL_ERROR;
        }
        if ( (pde_ptr_get_page_size(lu_ret.pdSlot) == pde_pde_small && (pde_pde_small_ptr_get_present(lu_ret.pdSlot))) ||
                (pde_ptr_get_page_size(lu_ret.pdSlot) == pde_pde_large && (pde_pde_large_ptr_get_present(lu_ret.pdSlot)))) {
            userError("IA32FrameMap: Mapping already present");
            current_syscall_error.type = seL4_DeleteFirst;

            return EXCEPTION_SYSCALL_ERROR;
        }

        *lu_ret.pdSlot = makeUserPDE(paddr, vmAttr, vmRights, mdb_node_get_cdtDepth(cte->cteMDBNode));
        cap = cap_frame_cap_set_capFMappedObject(cap, PD_REF(lu_ret.pd));
        cap = cap_frame_cap_set_capFMappedIndex(cap, lu_ret.pdIndex);
        cap = cap_frame_cap_set_capFMappedType(cap, IA32_MAPPING_PD);
        cdtUpdate(cte, cap);

        invalidatePageStructureCache();
        setThreadState(ksCurThread, ThreadState_Restart);
        return EXCEPTION_NONE;
    }

    default:
        fail("Invalid page type");
    }
}

static void IA32PageUnmapPD(cap_t cap)
{
    void *object = (void*)cap_frame_cap_get_capFMappedObject(cap);
    uint32_t index = cap_frame_cap_get_capFMappedIndex(cap);
    switch (cap_frame_cap_get_capFSize(cap)) {
    case IA32_SmallPage:
        unmapPageSmall(PTE_PTR(object), index);
        flushPageSmall(PTE_PTR(object), index);
        break;
    case IA32_LargePage:
        unmapPageLarge(PDE_PTR(object), index);
        flushPageLarge(PDE_PTR(object), index);
        break;
    default:
        fail("Invalid page type");
    }
}

static exception_t
decodeIA32FrameInvocation(
    word_t label,
    unsigned int length,
    cte_t* cte,
    cap_t cap,
    extra_caps_t extraCaps,
    word_t* buffer
)
{
    switch (label) {
    case IA32PageMap: { /* Map */
        word_t          vaddr;
        word_t          w_rightsMask;
        cap_t           vspaceCap;
        vm_rights_t     capVMRights;
        vm_rights_t     vmRights;
        vm_attributes_t vmAttr;
        vm_page_size_t  frameSize;

        if (length < 3 || extraCaps.excaprefs[0] == NULL) {
            userError("IA32Frame: Truncated message");
            current_syscall_error.type = seL4_TruncatedMessage;

            return EXCEPTION_SYSCALL_ERROR;
        }

        vaddr = getSyscallArg(0, buffer);
        w_rightsMask = getSyscallArg(1, buffer);
        vmAttr = vmAttributesFromWord(getSyscallArg(2, buffer));
        vspaceCap = extraCaps.excaprefs[0]->cap;

        frameSize = cap_frame_cap_get_capFSize(cap);
        capVMRights = cap_frame_cap_get_capFVMRights(cap);

        if (cap_frame_cap_get_capFMappedObject(cap) != 0) {
            userError("IA32Frame: Frame already mapped.");
            current_syscall_error.type = seL4_InvalidCapability;
            current_syscall_error.invalidCapNumber = 0;

            return EXCEPTION_SYSCALL_ERROR;
        }

        vmRights = maskVMRights(capVMRights, rightsFromWord(w_rightsMask));

        if (!checkVPAlignment(frameSize, vaddr)) {
            userError("IA32Frame: Alignment error when mapping");
            current_syscall_error.type = seL4_AlignmentError;

            return EXCEPTION_SYSCALL_ERROR;
        }

        if (isVTableRoot(vspaceCap)) {
            return decodeIA32PDFrameMap(vspaceCap, cte, cap, vmRights, vmAttr, vaddr);
#ifdef CONFIG_VTX
        } else if (cap_get_capType(vspaceCap) == cap_ept_page_directory_pointer_table_cap) {
            return decodeIA32EPTFrameMap(vspaceCap, cte, cap, vmRights, vmAttr, vaddr);
#endif
        } else {
            userError("IA32Frame: Attempting to map frame into invalid page directory cap.");
            current_syscall_error.type = seL4_InvalidCapability;
            current_syscall_error.invalidCapNumber = 1;

            return EXCEPTION_SYSCALL_ERROR;
        }


        return EXCEPTION_NONE;
    }

    case IA32PageUnmap: { /* Unmap */
        if (cap_frame_cap_get_capFMappedObject(cap)) {
            switch (cap_frame_cap_get_capFMappedType(cap)) {
            case IA32_MAPPING_PD:
                IA32PageUnmapPD(cap);
                break;
#ifdef CONFIG_VTX
            case IA32_MAPPING_EPT:
                IA32PageUnmapEPT(cap);
                break;
#endif
#ifdef CONFIG_IOMMU
            case IA32_MAPPING_IO:
                unmapIOPage(cap);
                break;
#endif
            default:
                fail("Unknown mapping type for frame");
            }
        }
        cap = cap_frame_cap_set_capFMappedObject(cap, 0);
        cdtUpdate(cte, cap);

        setThreadState(ksCurThread, ThreadState_Restart);
        return EXCEPTION_NONE;
    }

#ifdef CONFIG_IOMMU
    case IA32PageMapIO: { /* MapIO */
        return decodeIA32IOMapInvocation(label, length, cte, cap, extraCaps, buffer);
    }
#endif

    case IA32PageGetAddress: {
        /* Return it in the first message register. */
        assert(n_msgRegisters >= 1);

        setThreadState(ksCurThread, ThreadState_Restart);
        return performPageGetAddress((void*)cap_frame_cap_get_capFBasePtr(cap));
    }

    default:
        current_syscall_error.type = seL4_IllegalOperation;

        return EXCEPTION_SYSCALL_ERROR;
    }
}

exception_t
decodeIA32MMUInvocation(
    word_t label,
    unsigned int length,
    cptr_t cptr,
    cte_t* cte,
    cap_t cap,
    extra_caps_t extraCaps,
    word_t* buffer
)
{
    switch (cap_get_capType(cap)) {
    case cap_pdpt_cap:
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;

    case cap_page_directory_cap:
        return decodeIA32PageDirectoryInvocation(label, length, cte, cap, extraCaps, buffer);

    case cap_page_table_cap:
        return decodeIA32PageTableInvocation(label, length, cte, cap, extraCaps, buffer);

    case cap_frame_cap:
        return decodeIA32FrameInvocation(label, length, cte, cap, extraCaps, buffer);

    default:
        fail("Invalid arch cap type");
    }
}

#ifdef CONFIG_VTX
struct lookupEPTPDSlot_ret {
    exception_t status;
    ept_pde_t*  pd;
    uint32_t    pdIndex;
};
typedef struct lookupEPTPDSlot_ret lookupEPTPDSlot_ret_t;

struct lookupEPTPTSlot_ret {
    exception_t status;
    ept_pte_t*  pt;
    uint32_t    ptIndex;
};
typedef struct lookupEPTPTSlot_ret lookupEPTPTSlot_ret_t;

static ept_pdpte_t* CONST lookupEPTPDPTSlot(ept_pdpte_t *pdpt, vptr_t vptr)
{
    unsigned int pdptIndex;

    pdptIndex = vptr >> (PAGE_BITS + EPT_PT_BITS + EPT_PD_BITS);
    return pdpt + pdptIndex;
}

static lookupEPTPDSlot_ret_t lookupEPTPDSlot(ept_pdpte_t* pdpt, vptr_t vptr)
{
    lookupEPTPDSlot_ret_t ret;
    ept_pdpte_t* pdptSlot;

    pdptSlot = lookupEPTPDPTSlot(pdpt, vptr);

    if (!ept_pdpte_ptr_get_read(pdptSlot)) {
        current_lookup_fault = lookup_fault_missing_capability_new(22);

        ret.pd = NULL;
        ret.pdIndex = 0;
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    } else {
        ret.pd = paddr_to_pptr(ept_pdpte_ptr_get_pd_base_address(pdptSlot));
        ret.pdIndex = (vptr >> (PAGE_BITS + EPT_PT_BITS)) & MASK(EPT_PD_BITS);

        ret.status = EXCEPTION_NONE;
        return ret;
    }
}

static lookupEPTPTSlot_ret_t lookupEPTPTSlot(ept_pdpte_t* pdpt, vptr_t vptr)
{
    lookupEPTPTSlot_ret_t ret;
    lookupEPTPDSlot_ret_t lu_ret;
    ept_pde_t *pdSlot;

    lu_ret = lookupEPTPDSlot(pdpt, vptr);
    if (lu_ret.status != EXCEPTION_NONE) {
        current_syscall_error.type = seL4_FailedLookup;
        current_syscall_error.failedLookupWasSource = false;
        /* current_lookup_fault will have been set by lookupEPTPDSlot */
        ret.pt = NULL;
        ret.ptIndex = 0;
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    }

    pdSlot = lu_ret.pd + lu_ret.pdIndex;

    if ((ept_pde_ptr_get_page_size(pdSlot) != ept_pde_ept_pde_4k) ||
            !ept_pde_ept_pde_4k_ptr_get_read(pdSlot)) {
        current_lookup_fault = lookup_fault_missing_capability_new(22);

        ret.pt = NULL;
        ret.ptIndex = 0;
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    } else {
        ret.pt = paddr_to_pptr(ept_pde_ept_pde_4k_ptr_get_pt_base_address(pdSlot));
        ret.ptIndex = (vptr >> (PAGE_BITS)) & MASK(EPT_PT_BITS);

        ret.status = EXCEPTION_NONE;
        return ret;
    }
}

ept_pdpte_t *lookupEPTPDPTFromPD(ept_pde_t *pd)
{
    cte_t *pd_cte;

    /* First query the cdt and find the cap */
    pd_cte = cdtFindWithExtra(cap_ept_page_directory_cap_new(0, 0, EPT_PD_REF(pd)));
    /* We will not be returned a slot if there was no 'extra' information (aka if it is not mapped) */
    if (!pd_cte) {
        return NULL;
    }

    /* Return the mapping information from the cap. */
    return EPT_PDPT_PTR(cap_ept_page_directory_cap_get_capPDMappedObject(pd_cte->cap));
}

static ept_pdpte_t *lookupEPTPDPTFromPT(ept_pte_t *pt)
{
    cte_t *pt_cte;
    cap_t pt_cap;
    ept_pde_t *pd;

    /* First query the cdt and find the cap */
    pt_cte = cdtFindWithExtra(cap_ept_page_table_cap_new(0, 0, EPT_PT_REF(pt)));
    /* We will not be returned a slot if there was no 'extra' information (aka if it is not mapped) */
    if (!pt_cte) {
        return NULL;
    }

    /* Get any mapping information from the cap */
    pt_cap = pt_cte->cap;
    pd = EPT_PD_PTR(cap_ept_page_table_cap_get_capPTMappedObject(pt_cap));
    /* If we found it then it *should* have information */
    assert(pd);
    /* Now lookup the PDPT from the PD */
    return lookupEPTPDPTFromPD(pd);
}

void unmapEPTPD(ept_pdpte_t *pdpt, uint32_t index, ept_pde_t *pd)
{
    pdpt[index] = ept_pdpte_new(
                      0,  /* pd_base_address  */
                      0,  /* avl_cte_depth    */
                      0,  /* execute          */
                      0,  /* write            */
                      0   /* read             */
                  );
}

void unmapEPTPT(ept_pde_t *pd, uint32_t index, ept_pte_t *pt)
{
    pd[index] = ept_pde_ept_pde_4k_new(
                    0,  /* pt_base_address  */
                    0,  /* avl_cte_depth    */
                    0,  /* execute          */
                    0,  /* write            */
                    0   /* read             */
                );
}

void unmapAllEPTPD(ept_pdpte_t *pdpt)
{
    uint32_t i;

    for (i = 0; i < BIT(EPT_PDPT_BITS); i++) {
        ept_pdpte_t *pdpte = pdpt + i;
        if (ept_pdpte_ptr_get_pd_base_address(pdpte)) {
            cap_t cap;
            cte_t *pdCte;

            ept_pde_t *pd = EPT_PD_PTR(paddr_to_pptr(ept_pdpte_ptr_get_pd_base_address(pdpte)));
            uint32_t depth = ept_pdpte_ptr_get_avl_cte_depth(pdpte);
            pdCte = cdtFindAtDepth(cap_ept_page_directory_cap_new(EPT_PDPT_REF(pdpt), i, EPT_PD_REF(pd)), depth);
            assert(pdCte);

            cap = pdCte->cap;
            cap = cap_ept_page_directory_cap_set_capPDMappedObject(cap, 0);
            cdtUpdate(pdCte, cap);
        }
    }
}

void unmapAllEPTPT(ept_pde_t *pd)
{
    uint32_t i;

    for (i = 0; i < BIT(EPT_PD_BITS); i++) {
        ept_pde_t *pde = pd + i;
        switch (ept_pde_ptr_get_page_size(pde)) {
        case ept_pde_ept_pde_4k:
            if (ept_pde_ept_pde_4k_ptr_get_pt_base_address(pde)) {
                cap_t cap;
                cte_t *ptCte;

                ept_pte_t *pt = EPT_PT_PTR(paddr_to_pptr(ept_pde_ept_pde_4k_ptr_get_pt_base_address(pde)));
                uint32_t depth = ept_pde_ept_pde_4k_ptr_get_avl_cte_depth(pde);
                ptCte = cdtFindAtDepth(cap_ept_page_table_cap_new(EPT_PD_REF(pd), i, EPT_PT_REF(pt)), depth);
                assert(ptCte);

                cap = ptCte->cap;
                cap = cap_ept_page_table_cap_set_capPTMappedObject(cap, 0);
                cdtUpdate(ptCte, cap);
            }
            break;
        case ept_pde_ept_pde_2m:
            if (ept_pde_ept_pde_2m_ptr_get_page_base_address(pde)) {
                cap_t newCap;
                cte_t *frameCte;

                void *frame = paddr_to_pptr(ept_pde_ept_pde_2m_ptr_get_page_base_address(pde));
                uint32_t depth = ept_pde_ept_pde_2m_ptr_get_avl_cte_depth(pde);
                frameCte = cdtFindAtDepth(cap_frame_cap_new(IA32_LargePage, EPT_PD_REF(pd), i, IA32_MAPPING_EPT, 0, (uint32_t)frame), depth);
                assert(frameCte);

                newCap = cap_frame_cap_set_capFMappedObject(frameCte->cap, 0);
                cdtUpdate(frameCte, newCap);
                if (LARGE_PAGE_BITS == IA32_4M_bits) {
                    /* If we found a 2m mapping then the next entry will be the other half
                    * of this 4M frame, so skip it */
                    i++;
                }
            }
            break;
        default:
            fail("Unknown EPT PDE page size");
        }
    }
}

void unmapAllEPTPages(ept_pte_t *pt)
{
    uint32_t i;

    for (i = 0; i < BIT(EPT_PT_BITS); i++) {
        ept_pte_t *pte = pt + i;
        if (ept_pte_ptr_get_page_base_address(pte)) {
            cap_t newCap;
            cte_t *frameCte;

            void *frame = paddr_to_pptr(ept_pte_ptr_get_page_base_address(pte));
            uint32_t depth = ept_pte_ptr_get_avl_cte_depth(pte);
            frameCte = cdtFindAtDepth(cap_frame_cap_new(IA32_SmallPage, EPT_PT_REF(pt), i, IA32_MAPPING_EPT, 0, (uint32_t)frame), depth);
            assert(frameCte);

            newCap = cap_frame_cap_set_capFMappedObject(frameCte->cap, 0);
            cdtUpdate(frameCte, newCap);
        }
    }
}

enum ept_cache_options {
    EPTUncacheable = 0,
    EPTWriteCombining = 1,
    EPTWriteThrough = 4,
    EPTWriteProtected = 5,
    EPTWriteBack = 6
};
typedef enum ept_cache_options ept_cache_options_t;

static ept_cache_options_t
eptCacheFromVmAttr(vm_attributes_t vmAttr)
{
    /* PAT cache options are 1-1 with ept_cache_options. But need to
       verify user has specified a sensible option */
    ept_cache_options_t option = vmAttr.words[0];
    if (option != EPTUncacheable ||
            option != EPTWriteCombining ||
            option != EPTWriteThrough ||
            option != EPTWriteBack) {
        /* No failure mode is supported here, vmAttr settings should be verified earlier */
        option = EPTWriteBack;
    }
    return option;
}

exception_t
decodeIA32EPTInvocation(
    word_t label,
    unsigned int length,
    cptr_t cptr,
    cte_t* cte,
    cap_t cap,
    extra_caps_t extraCaps,
    word_t* buffer
)
{
    switch (cap_get_capType(cap)) {
    case cap_ept_page_directory_pointer_table_cap:
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    case cap_ept_page_directory_cap:
        return decodeIA32EPTPageDirectoryInvocation(label, length, cte, cap, extraCaps, buffer);
    case cap_ept_page_table_cap:
        return decodeIA32EPTPageTableInvocation(label, length, cte, cap, extraCaps, buffer);
    default:
        fail("Invalid cap type");
    }
}

exception_t
decodeIA32EPTPageDirectoryInvocation(
    word_t label,
    unsigned int length,
    cte_t* cte,
    cap_t cap,
    extra_caps_t extraCaps,
    word_t* buffer
)
{
    word_t          vaddr;
    word_t          pdptIndex;
    cap_t           pdptCap;
    ept_pdpte_t*    pdpt;
    ept_pdpte_t*    pdptSlot;
    ept_pdpte_t     pdpte;
    paddr_t         paddr;


    if (label == IA32EPTPageDirectoryUnmap) {
        setThreadState(ksCurThread, ThreadState_Restart);

        pdpt = EPT_PDPT_PTR(cap_ept_page_directory_cap_get_capPDMappedObject(cap));
        if (pdpt) {
            ept_pde_t* pd = (ept_pde_t*)cap_ept_page_directory_cap_get_capPDBasePtr(cap);
            unmapEPTPD(pdpt, cap_ept_page_directory_cap_get_capPDMappedIndex(cap), pd);
            invept((void*)((uint32_t)pdpt - EPT_PDPT_OFFSET));
        }

        cap = cap_ept_page_directory_cap_set_capPDMappedObject(cap, 0);
        cdtUpdate(cte, cap);

        return EXCEPTION_NONE;
    }

    if (label != IA32EPTPageDirectoryMap) {
        userError("IA32EPTPageDirectory Illegal operation.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (length < 2 || extraCaps.excaprefs[0] == NULL) {
        userError("IA32EPTPageDirectoryMap: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (cap_ept_page_directory_cap_get_capPDMappedObject(cap)) {
        userError("IA32EPTPageDirectoryMap: EPT Page directory is already mapped to an EPT page directory pointer table.");
        current_syscall_error.type =
            seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;

        return EXCEPTION_SYSCALL_ERROR;
    }

    vaddr = getSyscallArg(0, buffer);
    pdptCap = extraCaps.excaprefs[0]->cap;

    if (cap_get_capType(pdptCap) != cap_ept_page_directory_pointer_table_cap) {
        userError("IA32EPTPageDirectoryMap: Not a valid EPT page directory pointer table.");
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 1;

        return EXCEPTION_SYSCALL_ERROR;
    }

    pdpt = (ept_pdpte_t*)cap_ept_page_directory_pointer_table_cap_get_capPDPTBasePtr(pdptCap);

    if (vaddr >= PPTR_BASE) {
        userError("IA32EPTPageDirectoryMap: vaddr not in kernel window.");
        current_syscall_error.type = seL4_InvalidArgument;
        current_syscall_error.invalidArgumentNumber = 0;

        return EXCEPTION_SYSCALL_ERROR;
    }

    pdptIndex = vaddr >> (PAGE_BITS + EPT_PD_BITS + EPT_PT_BITS);
    pdptSlot = &pdpt[pdptIndex];

    if (ept_pdpte_ptr_get_read(pdptSlot)) {
        userError("IA32EPTPageDirectoryMap: Page directory already mapped here.");
        current_syscall_error.type = seL4_DeleteFirst;
        return EXCEPTION_SYSCALL_ERROR;
    }

    paddr = pptr_to_paddr((void*)(cap_ept_page_directory_cap_get_capPDBasePtr(cap)));
    pdpte = ept_pdpte_new(
                paddr,                                      /* pd_base_address  */
                mdb_node_get_cdtDepth(cte->cteMDBNode),     /* avl_cte_depth    */
                1,                                          /* execute          */
                1,                                          /* write            */
                1                                           /* read             */
            );

    cap = cap_ept_page_directory_cap_set_capPDMappedObject(cap, EPT_PDPT_REF(pdpt));
    cap = cap_ept_page_directory_cap_set_capPDMappedIndex(cap, pdptIndex);

    cdtUpdate(cte, cap);

    *pdptSlot = pdpte;
    invept((void*)((uint32_t)pdpt - EPT_PDPT_OFFSET));

    setThreadState(ksCurThread, ThreadState_Restart);
    return EXCEPTION_NONE;
}

exception_t
decodeIA32EPTPageTableInvocation(
    word_t label,
    unsigned int length,
    cte_t* cte,
    cap_t cap,
    extra_caps_t extraCaps,
    word_t* buffer
)
{
    word_t          vaddr;
    cap_t           pdptCap;
    ept_pdpte_t*    pdpt;
    ept_pde_t*      pd;
    unsigned int    pdIndex;
    ept_pde_t*      pdSlot;
    ept_pde_t       pde;
    paddr_t         paddr;
    lookupEPTPDSlot_ret_t lu_ret;

    if (label == IA32EPTPageTableUnmap) {
        setThreadState(ksCurThread, ThreadState_Restart);

        pd = EPT_PD_PTR(cap_ept_page_table_cap_get_capPTMappedObject(cap));
        if (pd) {
            ept_pte_t* pt = (ept_pte_t*)cap_ept_page_table_cap_get_capPTBasePtr(cap);
            unmapEPTPT(pd, cap_ept_page_table_cap_get_capPTMappedIndex(cap), pt);
            pdpt = lookupEPTPDPTFromPD(pd);
            if (pdpt) {
                invept((void*)((uint32_t)pdpt - EPT_PDPT_OFFSET));
            }
        }

        cap = cap_ept_page_table_cap_set_capPTMappedObject(cap, 0);
        cdtUpdate(cte, cap);

        return EXCEPTION_NONE;
    }

    if (label != IA32EPTPageTableMap) {
        userError("IA32EPTPageTable Illegal operation.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (length < 2 || extraCaps.excaprefs[0] == NULL) {
        userError("IA32EPTPageTable: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (cap_ept_page_table_cap_get_capPTMappedObject(cap)) {
        userError("IA32EPTPageTable EPT Page table is already mapped to an EPT page directory.");
        current_syscall_error.type =
            seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;

        return EXCEPTION_SYSCALL_ERROR;
    }

    vaddr = getSyscallArg(0, buffer);
    pdptCap = extraCaps.excaprefs[0]->cap;

    if (cap_get_capType(pdptCap) != cap_ept_page_directory_pointer_table_cap) {
        userError("IA32EPTPageTableMap: Not a valid EPT page directory pointer table.");
        userError("IA32ASIDPool: Invalid vspace root.");
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 1;

        return EXCEPTION_SYSCALL_ERROR;
    }

    pdpt = (ept_pdpte_t*)(cap_ept_page_directory_pointer_table_cap_get_capPDPTBasePtr(pdptCap));

    if (vaddr >= PPTR_BASE) {
        userError("IA32EPTPageTableMap: vaddr not in kernel window.");
        current_syscall_error.type = seL4_InvalidArgument;
        current_syscall_error.invalidArgumentNumber = 0;

        return EXCEPTION_SYSCALL_ERROR;
    }

    lu_ret = lookupEPTPDSlot(pdpt, vaddr);
    if (lu_ret.status != EXCEPTION_NONE) {
        current_syscall_error.type = seL4_FailedLookup;
        current_syscall_error.failedLookupWasSource = false;
        /* current_lookup_fault will have been set by lookupPTSlot */
        return EXCEPTION_SYSCALL_ERROR;
    }

    pd = lu_ret.pd;
    pdIndex = lu_ret.pdIndex;
    pdSlot = pd + pdIndex;

    if (((ept_pde_ptr_get_page_size(pdSlot) == ept_pde_ept_pde_4k) &&
            ept_pde_ept_pde_4k_ptr_get_read(pdSlot)) ||
            ((ept_pde_ptr_get_page_size(pdSlot) == ept_pde_ept_pde_2m) &&
             ept_pde_ept_pde_2m_ptr_get_read(pdSlot))) {
        userError("IA32EPTPageTableMap: Page table already mapped here");
        current_syscall_error.type = seL4_DeleteFirst;
        return EXCEPTION_SYSCALL_ERROR;
    }

    paddr = pptr_to_paddr((void*)(cap_ept_page_table_cap_get_capPTBasePtr(cap)));
    pde = ept_pde_ept_pde_4k_new(
              paddr,                                      /* pt_base_address  */
              mdb_node_get_cdtDepth(cte->cteMDBNode),     /* avl_cte_depth    */
              1,                                          /* execute          */
              1,                                          /* write            */
              1                                           /* read             */
          );

    cap = cap_ept_page_table_cap_set_capPTMappedObject(cap, EPT_PD_REF(pd));
    cap = cap_ept_page_table_cap_set_capPTMappedIndex(cap, pdIndex);

    cdtUpdate(cte, cap);

    *pdSlot = pde;
    invept((void*)((uint32_t)pdpt - EPT_PDPT_OFFSET));

    setThreadState(ksCurThread, ThreadState_Restart);
    return EXCEPTION_NONE;
}

static exception_t
decodeIA32EPTFrameMap(
    cap_t pdptCap,
    cte_t *cte,
    cap_t cap,
    vm_rights_t vmRights,
    vm_attributes_t vmAttr,
    word_t vaddr)
{
    paddr_t         paddr;
    ept_pdpte_t*    pdpt;
    vm_page_size_t  frameSize;

    frameSize = cap_frame_cap_get_capFSize(cap);
    pdpt = (ept_pdpte_t*)(cap_ept_page_directory_pointer_table_cap_get_capPDPTBasePtr(pdptCap));

    paddr = pptr_to_paddr((void*)cap_frame_cap_get_capFBasePtr(cap));

    switch (frameSize) {
        /* PTE mappings */
    case IA32_SmallPage: {
        lookupEPTPTSlot_ret_t lu_ret;
        ept_pte_t *ptSlot;

        lu_ret = lookupEPTPTSlot(pdpt, vaddr);
        if (lu_ret.status != EXCEPTION_NONE) {
            current_syscall_error.type = seL4_FailedLookup;
            current_syscall_error.failedLookupWasSource = false;
            /* current_lookup_fault will have been set by lookupEPTPTSlot */
            return EXCEPTION_SYSCALL_ERROR;
        }

        ptSlot = lu_ret.pt + lu_ret.ptIndex;

        if (ept_pte_ptr_get_page_base_address(ptSlot) != 0) {
            userError("IA32EPTFrameMap: Mapping already present.");
            current_syscall_error.type = seL4_DeleteFirst;
            return EXCEPTION_SYSCALL_ERROR;
        }

        *ptSlot = ept_pte_new(
                      paddr,
                      mdb_node_get_cdtDepth(cte->cteMDBNode),
                      0,
                      eptCacheFromVmAttr(vmAttr),
                      1,
                      WritableFromVMRights(vmRights),
                      1);

        invept((void*)((uint32_t)pdpt - EPT_PDPT_OFFSET));

        cap = cap_frame_cap_set_capFMappedObject(cap, EPT_PT_REF(lu_ret.pt));
        cap = cap_frame_cap_set_capFMappedIndex(cap, lu_ret.ptIndex);
        cap = cap_frame_cap_set_capFMappedType(cap, IA32_MAPPING_EPT);
        cdtUpdate(cte, cap);

        setThreadState(ksCurThread, ThreadState_Restart);
        return EXCEPTION_NONE;
    }

    /* PDE mappings */
    case IA32_LargePage: {
        lookupEPTPDSlot_ret_t lu_ret;
        ept_pde_t *pdSlot;

        lu_ret = lookupEPTPDSlot(pdpt, vaddr);
        if (lu_ret.status != EXCEPTION_NONE) {
            userError("IA32EPTFrameMap: Need a page directory first.");
            current_syscall_error.type = seL4_FailedLookup;
            current_syscall_error.failedLookupWasSource = false;
            /* current_lookup_fault will have been set by lookupEPTPDSlot */
            return EXCEPTION_SYSCALL_ERROR;
        }

        pdSlot = lu_ret.pd + lu_ret.pdIndex;

        if ((ept_pde_ptr_get_page_size(pdSlot) == ept_pde_ept_pde_4k) &&
                ept_pde_ept_pde_4k_ptr_get_read(pdSlot)) {
            userError("IA32EPTFrameMap: Page table already present.");
            current_syscall_error.type = seL4_DeleteFirst;
            return EXCEPTION_SYSCALL_ERROR;
        }
        if ((ept_pde_ptr_get_page_size(pdSlot + 1) == ept_pde_ept_pde_4k) &&
                ept_pde_ept_pde_4k_ptr_get_read(pdSlot + 1)) {
            userError("IA32EPTFrameMap: Page table already present.");
            current_syscall_error.type = seL4_DeleteFirst;
            return EXCEPTION_SYSCALL_ERROR;
        }
        if ((ept_pde_ptr_get_page_size(pdSlot) == ept_pde_ept_pde_2m) &&
                ept_pde_ept_pde_2m_ptr_get_read(pdSlot)) {
            userError("IA32EPTFrameMap: Mapping already present.");
            current_syscall_error.type = seL4_DeleteFirst;
            return EXCEPTION_SYSCALL_ERROR;
        }

        if (LARGE_PAGE_BITS == IA32_4M_bits) {
            pdSlot[1] = ept_pde_ept_pde_2m_new(
                            paddr + (1 << 21),
                            mdb_node_get_cdtDepth(cte->cteMDBNode),
                            0,
                            eptCacheFromVmAttr(vmAttr),
                            1,
                            WritableFromVMRights(vmRights),
                            1);
        }
        pdSlot[0] = ept_pde_ept_pde_2m_new(
                        paddr,
                        mdb_node_get_cdtDepth(cte->cteMDBNode),
                        0,
                        eptCacheFromVmAttr(vmAttr),
                        1,
                        WritableFromVMRights(vmRights),
                        1);

        invept((void*)((uint32_t)pdpt - EPT_PDPT_OFFSET));

        cap = cap_frame_cap_set_capFMappedObject(cap, EPT_PD_REF(lu_ret.pd));
        cap = cap_frame_cap_set_capFMappedIndex(cap, lu_ret.pdIndex);
        cap = cap_frame_cap_set_capFMappedType(cap, IA32_MAPPING_EPT);
        cdtUpdate(cte, cap);

        setThreadState(ksCurThread, ThreadState_Restart);
        return EXCEPTION_NONE;
    }

    default:
        fail("Invalid page type");
    }
}

void IA32EptPdpt_Init(ept_pml4e_t * pdpt)
{
    /* Map in a PDPT for the 512GB region. */
    pdpt[0] = ept_pml4e_new(
                  pptr_to_paddr((void*)(pdpt + BIT(EPT_PDPT_BITS))),
                  1,
                  1,
                  1
              );
    invept(pdpt);
}

void IA32PageUnmapEPT(cap_t cap)
{
    void *object = (void*)cap_frame_cap_get_capFMappedObject(cap);
    uint32_t index = cap_frame_cap_get_capFMappedIndex(cap);
    ept_pdpte_t *pdpt;
    switch (cap_frame_cap_get_capFSize(cap)) {
    case IA32_SmallPage: {
        ept_pte_t *pt = EPT_PT_PTR(object);
        pt[index] = ept_pte_new(0, 0, 0, 0, 0, 0, 0);
        pdpt = lookupEPTPDPTFromPT(pt);
        break;
    }
    case IA32_LargePage: {
        ept_pde_t *pd = EPT_PD_PTR(object);
        pd[index] = ept_pde_ept_pde_2m_new(0, 0, 0, 0, 0, 0, 0);
        if (LARGE_PAGE_BITS == IA32_4M_bits) {
            pd[index + 1] = ept_pde_ept_pde_2m_new(0, 0, 0, 0, 0, 0, 0);
        }
        pdpt = lookupEPTPDPTFromPD(pd);
        break;
    }
    default:
        fail("Invalid page type");
    }
    if (pdpt) {
        invept((void*)((uint32_t)pdpt - EPT_PDPT_OFFSET));
    }
}

#endif /* VTX */
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/kernel/vspace_32.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <api/syscall.h>
#include <config.h>
#include <machine/io.h>
#include <kernel/boot.h>
#include <kernel/cspace.h>
#include <kernel/thread.h>
#include <model/statedata.h>
#include <object/cnode.h>
#include <arch/api/invocation.h>
#include <arch/kernel/apic.h>
#include <arch/kernel/vspace.h>
#include <arch/linker.h>

#ifndef CONFIG_PAE_PAGING

/* setup initial boot page directory */

/* The boot pd is referenced by code that runs before paging, so
 * place it in PHYS_DATA */
pde_t _boot_pd[BIT(PD_BITS)] __attribute__((aligned(BIT(PAGE_BITS)))) PHYS_DATA;

BOOT_CODE
pde_t *get_boot_pd()
{
    return _boot_pd;
}

/* This function is duplicated from pde_pde_large_ptr_new, generated by the
 * bitfield tool in structures_gen.h. It is required by functions that need to
 * call it before the MMU is turned on. Any changes made to the bitfield
 * generation need to be replicated here.
 */
PHYS_CODE
static inline void
pde_pde_large_ptr_new_phys(pde_t *pde_ptr, uint32_t page_base_address,
                           uint32_t pat, uint32_t avl, uint32_t global, uint32_t dirty,
                           uint32_t accessed, uint32_t cache_disabled, uint32_t write_through,
                           uint32_t super_user, uint32_t read_write, uint32_t present)
{
    pde_ptr->words[0] = 0;

    pde_ptr->words[0] |= (page_base_address & 0xffc00000) >> 0;
    pde_ptr->words[0] |= (pat & 0x1) << 12;
    pde_ptr->words[0] |= (avl & 0x7) << 9;
    pde_ptr->words[0] |= (global & 0x1) << 8;
    pde_ptr->words[0] |= (pde_pde_large & 0x1) << 7;
    pde_ptr->words[0] |= (dirty & 0x1) << 6;
    pde_ptr->words[0] |= (accessed & 0x1) << 5;
    pde_ptr->words[0] |= (cache_disabled & 0x1) << 4;
    pde_ptr->words[0] |= (write_through & 0x1) << 3;
    pde_ptr->words[0] |= (super_user & 0x1) << 2;
    pde_ptr->words[0] |= (read_write & 0x1) << 1;
    pde_ptr->words[0] |= (present & 0x1) << 0;
}

PHYS_CODE VISIBLE void
init_boot_pd(void)
{
    unsigned int i;

    /* identity mapping from 0 up to PPTR_BASE (virtual address) */
    for (i = 0; i < (PPTR_BASE >> IA32_4M_bits); i++) {
        pde_pde_large_ptr_new_phys(
            _boot_pd + i,
            i << IA32_4M_bits, /* physical address */
            0, /* pat            */
            0, /* avl            */
            1, /* global         */
            0, /* dirty          */
            0, /* accessed       */
            0, /* cache_disabled */
            0, /* write_through  */
            0, /* super_user     */
            1, /* read_write     */
            1  /* present        */
        );
    }

    /* mapping of PPTR_BASE (virtual address) to PADDR_BASE up to end of virtual address space */
    for (i = 0; i < ((-PPTR_BASE) >> IA32_4M_bits); i++) {
        pde_pde_large_ptr_new_phys(
            _boot_pd + i + (PPTR_BASE >> IA32_4M_bits),
            (i << IA32_4M_bits) + PADDR_BASE, /* physical address */
            0, /* pat            */
            0, /* avl            */
            1, /* global         */
            0, /* dirty          */
            0, /* accessed       */
            0, /* cache_disabled */
            0, /* write_through  */
            0, /* super_user     */
            1, /* read_write     */
            1  /* present        */
        );
    }
}

BOOT_CODE void
map_it_pd_cap(cap_t pd_cap)
{
    /* this shouldn't be called, and it does nothing */
    fail("Should not be called");
}

/* ==================== BOOT CODE FINISHES HERE ==================== */

lookupPDSlot_ret_t lookupPDSlot(void *vspace, vptr_t vptr)
{
    lookupPDSlot_ret_t pdSlot;
    pde_t *pd = PDE_PTR(vspace);
    unsigned int pdIndex;

    pdIndex = vptr >> (PAGE_BITS + PT_BITS);
    pdSlot.status = EXCEPTION_NONE;
    pdSlot.pdSlot = pd + pdIndex;
    pdSlot.pd = pd;
    pdSlot.pdIndex = pdIndex;
    return pdSlot;
}

bool_t CONST isVTableRoot(cap_t cap)
{
    return cap_get_capType(cap) == cap_page_directory_cap;
}

bool_t CONST isValidNativeRoot(cap_t cap)
{
    return isVTableRoot(cap);
}

bool_t CONST isValidVTableRoot(cap_t cap)
{
#ifdef CONFIG_VTX
    if (cap_get_capType(cap) == cap_ept_page_directory_pointer_table_cap) {
        return true;
    }
#endif
    return isValidNativeRoot(cap);
}

void *getValidNativeRoot(cap_t vspace_cap)
{
    if (isValidNativeRoot(vspace_cap)) {
        return PDE_PTR(cap_page_directory_cap_get_capPDBasePtr(vspace_cap));
    }
    return NULL;
}

void copyGlobalMappings(void* new_vspace)
{
    unsigned int i;
    pde_t *newPD = (pde_t*)new_vspace;

    for (i = PPTR_BASE >> IA32_4M_bits; i < BIT(PD_BITS); i++) {
        newPD[i] = ia32KSkernelPD[i];
    }
}

void unmapPageDirectory(pdpte_t *pdpt, uint32_t pdptIndex, pde_t *pd)
{
}

void unmapAllPageDirectories(pdpte_t *pdpt)
{
}

void flushAllPageDirectories(pdpte_t *pdpt)
{
}

void flushPageSmall(pte_t *pt, uint32_t ptIndex)
{
    cap_t threadRoot;
    cte_t *ptCte;
    pde_t *pd;
    uint32_t pdIndex;

    /* We know this pt can only be mapped into one single pd. So
     * lets find a cap with that mapping information */
    ptCte = cdtFindWithExtra(cap_page_table_cap_new(0, 0, PT_REF(pt)));
    if (ptCte) {
        pd = PD_PTR(cap_page_table_cap_get_capPTMappedObject(ptCte->cap));
        pdIndex = cap_page_table_cap_get_capPTMappedIndex(ptCte->cap);

        /* check if page belongs to current address space */
        threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
        if (isValidNativeRoot(threadRoot) && (void*)pptr_of_cap(threadRoot) == pd) {
            invalidateTLBentry( (pdIndex << (PT_BITS + PAGE_BITS)) | (ptIndex << PAGE_BITS));
            invalidatePageStructureCache();
        }
    }
}

void flushPageLarge(pde_t *pd, uint32_t pdIndex)
{
    cap_t               threadRoot;

    /* check if page belongs to current address space */
    threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
    if (cap_get_capType(threadRoot) == cap_page_directory_cap &&
            PDE_PTR(cap_page_directory_cap_get_capPDBasePtr(threadRoot)) == pd) {
        invalidateTLBentry(pdIndex << (PT_BITS + PAGE_BITS));
        invalidatePageStructureCache();
    }
}

void flushAllPageTables(pde_t *pd)
{
    cap_t threadRoot;
    /* check if this is the current address space */
    threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
    if (cap_get_capType(threadRoot) == cap_page_directory_cap &&
            PDE_PTR(cap_page_directory_cap_get_capPDBasePtr(threadRoot)) == pd) {
        invalidateTLB();
    }
    invalidatePageStructureCache();
}

void flushPageDirectory(pdpte_t *pdpt, uint32_t pdptIndex, pde_t *pd)
{
    flushAllPageTables(pd);
}

exception_t
decodeIA32PageDirectoryInvocation(
    word_t label,
    unsigned int length,
    cte_t* cte,
    cap_t cap,
    extra_caps_t extraCaps,
    word_t* buffer
)
{
    current_syscall_error.type = seL4_IllegalOperation;
    return EXCEPTION_SYSCALL_ERROR;
}

#endif
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/kernel/vspace_pae.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>
#include <machine/io.h>
#include <kernel/boot.h>
#include <model/statedata.h>
#include <arch/kernel/vspace.h>

#ifdef CONFIG_PAE_PAGING

/* The boot pd is referenced by code that runs before paging, so
 * place it in PHYS_DATA. In PAE mode the top level is actually
 * a PDPTE, but we call it _boot_pd for compatibility */
pdpte_t _boot_pd[BIT(PDPT_BITS)] __attribute__((aligned(BIT(PAGE_BITS)))) PHYS_DATA;
/* Allocate enough page directories to fill every slot in the PDPT */
pde_t _boot_pds[BIT(PD_BITS + PDPT_BITS)] __attribute__((aligned(BIT(PAGE_BITS)))) PHYS_DATA;

BOOT_CODE
pde_t *get_boot_pd()
{
    /* return a pointer to the continus array of boot pds */
    return _boot_pds;
}

/* These functions arefrom what is generated by the bitfield tool. It is
 * required by functions that need to call it before the MMU is turned on.
 * Any changes made to the bitfield generation need to be replicated here.
 */
PHYS_CODE
static inline void
pdpte_ptr_new_phys(pdpte_t *pdpte_ptr, uint32_t pd_base_address, uint32_t avl, uint32_t cache_disabled, uint32_t write_through, uint32_t present)
{
    pdpte_ptr->words[0] = 0;
    pdpte_ptr->words[1] = 0;

    pdpte_ptr->words[0] |= (pd_base_address & 0xfffff000) >> 0;
    pdpte_ptr->words[0] |= (avl & 0x7) << 9;
    pdpte_ptr->words[0] |= (cache_disabled & 0x1) << 4;
    pdpte_ptr->words[0] |= (write_through & 0x1) << 3;
    pdpte_ptr->words[0] |= (present & 0x1) << 0;
}

PHYS_CODE
static inline void
pde_pde_large_ptr_new_phys(pde_t *pde_ptr, uint32_t page_base_address,
                           uint32_t pat, uint32_t avl, uint32_t global, uint32_t dirty,
                           uint32_t accessed, uint32_t cache_disabled, uint32_t write_through,
                           uint32_t super_user, uint32_t read_write, uint32_t present)
{
    pde_ptr->words[0] = 0;
    pde_ptr->words[1] = 0;

    pde_ptr->words[0] |= (page_base_address & 0xffe00000) >> 0;
    pde_ptr->words[0] |= (pat & 0x1) << 12;
    pde_ptr->words[0] |= (avl & 0x7) << 9;
    pde_ptr->words[0] |= (global & 0x1) << 8;
    pde_ptr->words[0] |= ((uint32_t)pde_pde_large & 0x1) << 7;
    pde_ptr->words[0] |= (dirty & 0x1) << 6;
    pde_ptr->words[0] |= (accessed & 0x1) << 5;
    pde_ptr->words[0] |= (cache_disabled & 0x1) << 4;
    pde_ptr->words[0] |= (write_through & 0x1) << 3;
    pde_ptr->words[0] |= (super_user & 0x1) << 2;
    pde_ptr->words[0] |= (read_write & 0x1) << 1;
    pde_ptr->words[0] |= (present & 0x1) << 0;

}

PHYS_CODE VISIBLE void
init_boot_pd(void)
{
    unsigned int i;

    /* first map in all the pds into the pdpt */
    for (i = 0; i < BIT(PDPT_BITS); i++) {
        uint32_t pd_base = (uint32_t)&_boot_pds[i * BIT(PD_BITS)];
        pdpte_ptr_new_phys(
            _boot_pd + i,
            pd_base,    /* pd_base_address */
            0,          /* avl */
            0,          /* cache_disabled */
            0,          /* write_through */
            1           /* present */
        );
    }

    /* identity mapping from 0 up to PPTR_BASE (virtual address) */
    for (i = 0; (i << IA32_2M_bits) < PPTR_BASE; i++) {
        pde_pde_large_ptr_new_phys(
            _boot_pds + i,
            i << IA32_2M_bits, /* physical address */
            0, /* pat            */
            0, /* avl            */
            1, /* global         */
            0, /* dirty          */
            0, /* accessed       */
            0, /* cache_disabled */
            0, /* write_through  */
            0, /* super_user     */
            1, /* read_write     */
            1  /* present        */
        );
    }

    /* mapping of PPTR_BASE (virtual address) to PADDR_BASE up to end of virtual address space */
    for (i = 0; (i << IA32_2M_bits) < -PPTR_BASE; i++) {
        pde_pde_large_ptr_new_phys(
            _boot_pds + i + (PPTR_BASE >> IA32_2M_bits),
            (i << IA32_2M_bits) + PADDR_BASE, /* physical address */
            0, /* pat            */
            0, /* avl            */
            1, /* global         */
            0, /* dirty          */
            0, /* accessed       */
            0, /* cache_disabled */
            0, /* write_through  */
            0, /* super_user     */
            1, /* read_write     */
            1  /* present        */
        );
    }
}

BOOT_CODE void
map_it_pd_cap(cap_t pd_cap)
{
    pdpte_t *pdpt = PDPTE_PTR(cap_page_directory_cap_get_capPDMappedObject(pd_cap));
    pde_t *pd = PDE_PTR(cap_page_directory_cap_get_capPDBasePtr(pd_cap));
    uint32_t index = cap_page_directory_cap_get_capPDMappedIndex(pd_cap);

    pdpte_ptr_new(
        pdpt + index,
        pptr_to_paddr(pd),
        0, /* avl */
        0, /* cache_disabled */
        0, /* write_through */
        1  /* present */
    );
    invalidatePageStructureCache();
}

/* ==================== BOOT CODE FINISHES HERE ==================== */

void copyGlobalMappings(void* new_vspace)
{
    unsigned int i;
    pdpte_t *pdpt = (pdpte_t*)new_vspace;

    for (i = PPTR_BASE >> IA32_1G_bits; i < BIT(PDPT_BITS); i++) {
        pdpt[i] = ia32KSkernelPDPT[i];
    }
}

bool_t CONST isVTableRoot(cap_t cap)
{
    return cap_get_capType(cap) == cap_pdpt_cap;
}

bool_t CONST isValidNativeRoot(cap_t cap)
{
    return isVTableRoot(cap);
}

bool_t CONST isValidVTableRoot(cap_t cap)
{
#ifdef CONFIG_VTX
    if (cap_get_capType(cap) == cap_ept_page_directory_pointer_table_cap) {
        return true;
    }
#endif
    return isValidNativeRoot(cap);
}

void *getValidNativeRoot(cap_t vspace_cap)
{
    if (isValidNativeRoot(vspace_cap)) {
        return PDPTE_PTR(cap_pdpt_cap_get_capPDPTBasePtr(vspace_cap));
    }
    return NULL;
}

static inline pdpte_t *lookupPDPTSlot(void *vspace, vptr_t vptr)
{
    pdpte_t *pdpt = PDPT_PTR(vspace);
    return pdpt + (vptr >> IA32_1G_bits);
}

lookupPDSlot_ret_t lookupPDSlot(void *vspace, vptr_t vptr)
{
    pdpte_t *pdptSlot;
    lookupPDSlot_ret_t ret;

    pdptSlot = lookupPDPTSlot(vspace, vptr);

    if (!pdpte_ptr_get_present(pdptSlot)) {
        current_lookup_fault = lookup_fault_missing_capability_new(PAGE_BITS + PT_BITS + PD_BITS);
        ret.pdSlot = NULL;
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    } else {
        pde_t *pd;
        pde_t *pdSlot;
        unsigned int pdIndex;

        pd = paddr_to_pptr(pdpte_ptr_get_pd_base_address(pdptSlot));
        pdIndex = (vptr >> (PAGE_BITS + PT_BITS)) & MASK(PD_BITS);
        pdSlot = pd + pdIndex;

        ret.pdSlot = pdSlot;
        ret.pd = pd;
        ret.pdIndex = pdIndex;
        ret.status = EXCEPTION_NONE;
        return ret;
    }
}

void unmapPageDirectory(pdpte_t *pdpt, uint32_t pdptIndex, pde_t *pd)
{
    cap_t threadRoot;
    pdpt[pdptIndex] = pdpte_new(
                          0, /* pdpt_base_address */
                          0, /* val */
                          0, /* cache_disabled */
                          0, /* write_through */
                          0  /* present */
                      );
    /* according to the intel manual if we modify a pdpt we must
     * reload cr3 */
    threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
    if (isValidNativeRoot(threadRoot) && (void*)pptr_of_cap(threadRoot) == (void*)pdpt) {
        write_cr3(read_cr3());
    }
}

void unmapAllPageDirectories(pdpte_t *pdpt)
{
    uint32_t i;

    for (i = 0; i < PPTR_USER_TOP >> IA32_1G_bits; i++) {
        if (pdpte_ptr_get_present(pdpt + i)) {
            pde_t *pd = PD_PTR(paddr_to_pptr(pdpte_ptr_get_pd_base_address(pdpt + i)));
            cte_t *pdCte;
            cap_t pdCap;
            pdCte = cdtFindAtDepth(cap_page_directory_cap_new(PDPT_REF(pdpt), i, PD_REF(pd)), pdpte_ptr_get_avl_cte_depth(pdpt + i));
            assert(pdCte);

            pdCap = pdCte->cap;
            pdCap = cap_page_directory_cap_set_capPDMappedObject(pdCap, 0);
            cdtUpdate(pdCte, pdCap);
        }
    }
}

void flushAllPageDirectories(pdpte_t *pdpt)
{
    cap_t threadRoot;
    threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
    if (PDPTE_PTR(cap_pdpt_cap_get_capPDPTBasePtr(threadRoot)) == pdpt) {
        invalidateTLB();
    }
}

void flushPageSmall(pte_t *pt, uint32_t ptIndex)
{
    cap_t threadRoot;
    cte_t *ptCte;
    pde_t *pd;
    uint32_t pdIndex;

    /* We know this pt can only be mapped into one single pd. So
     * lets find a cap with that mapping information */
    ptCte = cdtFindWithExtra(cap_page_table_cap_new(0, 0, PT_REF(pt)));
    if (ptCte) {
        pd = PD_PTR(cap_page_table_cap_get_capPTMappedObject(ptCte->cap));
        pdIndex = cap_page_table_cap_get_capPTMappedIndex(ptCte->cap);

        if (pd) {
            cte_t *pdCte;
            pdpte_t *pdpt;
            uint32_t pdptIndex;
            pdCte = cdtFindWithExtra(cap_page_directory_cap_new(0, 0, PD_REF(pd)));
            if (pdCte) {
                pdpt = PDPT_PTR(cap_page_directory_cap_get_capPDMappedObject(pdCte->cap));
                pdptIndex = cap_page_directory_cap_get_capPDMappedIndex(pdCte->cap);

                /* check if page belongs to current address space */
                threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
                if (isValidNativeRoot(threadRoot) && (void*)pptr_of_cap(threadRoot) == pdpt) {
                    invalidateTLBentry( (pdptIndex << (PD_BITS + PT_BITS + PAGE_BITS)) | (pdIndex << (PT_BITS + PAGE_BITS)) | (ptIndex << PAGE_BITS));
                    invalidatePageStructureCache();
                }
            }
        }
    }
}

void flushPageLarge(pde_t *pd, uint32_t pdIndex)
{
    cap_t threadRoot;
    cte_t *pdCte;

    pdCte = cdtFindWithExtra(cap_page_directory_cap_new(0, 0, PD_REF(pd)));
    if (pdCte) {
        pdpte_t *pdpt;
        uint32_t pdptIndex;
        pdpt = PDPT_PTR(cap_page_directory_cap_get_capPDMappedObject(pdCte->cap));
        pdptIndex = cap_page_directory_cap_get_capPDMappedIndex(pdCte->cap);

        /* check if page belongs to current address space */
        threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
        if (cap_get_capType(threadRoot) == cap_pdpt_cap &&
                PDPTE_PTR(cap_pdpt_cap_get_capPDPTBasePtr(threadRoot)) == pdpt) {
            invalidateTLBentry( (pdIndex << (PT_BITS + PAGE_BITS)) | (pdptIndex << IA32_1G_bits));
            invalidatePageStructureCache();
        }
    }
}

void flushAllPageTables(pde_t *pd)
{
    cap_t threadRoot;
    cte_t *pdCte;

    pdCte = cdtFindWithExtra(cap_page_directory_cap_new(0, 0, PD_REF(pd)));
    if (pdCte) {
        pdpte_t *pdpt;
        pdpt = PDPT_PTR(cap_page_directory_cap_get_capPDMappedObject(pdCte->cap));
        /* check if this is the current address space */
        threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
        if (cap_get_capType(threadRoot) == cap_pdpt_cap &&
                PDPTE_PTR(cap_pdpt_cap_get_capPDPTBasePtr(threadRoot)) == pdpt) {
            invalidateTLB();
        }
        invalidatePageStructureCache();
    }
}

void flushPageDirectory(pdpte_t *pdpt, uint32_t pdptIndex, pde_t *pd)
{
    flushAllPageDirectories(pdpt);
}

exception_t
decodeIA32PageDirectoryInvocation(
    word_t label,
    unsigned int length,
    cte_t* cte,
    cap_t cap,
    extra_caps_t extraCaps,
    word_t* buffer
)
{
    word_t          vaddr;
    vm_attributes_t attr;
    pdpte_t *       pdpt;
    pdpte_t *       pdptSlot;
    unsigned int    pdptIndex;
    cap_t           vspaceCap;
    pdpte_t         pdpte;
    paddr_t         paddr;
    cap_t           threadRoot;

    if (label == IA32PageDirectoryUnmap) {
        setThreadState(ksCurThread, ThreadState_Restart);

        pdpt = PDPTE_PTR(cap_page_directory_cap_get_capPDMappedObject(cap));
        if (pdpt) {
            pde_t *pd = PDE_PTR(cap_page_directory_cap_get_capPDBasePtr(cap));
            pdptIndex = cap_page_directory_cap_get_capPDMappedIndex(cap);
            unmapPageDirectory(pdpt, pdptIndex, pd);
            flushPageDirectory(pdpt, pdptIndex, pd);
            clearMemory((void *)pd, cap_get_capSizeBits(cap));
        }
        cdtUpdate(cte, cap_page_directory_cap_set_capPDMappedObject(cap, 0));

        return EXCEPTION_NONE;
    }

    if (label != IA32PageDirectoryMap) {
        userError("IA32PageDirectory: Illegal operation.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (length < 2 || extraCaps.excaprefs[0] == NULL) {
        userError("IA32PageDirectory: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (cdtFindWithExtra(cap)) {
        userError("IA32PageDirectory: Page direcotry is already mapped to a pdpt.");
        current_syscall_error.type =
            seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;

        return EXCEPTION_SYSCALL_ERROR;
    }

    vaddr = getSyscallArg(0, buffer) & (~MASK(IA32_1G_bits));
    attr = vmAttributesFromWord(getSyscallArg(1, buffer));
    vspaceCap = extraCaps.excaprefs[0]->cap;

    if (!isValidNativeRoot(vspaceCap)) {
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 1;
        return EXCEPTION_SYSCALL_ERROR;
    }

    pdpt = (void*)pptr_of_cap(vspaceCap);

    if (vaddr >= PPTR_USER_TOP) {
        userError("IA32PageDirectory: Mapping address too high.");
        current_syscall_error.type = seL4_InvalidArgument;
        current_syscall_error.invalidArgumentNumber = 0;
        return EXCEPTION_SYSCALL_ERROR;
    }

    pdptIndex = vaddr >> IA32_1G_bits;
    pdptSlot = pdpt + pdptIndex;

    if (pdpte_ptr_get_present(pdptSlot)) {
        current_syscall_error.type = seL4_DeleteFirst;
        return EXCEPTION_SYSCALL_ERROR;
    }

    paddr = pptr_to_paddr(PDE_PTR(cap_page_directory_cap_get_capPDBasePtr(cap)));
    pdpte = pdpte_new(
                paddr,                                      /* pd_base_address  */
                mdb_node_get_cdtDepth(cte->cteMDBNode),     /* avl_cte_depth    */
                vm_attributes_get_ia32PCDBit(attr),         /* cache_disabled   */
                vm_attributes_get_ia32PWTBit(attr),         /* write_through    */
                1                                           /* present          */
            );

    cap = cap_page_directory_cap_set_capPDMappedObject(cap, PDPT_REF(pdpt));
    cap = cap_page_directory_cap_set_capPDMappedIndex(cap, pdptIndex);

    cdtUpdate(cte, cap);
    *pdptSlot = pdpte;

    /* according to the intel manual if we modify a pdpt we must
     * reload cr3 */
    threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
    if (isValidNativeRoot(threadRoot) && (void*)pptr_of_cap(threadRoot) == (void*)pptr_of_cap(vspaceCap)) {
        write_cr3(read_cr3());
    }

    setThreadState(ksCurThread, ThreadState_Restart);
    invalidatePageStructureCache();
    return EXCEPTION_NONE;
}

#endif
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/machine/capdl.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>

#ifdef DEBUG

#include <object/structures.h>
#include <object/tcb.h>
#include <model/statedata.h>
#include <machine/capdl.h>
#include <arch/machine/capdl.h>
#include <plat/machine/debug_helpers.h>
#include <machine.h>

#define ARCH 0xe1

#define PD_READ_SIZE         BIT(PD_BITS)
#define PT_READ_SIZE         BIT(PT_BITS)
#define IO_PT_READ_SIZE      BIT(VTD_PT_BITS)

static int getDecodedChar(unsigned char *result)
{
    unsigned char c;
    c = getDebugChar();
    if (c == START) {
        return 1;
    }
    if (c == ESCAPE) {
        c = getDebugChar();
        if (c == START) {
            return 1;
        }
        switch (c) {
        case ESCAPE_ESCAPE:
            *result = ESCAPE;
            break;
        case START_ESCAPE:
            *result = START;
            break;
        case END_ESCAPE:
            *result = END;
            break;
        default:
            if (c >= 20 && c < 40) {
                *result = c - 20;
            }
        }
        return 0;
    } else {
        *result = c;
        return 0;
    }
}

static void putEncodedChar(unsigned char c)
{
    switch (c) {
    case ESCAPE:
        putDebugChar(ESCAPE);
        putDebugChar(ESCAPE_ESCAPE);
        break;
    case START:
        putDebugChar(ESCAPE);
        putDebugChar(START_ESCAPE);
        break;
    case END:
        putDebugChar(ESCAPE);
        putDebugChar(END_ESCAPE);
        break;
    default:
        if (c < 20) {
            putDebugChar(ESCAPE);
            putDebugChar(c + 20);
        } else {
            putDebugChar(c);
        }
    }
}

static int getArg32(unsigned int *res)
{
    unsigned char b1 = 0;
    unsigned char b2 = 0;
    unsigned char b3 = 0;
    unsigned char b4 = 0;
    if (getDecodedChar(&b1)) {
        return 1;
    }
    if (getDecodedChar(&b2)) {
        return 1;
    }
    if (getDecodedChar(&b3)) {
        return 1;
    }
    if (getDecodedChar(&b4)) {
        return 1;
    }
    *res = (b1 << 24 ) | (b2 << 16) | (b3 << 8) | b4;
    return 0;
}

static void sendWord(unsigned int word)
{
    putEncodedChar(word & 0xff);
    putEncodedChar((word >> 8) & 0xff);
    putEncodedChar((word >> 16) & 0xff);
    putEncodedChar((word >> 24) & 0xff);
}

static void sendPD(unsigned int address)
{
    unsigned int i;
    unsigned int exists;
    pde_t *start = (pde_t *)address;
    for (i = 0; i < PD_READ_SIZE; i++) {
        pde_t pde = start[i];
        exists = 1;
        if (pde_get_page_size(pde) == pde_pde_small && (pde_pde_small_get_pt_base_address(pde) == 0 ||
                                                        !pde_pde_small_get_present(pde) || !pde_pde_small_get_super_user(pde))) {
            exists = 0;
        } else if (pde_get_page_size(pde) == pde_pde_large && (pde_pde_large_get_page_base_address(pde) == 0 ||
                                                               !pde_pde_large_get_present(pde) || !pde_pde_large_get_super_user(pde))) {
            exists = 0;
        }
        if (exists != 0 && i < PPTR_BASE >> pageBitsForSize(IA32_LargePage)) {
            sendWord(i);
            sendWord(pde.words[0]);
        }
    }
}

static void sendPT(unsigned int address)
{
    unsigned int i;
    pte_t *start = (pte_t *)address;
    for (i = 0; i < PT_READ_SIZE; i++) {
        pte_t pte = start[i];
        if (pte_get_page_base_address(pte) != 0 && pte_get_present(pte) && pte_get_super_user(pte)) {
            sendWord(i);
            sendWord(pte.words[0]);
        }
    }
}

#ifdef CONFIG_IOMMU

static void sendIOPT(unsigned int address, unsigned int level)
{
    unsigned int i;
    vtd_pte_t *start = (vtd_pte_t *)address;
    for (i = 0; i < IO_PT_READ_SIZE; i++) {
        vtd_pte_t vtd_pte = start[i];
        if (vtd_pte_get_addr(vtd_pte) != 0) {
            sendWord(i);
            sendWord(vtd_pte.words[0]);
            sendWord(vtd_pte.words[1]);
            if (level == ia32KSnumIOPTLevels) {
                sendWord(1);
            } else {
                sendWord(0);
            }
        }
    }
}

static void sendIOSpace(uint32_t pci_request_id)
{
    uint32_t   vtd_root_index;
    uint32_t   vtd_context_index;
    vtd_rte_t* vtd_root_slot;
    vtd_cte_t* vtd_context;
    vtd_cte_t* vtd_context_slot;

    vtd_root_index = vtd_get_root_index(pci_request_id);
    vtd_root_slot = ia32KSvtdRootTable + vtd_root_index;

    vtd_context = (vtd_cte_t*)paddr_to_pptr(vtd_rte_ptr_get_ctp(vtd_root_slot));
    vtd_context_index = vtd_get_context_index(pci_request_id);
    vtd_context_slot = &vtd_context[vtd_context_index];

    if (vtd_cte_ptr_get_present(vtd_context_slot)) {
        sendWord(vtd_cte_ptr_get_asr(vtd_context_slot));
    } else {
        sendWord(0);
    }
}

#endif

static void sendRunqueues(void)
{
    unsigned int i;
    sendWord((unsigned int)ksCurThread);
    for (i = 0; i < NUM_READY_QUEUES; i++) {
        tcb_t *current = ksReadyQueues[i].head;
        if (current != 0) {
            while (current != ksReadyQueues[i].end) {
                sendWord((unsigned int)current);
                current = current -> tcbSchedNext;
            }
            sendWord((unsigned int)current);
        }
    }
}

static void sendEPQueue(unsigned int epptr)
{
    tcb_t *current = (tcb_t *)endpoint_ptr_get_epQueue_head((endpoint_t *)epptr);
    tcb_t *tail = (tcb_t *)endpoint_ptr_get_epQueue_tail((endpoint_t *)epptr);
    if (current == 0) {
        return;
    }
    while (current != tail) {
        sendWord((unsigned int)current);
        current = current->tcbEPNext;
    }
    sendWord((unsigned int)current);
}

static void sendCNode(unsigned int address, unsigned int sizebits)
{
    unsigned int i;
    cte_t *start = (cte_t *)address;
    for (i = 0; i < (1 << sizebits); i++) {
        cap_t cap = start[i].cap;
        if (cap_get_capType(cap) != cap_null_cap) {
            sendWord(i);
            sendWord(cap.words[0]);
            sendWord(cap.words[1]);
        }
    }
}

static void sendIRQNode(void)
{
    sendCNode((unsigned int)intStateIRQNode, 8);
}

static void sendVersion(void)
{
    sendWord(ARCH);
    sendWord(CAPDL_VERSION);
}

void capDL(void)
{
    int result;
    int done = 0;
    while (done == 0) {
        unsigned char c;
        do {
            c = getDebugChar();
        } while (c != START);
        do {
            result = getDecodedChar(&c);
            if (result) {
                continue;
            }
            switch (c) {
            case PD_COMMAND: {
                /*pgdir */
                unsigned int arg;
                result = getArg32(&arg);
                if (result) {
                    continue;
                }
                sendPD(arg);
                putDebugChar(END);
            }
            break;
            case PT_COMMAND: {
                /*pg table */
                unsigned int arg;
                result = getArg32(&arg);
                if (result) {
                    continue;
                }
                sendPT(arg);
                putDebugChar(END);
            }
            break;
#ifdef CONFIG_IOMMU
            case IO_PT_COMMAND: {
                /*io pt table */
                unsigned int address, level;
                result = getArg32(&address);
                if (result) {
                    continue;
                }
                result = getArg32(&level);
                if (result) {
                    continue;
                }
                sendIOPT(address, level);
                putDebugChar(END);
            }
            break;
            case IO_SPACE_COMMAND: {
                /*io space */
                unsigned int arg;
                result = getArg32(&arg);
                if (result) {
                    continue;
                }
                sendIOSpace(arg);
                putDebugChar(END);
            }
            break;
#endif
            case RQ_COMMAND: {
                /*runqueues */
                sendRunqueues();
                putDebugChar(END);
                result = 0;
            }
            break;
            case EP_COMMAND: {
                /*endpoint waiters */
                unsigned int arg;
                result = getArg32(&arg);
                if (result) {
                    continue;
                }
                sendEPQueue(arg);
                putDebugChar(END);
            }
            break;
            case CN_COMMAND: {
                /*cnode */
                unsigned int address, sizebits;
                result = getArg32(&address);
                if (result) {
                    continue;
                }
                result = getArg32(&sizebits);
                if (result) {
                    continue;
                }

                sendCNode(address, sizebits);
                putDebugChar(END);
            }
            break;
            case IRQ_COMMAND: {
                sendIRQNode();
                putDebugChar(END);
                result = 0;
            }
            break;
            case VERSION_COMMAND: {
                sendVersion();
                putDebugChar(END);
            }
            break;
            case DONE: {
                done = 1;
                putDebugChar(END);
            }
            default:
                result = 0;
                break;
            }
        } while (result);
    }
}

#endif
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/machine/fpu.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <api/failures.h>
#include <api/syscall.h>
#include <model/statedata.h>
#include <arch/machine/fpu.h>
#include <arch/machine/cpu_registers.h>
#include <arch/object/structures.h>
#include <arch/linker.h>

/*
 * Setup the FPU register state for a new thread.
 */
void
Arch_initFpuContext(user_context_t *context)
{
    context->fpuState = ia32KSnullFpuState;
}

/*
 * Switch the owner of the FPU to the given thread.
 */
static void
switchFpuOwner(tcb_t *new_owner)
{
    enableFpu();
    if (ia32KSfpuOwner) {
        saveFpuState(&ia32KSfpuOwner->tcbArch.tcbContext.fpuState);
    }
    if (new_owner) {
        loadFpuState(&new_owner->tcbArch.tcbContext.fpuState);
    } else {
        disableFpu();
    }
    ia32KSfpuOwner = new_owner;
}

/*
 * Handle a FPU fault.
 *
 * This CPU exception is thrown when userspace attempts to use the FPU while
 * it is disabled. We need to save the current state of the FPU, and hand
 * it over.
 */
VISIBLE exception_t
handleUnimplementedDevice(void)
{
    /*
     * If we have already given the FPU to the user, we should not reach here.
     *
     * This should only be able to occur on CPUs without an FPU at all, which
     * we presumably are happy to assume will not be running seL4.
     */
    assert(ksCurThread != ia32KSfpuOwner);

    /* Otherwise, lazily switch over the FPU. */
    switchFpuOwner(ksCurThread);

    return EXCEPTION_NONE;
}

/*
 * Prepare for the deletion of the given thread.
 */
void
Arch_fpuThreadDelete(tcb_t *thread)
{
    /*
     * If the thread being deleted currently owns the FPU, switch away from it
     * so that 'ia32KSfpuOwner' doesn't point to invalid memory.
     */
    if (ia32KSfpuOwner == thread) {
        switchFpuOwner(NULL);
    }
}

/*
 * Initialise the FPU for this machine.
 */
BOOT_CODE void
Arch_initFpu(void)
{
    /* Enable FPU / SSE / SSE2 / SSE3 / SSSE3 / SSE4 Extensions. */
    write_cr4(read_cr4() | CR4_OSFXSR);

    /* Enable the FPU in general. Although leave it in a state where it will
     * generate a fault if someone tries to use it as we implement lazy
     * switching */
    write_cr0((read_cr0() & ~CR0_EMULATION) | CR0_MONITOR_COPROC | CR0_NUMERIC_ERROR | CR0_TASK_SWITCH);
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/machine/hardware.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <types.h>
#include <machine/registerset.h>
#include <model/statedata.h>
#include <object/structures.h>
#include <arch/machine.h>
#include <arch/machine/hardware.h>
#include <arch/machine/registerset.h>
#include <arch/linker.h>

/* initialises MSRs required to setup sysenter and sysexit */
BOOT_CODE void
init_sysenter_msrs(void)
{
    ia32_wrmsr(IA32_SYSENTER_CS_MSR,  0, (uint32_t)SEL_CS_0);
    ia32_wrmsr(IA32_SYSENTER_EIP_MSR, 0, (uint32_t)&handle_syscall);
    ia32_wrmsr(IA32_SYSENTER_ESP_MSR, 0, (uint32_t)&ia32KStss.words[1]);
}

word_t PURE getRestartPC(tcb_t *thread)
{
    return getRegister(thread, FaultEIP);
}

void setNextPC(tcb_t *thread, word_t v)
{
    setRegister(thread, NextEIP, v);
}

/* Returns the size of CPU's cacheline */
BOOT_CODE uint32_t CONST
getCacheLineSizeBits(void)
{
    uint32_t line_size;
    uint32_t n;

    line_size = getCacheLineSize();
    if (line_size == 0) {
        printf("Cacheline size must be >0\n");
        return 0;
    }

    /* determine size_bits */
    n = 0;
    while (!(line_size & 1)) {
        line_size >>= 1;
        n++;
    }

    if (line_size != 1) {
        printf("Cacheline size must be a power of two\n");
        return 0;
    }

    return n;
}

/* Flushes a specific memory range from the CPU cache */

void flushCacheRange(void* vaddr, uint32_t size_bits)
{
    uint32_t v;

    assert(size_bits < WORD_BITS);
    assert(IS_ALIGNED((uint32_t)vaddr, size_bits));

    ia32_mfence();

    for (v = ROUND_DOWN((uint32_t)vaddr, ia32KScacheLineSizeBits);
            v < (uint32_t)vaddr + BIT(size_bits);
            v += BIT(ia32KScacheLineSizeBits)) {
        flushCacheLine((void*)v);
    }
    ia32_mfence();
}

/* Disables as many prefetchers as possible */
BOOT_CODE bool_t
disablePrefetchers()
{
    uint32_t version_info;
    uint32_t low, high;
    int i;

    uint32_t valid_models[] = { BROADWELL_MODEL_ID, HASWELL_MODEL_ID, IVY_BRIDGE_MODEL_ID,
                                SANDY_BRIDGE_1_MODEL_ID, SANDY_BRIDGE_2_MODEL_ID, WESTMERE_1_MODEL_ID, WESTMERE_2_MODEL_ID,
                                WESTMERE_3_MODEL_ID, NEHALEM_1_MODEL_ID, NEHALEM_2_MODEL_ID, NEHALEM_3_MODEL_ID
                              };

    version_info = ia32_cpuid_eax(0x1, 0x0);

    for (i = 0; i < ARRAY_SIZE(valid_models); ++i) {
        if (MODEL_ID(version_info) == valid_models[i]) {
            low = ia32_rdmsr_low(IA32_PREFETCHER_MSR);
            high = ia32_rdmsr_high(IA32_PREFETCHER_MSR);

            low |= IA32_PREFETCHER_MSR_L2;
            low |= IA32_PREFETCHER_MSR_L2_ADJACENT;
            low |= IA32_PREFETCHER_MSR_DCU;
            low |= IA32_PREFETCHER_MSR_DCU_IP;

            ia32_wrmsr(IA32_PREFETCHER_MSR, high, low);

            return true;
        }
    }

    printf("Disabling prefetchers not implemented for CPU model: %x\n", MODEL_ID(version_info));
    return false;
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/machine/registerset.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>
#include <arch/machine/registerset.h>
#include <arch/machine/fpu.h>
#include <arch/object/structures.h>

const register_t msgRegisters[] = {
    EDI, EBP
};

const register_t frameRegisters[] = {
    FaultEIP, ESP, EFLAGS, EAX, EBX, ECX, EDX, ESI, EDI, EBP
};

const register_t gpRegisters[] = {
    TLS_BASE, FS, GS
};

const register_t exceptionMessage[] = {
    FaultEIP, ESP, EFLAGS
};

const register_t syscallMessage[] = {
    EAX, EBX, ECX, EDX, ESI, EDI, EBP, NextEIP, ESP, EFLAGS
};

#ifdef CONFIG_VTX
const register_t crExitRegs[] = {
    EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
};
#endif

void Arch_initContext(user_context_t* context)
{
    context->registers[EAX] = 0;
    context->registers[EBX] = 0;
    context->registers[ECX] = 0;
    context->registers[EDX] = 0;
    context->registers[ESI] = 0;
    context->registers[EDI] = 0;
    context->registers[EBP] = 0;
    context->registers[DS] = SEL_DS_3;
    context->registers[ES] = SEL_DS_3;
    context->registers[FS] = SEL_NULL;
    context->registers[GS] = SEL_NULL;
    context->registers[TLS_BASE] = 0;
    context->registers[Error] = 0;
    context->registers[FaultEIP] = 0;
    context->registers[NextEIP] = 0;            /* overwritten by setNextPC() later on */
    context->registers[CS] = SEL_CS_3;
    context->registers[EFLAGS] = BIT(9) | BIT(1); /* enable interrupts and set bit 1 which is always 1 */
    context->registers[ESP] = 0;                /* userland has to set it after entry */
    context->registers[SS] = SEL_DS_3;

    Arch_initFpuContext(context);
}

word_t sanitiseRegister(register_t reg, word_t v)
{
    if (reg == EFLAGS) {
        v |=  BIT(1);   /* reserved bit that must be set to 1 */
        v &= ~BIT(3);   /* reserved bit that must be set to 0 */
        v &= ~BIT(5);   /* reserved bit that must be set to 0 */
        v |=  BIT(9);   /* interrupts must be enabled in userland */
        v &=  MASK(12); /* bits 12:31 have to be 0 */
    }
    if (reg == FS || reg == GS) {
        if (v != SEL_TLS && v != SEL_IPCBUF) {
            v = 0;
        }
    }
    return v;
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/model/statedata.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>
#include <util.h>
#include <api/types.h>
#include <arch/types.h>
#include <arch/model/statedata.h>
#include <arch/object/structures.h>

/* ==== read/write kernel state not preserved across kernel entries ==== */

/* Interrupt currently being handled */
interrupt_t ia32KScurInterrupt VISIBLE;

/* ==== proper read/write kernel state ==== */

/* Task State Segment (TSS), contains currently running TCB in ESP0 */
tss_t ia32KStss VISIBLE;

/* Global Descriptor Table (GDT) */
gdt_entry_t ia32KSgdt[GDT_ENTRIES];

/*
 * Current thread whose state is installed in the FPU, or NULL if
 * the FPU is currently invalid.
 */
tcb_t *ia32KSfpuOwner VISIBLE;

/* ==== read-only kernel state (only written during bootstrapping) ==== */

/* The privileged kernel mapping PD & PT */
pdpte_t* ia32KSkernelPDPT;
pde_t* ia32KSkernelPD;
pte_t* ia32KSkernelPT;

/* CPU Cache Line Size */
uint32_t ia32KScacheLineSizeBits;

/* Interrupt Descriptor Table (IDT) */
idt_entry_t ia32KSidt[IDT_ENTRIES];

/* A valid initial FPU state, copied to every new thread. */
user_fpu_state_t ia32KSnullFpuState ALIGN(MIN_FPU_ALIGNMENT);

/* Current active page directory. This is really just a shadow of CR3 */
paddr_t ia32KSCurrentPD VISIBLE;

#ifdef CONFIG_IOMMU
/* Number of IOMMUs (DMA Remapping Hardware Units) */
uint32_t ia32KSnumDrhu;

/* Intel VT-d Root Entry Table */
vtd_rte_t* ia32KSvtdRootTable;
uint32_t ia32KSnumIOPTLevels;
uint32_t ia32KSnumIODomainIDBits;
int ia32KSFirstValidIODomain;
#endif

#if defined DEBUG || defined RELEASE_PRINTF
uint16_t ia32KSconsolePort;
uint16_t ia32KSdebugPort;
#endif

node_id_t ia32KSNodeID;
uint32_t ia32KSNumNodes;
cpu_id_t* ia32KSCPUList;
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/object/interrupt.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <kernel/boot.h>
#include <model/statedata.h>
#include <arch/object/interrupt.h>
#include <arch/linker.h>

exception_t Arch_decodeInterruptControl(unsigned int length, extra_caps_t extraCaps)
{
    current_syscall_error.type = seL4_IllegalOperation;
    return EXCEPTION_SYSCALL_ERROR;
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/object/ioport.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <kernel/thread.h>
#include <api/failures.h>
#include <api/syscall.h>
#include <machine/io.h>
#include <arch/object/ioport.h>
#include <arch/api/invocation.h>

static exception_t
ensurePortOperationAllowed(cap_t cap, uint32_t start_port, uint32_t size)
{
    uint32_t first_allowed = cap_io_port_cap_get_capIOPortFirstPort(cap);
    uint32_t last_allowed = cap_io_port_cap_get_capIOPortLastPort(cap);
    uint32_t end_port = start_port + size - 1;
    assert(first_allowed <= last_allowed);
    assert(start_port <= end_port);

    if ((start_port < first_allowed) || (end_port > last_allowed)) {
        userError("IOPort: Ports %d--%d fall outside permitted range %d--%d.",
                  (int)start_port, (int)end_port,
                  (int)first_allowed, (int)last_allowed);
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    return EXCEPTION_NONE;
}

exception_t
decodeIA32PortInvocation(
    word_t label,
    unsigned int length,
    cptr_t cptr,
    cte_t* slot,
    cap_t cap,
    extra_caps_t extraCaps,
    word_t* buffer
)
{
    uint32_t res;
    uint32_t len;
    uint16_t port;
    exception_t ret;

    /* Ensure user specified at very least a port. */
    if (length < 1) {
        userError("IOPort: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    /* Get the port the user is trying to write to. */
    port = getSyscallArg(0, buffer) & 0xffff;

    switch (label) {
    case IA32IOPortIn8: { /* inport 8 bits */

        /* Check we are allowed to perform the operation. */
        ret = ensurePortOperationAllowed(cap, port, 1);
        if (ret != EXCEPTION_NONE) {
            return ret;
        }

        /* Perform the read. */
        res = in8(port);
        len = 1;
        break;
    }

    case IA32IOPortIn16: { /* inport 16 bits */

        /* Check we are allowed to perform the operation. */
        ret = ensurePortOperationAllowed(cap, port, 2);
        if (ret != EXCEPTION_NONE) {
            return ret;
        }

        /* Perform the read. */
        res = in16(port);
        len = 1;
        break;
    }

    case IA32IOPortIn32: { /* inport 32 bits */

        /* Check we are allowed to perform the operation. */
        ret = ensurePortOperationAllowed(cap, port, 4);
        if (ret != EXCEPTION_NONE) {
            return ret;
        }

        /* Perform the read. */
        res = in32(port);
        len = 1;
        break;
    }

    case IA32IOPortOut8: { /* outport 8 bits */
        uint8_t data;

        /* Check we are allowed to perform the operation. */
        ret = ensurePortOperationAllowed(cap, port, 1);
        if (ret != EXCEPTION_NONE) {
            return ret;
        }

        /* Perform the write. */
        data = (getSyscallArg(0, buffer) >> 16) & 0xff;
        out8(port, data);
        len = 0;
        break;
    }

    case IA32IOPortOut16: { /* outport 16 bits */
        uint16_t data;

        /* Check we are allowed to perform the operation. */
        ret = ensurePortOperationAllowed(cap, port, 2);
        if (ret != EXCEPTION_NONE) {
            return ret;
        }

        /* Perform the write. */
        data = (getSyscallArg(0, buffer) >> 16) & 0xffff;
        out16(port, data);
        len = 0;
        break;
    }

    case IA32IOPortOut32: { /* outport 32 bits */
        uint32_t data;

        /* Ensure the incoming message is long enough for the write. */
        if (length < 2) {
            userError("IOPort Out32: Truncated message.");
            current_syscall_error.type = seL4_TruncatedMessage;
            return EXCEPTION_SYSCALL_ERROR;
        }

        /* Check we are allowed to perform the operation. */
        ret = ensurePortOperationAllowed(cap, port, 4);
        if (ret != EXCEPTION_NONE) {
            return ret;
        }

        /* Perform the write. */
        data = getSyscallArg(1, buffer);
        out32(port, data);
        len = 0;
        break;
    }

    default:
        userError("IOPort: Unknown operation.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (len > 0) {
        /* return the value read from the port */
        setRegister(ksCurThread, badgeRegister, 0);
        if (n_msgRegisters < 1) {
            word_t* ipcBuffer;
            ipcBuffer = lookupIPCBuffer(true, ksCurThread);
            if (ipcBuffer != NULL) {
                ipcBuffer[1] = res;
                len = 1;
            } else {
                len = 0;
            }
        } else {
            setRegister(ksCurThread, msgRegisters[0], res);
            len = 1;
        }
    }
    setRegister(ksCurThread, msgInfoRegister,
                wordFromMessageInfo(message_info_new(0, 0, 0, len)));

    setThreadState(ksCurThread, ThreadState_Restart);
    return EXCEPTION_NONE;
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/object/iospace.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>

#ifdef CONFIG_IOMMU

#include <api/syscall.h>
#include <machine/io.h>
#include <kernel/thread.h>
#include <arch/api/invocation.h>
#include <arch/object/iospace.h>
#include <arch/model/statedata.h>
#include <arch/linker.h>
#include <plat/machine/intel-vtd.h>

typedef struct lookupVTDContextSlot_ret {
    vtd_cte_t *cte;
    uint32_t index;
} lookupVTDContextSlot_ret_t;

BOOT_CODE cap_t
master_iospace_cap(void)
{
    if (ia32KSnumDrhu == 0) {
        return cap_null_cap_new();
    }

    return
        cap_io_space_cap_new(
            0,              /* capDomainID  */
            0              /* capPCIDevice */
        );
}

/* parent[index] IS child */
void unmapVTDPT(vtd_pte_t *parent, vtd_pte_t *child, uint32_t index)
{
    parent[index] = vtd_pte_new(0, 0, 0, 0);
    flushCacheRange(parent + index, VTD_PTE_SIZE_BITS);
    invalidate_iotlb();
}

void unmapAllIOPT(vtd_pte_t *pt, int level)
{
    uint32_t i;

    for (i = 0; i < BIT(VTD_PT_BITS); i++) {
        /* see if there is anything here */
        vtd_pte_t *pte = pt + i;
        if (vtd_pte_ptr_get_read(pte)) {
            uint32_t depth = vtd_pte_ptr_get_cte_depth(pte);
            /* work out if we are searching for frames or more page tables */
            if (level == ia32KSnumIOPTLevels - 1) {
                cap_t cap;
                void *frame = paddr_to_pptr(vtd_pte_ptr_get_addr(pte));
                cte_t *cte = cdtFindAtDepth(cap_frame_cap_new(IA32_SmallPage, VTD_PT_REF(pt), i, IA32_MAPPING_IO, 0, (uint32_t)frame), depth);
                assert(cte);
                cap = cap_frame_cap_set_capFMappedObject(cte->cap, 0);
                cdtUpdate(cte, cap);
            } else {
                cap_t cap;
                vtd_pte_t *pt2 = VTD_PTE_PTR(paddr_to_pptr(vtd_pte_ptr_get_addr(pte)));
                cte_t *cte = cdtFindAtDepth(cap_io_page_table_cap_new(0, VTD_PT_REF(pt), i, VTD_PT_REF(pt2)), depth);
                assert(cte);
                cap = cap_io_page_table_cap_set_capIOPTMappedObject(cte->cap, 0);
                cdtUpdate(cte, cap);
            }
        }
    }
}

static void unmapVTDContextEntryAt(vtd_cte_t *cte, uint32_t index)
{
    /* Lookup the page table and unmap it */
    vtd_pte_t *vtd_pt;
    cte_t *ptCte;
    cap_t ptCap;
    vtd_cte_t *vtd_context_slot = cte + index;
    /* First see if there is a page table */
    if (!vtd_cte_ptr_get_present(vtd_context_slot)) {
        return;
    }
    /* see if it reserved, and thus shouldn't be unmapped */
    if (vtd_cte_ptr_get_rmrr(vtd_context_slot)) {
        return;
    }
    /* Lookup the slot */
    vtd_pt = (vtd_pte_t*)paddr_to_pptr(vtd_cte_ptr_get_asr(vtd_context_slot));
    ptCte = cdtFindAtDepth(cap_io_page_table_cap_new(0, (uint32_t)cte, index, VTD_PT_REF(vtd_pt)), vtd_cte_ptr_get_cte_depth(vtd_context_slot));
    assert(ptCte);
    /* unmap */
    ptCap = cap_io_page_table_cap_set_capIOPTMappedObject(ptCte->cap, 0);
    cdtUpdate(ptCte, ptCap);
    assert(vtd_cte_ptr_get_present(vtd_context_slot));
    vtd_cte_ptr_new(
        vtd_context_slot,
        0,  /* Domain ID          */
        0,  /* CTE Depth          */
        0,  /* RMRR Mapping       */
        0,  /* Address Width      */
        0,  /* Address Space Root */
        0,  /* Translation Type   */
        0   /* Present            */
    );
    flushCacheRange(vtd_context_slot, VTD_CTE_SIZE_BITS);
    invalidate_iotlb();
}

static lookupVTDContextSlot_ret_t lookupVTDContextSlot_helper(cap_t cap)
{
    uint32_t   vtd_root_index;
    uint32_t   vtd_context_index;
    dev_id_t   pci_request_id;
    vtd_rte_t* vtd_root_slot;
    vtd_cte_t* vtd_context;

    assert(cap_get_capType(cap) == cap_io_space_cap);
    pci_request_id = cap_io_space_cap_get_capPCIDevice(cap);

    vtd_root_index = vtd_get_root_index(pci_request_id);
    vtd_root_slot = ia32KSvtdRootTable + vtd_root_index;

    vtd_context = (vtd_cte_t*)paddr_to_pptr(vtd_rte_ptr_get_ctp(vtd_root_slot));
    vtd_context_index = vtd_get_context_index(pci_request_id);

    return (lookupVTDContextSlot_ret_t) {
        .cte = vtd_context, .index = vtd_context_index
    };

}

vtd_cte_t *lookupVTDContextSlot(cap_t cap)
{
    lookupVTDContextSlot_ret_t ret = lookupVTDContextSlot_helper(cap);
    return ret.cte + ret.index;
}

void unmapVTDContextEntry(cap_t cap)
{
    lookupVTDContextSlot_ret_t ret = lookupVTDContextSlot_helper(cap);
    unmapVTDContextEntryAt(ret.cte, ret.index);
}

static lookupIOPTSlot_ret_t lookupIOPTSlot_helper(vtd_pte_t* iopt, word_t translation, word_t levels)
{
    lookupIOPTSlot_ret_t ret;
    uint32_t             iopt_index;
    vtd_pte_t*           vtd_pte_slot;
    vtd_pte_t*           vtd_next_level_iopt;

    if (VTD_PT_BITS * levels >= 32) {
        iopt_index = 0;
    } else {
        iopt_index = (translation >> (VTD_PT_BITS * levels)) & MASK(VTD_PT_BITS);
    }

    vtd_pte_slot = iopt + iopt_index;

    if (!vtd_pte_ptr_get_write(vtd_pte_slot) || levels == 0) {
        /* Slot is in this page table level */
        ret.iopt = iopt;
        ret.index = iopt_index;
        ret.level    = ia32KSnumIOPTLevels - levels;
        ret.status   = EXCEPTION_NONE;
        return ret;
    } else {
        vtd_next_level_iopt = (vtd_pte_t*)paddr_to_pptr(vtd_pte_ptr_get_addr(vtd_pte_slot));
        return lookupIOPTSlot_helper(vtd_next_level_iopt, translation, levels - 1);
    }
}

static inline lookupIOPTSlot_ret_t lookupIOPTSlot(vtd_pte_t* iopt, word_t io_address)
{
    lookupIOPTSlot_ret_t ret;

    if (iopt == 0) {
        ret.iopt        = 0;
        ret.index       = 0;
        ret.level       = 0;
        ret.status      = EXCEPTION_LOOKUP_FAULT;
        return ret;
    } else {
        return lookupIOPTSlot_helper(iopt, io_address >> PAGE_BITS, ia32KSnumIOPTLevels - 1);
    }
}

void
unmapIOPTCap(cap_t cap)
{

    int level = cap_io_page_table_cap_get_capIOPTLevel(cap);
    vtd_pte_t *pt = VTD_PTE_PTR(cap_get_capPtr(cap));
    if (level == 0) {
        vtd_cte_t *ct = VTD_CTE_PTR(cap_io_page_table_cap_get_capIOPTMappedObject(cap));
        uint32_t index = cap_io_page_table_cap_get_capIOPTMappedIndex(cap);
        if (ct) {
            unmapVTDContextEntryAt(ct, index);
        }
    } else {
        vtd_pte_t *parent = VTD_PTE_PTR(cap_io_page_table_cap_get_capIOPTMappedObject(cap));
        if (parent) {
            uint32_t index = cap_io_page_table_cap_get_capIOPTMappedIndex(cap);
            unmapVTDPT(parent, pt, index);
        }
    }
}

exception_t
decodeIA32IOPTInvocation(
    word_t       label,
    uint32_t     length,
    cte_t*       slot,
    cap_t        cap,
    extra_caps_t extraCaps,
    word_t*      buffer
)
{
    cap_t      io_space;
    paddr_t    paddr;
    uint32_t   io_address;
    uint16_t   domain_id;
    vtd_cte_t* vtd_context_slot;
    vtd_pte_t* vtd_pte;
    lookupVTDContextSlot_ret_t lookup_ret;

    if (label == IA32IOPageTableUnmap) {
        unmapIOPTCap(cap);

        cap = cap_io_page_table_cap_set_capIOPTMappedObject(cap, 0);
        cdtUpdate(slot, cap);

        setThreadState(ksCurThread, ThreadState_Restart);
        return EXCEPTION_NONE;
    }

    if (extraCaps.excaprefs[0] == NULL || length < 1) {
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (label != IA32IOPageTableMap ) {
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    io_space     = extraCaps.excaprefs[0]->cap;
    io_address   = getSyscallArg(0, buffer);

    if (cap_io_page_table_cap_get_capIOPTMappedObject(cap)) {
        userError("IA32IOPageTableMap: Page table already mapped");
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if ((cap_get_capType(io_space) != cap_io_space_cap)) {
        userError("IA32IOPageTableMap: Not a valid IO Space");
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;
        return EXCEPTION_SYSCALL_ERROR;
    }

    domain_id = cap_io_space_cap_get_capDomainID(io_space);
    if (domain_id == 0) {
        userError("IA32IOPageTableMap: IOSpace has no domain ID");
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;

        return EXCEPTION_SYSCALL_ERROR;
    }

    paddr = pptr_to_paddr(VTD_PTE_PTR(cap_io_page_table_cap_get_capIOPTBasePtr(cap)));
    lookup_ret = lookupVTDContextSlot_helper(io_space);
    vtd_context_slot = lookup_ret.cte + lookup_ret.index;

    if (!vtd_cte_ptr_get_present(vtd_context_slot)) {
        /* 1st Level Page Table */
        vtd_cte_ptr_new(
            vtd_context_slot,
            domain_id,               /* Domain ID */
            mdb_node_get_cdtDepth(slot->cteMDBNode), /* CTE Depth */
            false,                   /* RMRR Mapping */
            ia32KSnumIOPTLevels - 2, /* Address Width (x = levels - 2)       */
            paddr,                   /* Address Space Root                   */
            0,                       /* Translation Type                     */
            true                     /* Present                              */
        );

        flushCacheRange(vtd_context_slot, VTD_CTE_SIZE_BITS);

        cap = cap_io_page_table_cap_set_capIOPTMappedObject(cap, VTD_CTE_REF(lookup_ret.cte));
        cap = cap_io_page_table_cap_set_capIOPTMappedIndex(cap, lookup_ret.index);
        cap = cap_io_page_table_cap_set_capIOPTLevel(cap, 0);
    } else {
        lookupIOPTSlot_ret_t lu_ret;

        vtd_pte = (vtd_pte_t*)paddr_to_pptr(vtd_cte_ptr_get_asr(vtd_context_slot));
        lu_ret  = lookupIOPTSlot(vtd_pte, io_address);

        if (lu_ret.status != EXCEPTION_NONE) {
            current_syscall_error.type = seL4_FailedLookup;
            current_syscall_error.failedLookupWasSource = false;
            return EXCEPTION_SYSCALL_ERROR;
        }
        if (lu_ret.level == ia32KSnumIOPTLevels) {
            userError("IA32IOPageTableMap: Cannot map any more levels of page tables");
            current_syscall_error.type =  seL4_InvalidCapability;
            current_syscall_error.invalidCapNumber = 0;
            return EXCEPTION_SYSCALL_ERROR;
        }
        if (vtd_pte_ptr_get_read(lu_ret.iopt + lu_ret.index)) {
            userError("IA32IOPageTableMap: Mapping already exists");
            current_syscall_error.type =  seL4_InvalidCapability;
            current_syscall_error.invalidCapNumber = 0;
            return EXCEPTION_SYSCALL_ERROR;
        }

        vtd_pte_ptr_new(
            lu_ret.iopt + lu_ret.index,
            paddr,  /* Physical Address         */
            mdb_node_get_cdtDepth(slot->cteMDBNode), /* CTE depth */
            1,      /* Read permission flag     */
            1       /* Write permission flag    */
        );

        flushCacheRange(lu_ret.iopt + lu_ret.index, VTD_PTE_SIZE_BITS);

        cap = cap_io_page_table_cap_set_capIOPTMappedObject(cap, VTD_PTE_REF(lu_ret.iopt));
        cap = cap_io_page_table_cap_set_capIOPTMappedIndex(cap, lu_ret.index);
        cap = cap_io_page_table_cap_set_capIOPTLevel(cap, lu_ret.level);
    }

    cdtUpdate(slot, cap);

    setThreadState(ksCurThread, ThreadState_Restart);
    return EXCEPTION_NONE;
}

exception_t
decodeIA32IOMapInvocation(
    word_t       label,
    uint32_t     length,
    cte_t*       slot,
    cap_t        cap,
    extra_caps_t extraCaps,
    word_t*      buffer
)
{
    cap_t      io_space;
    uint32_t   io_address;
    uint32_t   domain_id;
    vtd_cte_t* vtd_context_slot;
    vtd_pte_t* vtd_pte;
    paddr_t    paddr;
    vm_rights_t capVMRights;
    vm_rights_t vmRights;
    word_t     w_rightsMask;
    lookupIOPTSlot_ret_t lu_ret;

    if (extraCaps.excaprefs[0] == NULL || length < 2) {
        userError("IA32IOFrameMap: Truncated message");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (cap_frame_cap_get_capFSize(cap) != IA32_SmallPage) {
        userError("IA32IOFrameMap: Only 4K frames supported");
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (cap_frame_cap_get_capFMappedObject(cap) != 0) {
        userError("IA32IOFrameMap: Frame already mapped");
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;
        return EXCEPTION_SYSCALL_ERROR;
    }

    io_space    = extraCaps.excaprefs[0]->cap;
    w_rightsMask = getSyscallArg(0, buffer);
    io_address  = getSyscallArg(1, buffer);
    paddr       = pptr_to_paddr((void*)cap_frame_cap_get_capFBasePtr(cap));
    capVMRights = cap_frame_cap_get_capFVMRights(cap);
    vmRights = maskVMRights(capVMRights, rightsFromWord(w_rightsMask));

    if (cap_get_capType(io_space) != cap_io_space_cap) {
        userError("IA32IOFrameMap: IOSpace cap invalid.");
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 1;
        return EXCEPTION_SYSCALL_ERROR;
    }

    domain_id = cap_io_space_cap_get_capDomainID(io_space);

    if (domain_id == 0) {
        userError("IA32IOFrameMap: IOSpace has no domain ID");
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;
        return EXCEPTION_SYSCALL_ERROR;
    }

    vtd_context_slot = lookupVTDContextSlot(io_space);

    if (!vtd_cte_ptr_get_present(vtd_context_slot)) {
        /* 1st Level Page Table is not installed */
        current_syscall_error.type = seL4_FailedLookup;
        current_syscall_error.failedLookupWasSource = false;
        return EXCEPTION_SYSCALL_ERROR;
    }

    vtd_pte = (vtd_pte_t*)paddr_to_pptr(vtd_cte_ptr_get_asr(vtd_context_slot));
    lu_ret  = lookupIOPTSlot(vtd_pte, io_address);

    if (lu_ret.status != EXCEPTION_NONE || lu_ret.level != ia32KSnumIOPTLevels) {
        current_syscall_error.type = seL4_FailedLookup;
        current_syscall_error.failedLookupWasSource = false;
        return EXCEPTION_SYSCALL_ERROR;
    }
    if (vtd_pte_ptr_get_read(lu_ret.iopt + lu_ret.index)) {
        userError("IA32IOFrameMap: Mapping already present");
        current_syscall_error.type = seL4_DeleteFirst;
        return EXCEPTION_SYSCALL_ERROR;
    }

    vtd_pte_ptr_new(
        lu_ret.iopt + lu_ret.index,
        paddr,                                /* Physical Address */
        mdb_node_get_cdtDepth(slot->cteMDBNode),
        1,                                    /* Read permission  */
        WritableFromVMRights(vmRights)        /* Write permission */
    );
    cap = cap_frame_cap_set_capFMappedObject(cap, VTD_PT_REF(lu_ret.iopt));
    cap = cap_frame_cap_set_capFMappedIndex(cap, lu_ret.index);
    cap = cap_frame_cap_set_capFMappedType(cap, IA32_MAPPING_IO);
    cdtUpdate(slot, cap);

    flushCacheRange(lu_ret.iopt + lu_ret.index, VTD_PTE_SIZE_BITS);

    setThreadState(ksCurThread, ThreadState_Restart);
    return EXCEPTION_NONE;
}

void unmapIOPage(cap_t cap)
{
    vtd_pte_t *vtd_pt = VTD_PTE_PTR(cap_frame_cap_get_capFMappedObject(cap));
    uint32_t index = cap_frame_cap_get_capFMappedIndex(cap);
    vtd_pte_t *vtd_pte = vtd_pt + index;

    if (!vtd_pt) {
        return;
    }

    vtd_pte_ptr_new(vtd_pte, 0, 0, 0, 0);

    flushCacheRange(vtd_pte, VTD_PTE_SIZE_BITS);
    invalidate_iotlb();
}

#endif
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/object/ipi.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <api/failures.h>
#include <api/syscall.h>
#include <kernel/thread.h>
#include <arch/object/ipi.h>
#include <arch/kernel/apic.h>
#include <arch/api/invocation.h>

exception_t
decodeIA32IPIInvocation(
    word_t label,
    unsigned int length,
    cptr_t cptr,
    cte_t* slot,
    cap_t cap,
    extra_caps_t extraCaps,
    word_t* buffer
)
{
    node_id_t node_id;
    irq_t     irq;

    if (label != IA32IPISend) {
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (length < 1) {
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    node_id = getSyscallArg(0, buffer) & 0xff;
    irq = (getSyscallArg(0, buffer) >> 8) & 0xff;

    if (node_id >= ia32KSNumNodes || irq < irq_ipi_min || irq > irq_ipi_max) {
        current_syscall_error.type = seL4_InvalidArgument;
        return EXCEPTION_SYSCALL_ERROR;
    }

    /* send the IPI */
    apic_send_ipi(ia32KSCPUList[node_id], irq + IRQ_INT_OFFSET);

    /* setup reply message */
    setRegister(ksCurThread, badgeRegister, 0);
    setRegister(ksCurThread, msgInfoRegister,
                wordFromMessageInfo(message_info_new(0, 0, 0, 0)));
    setThreadState(ksCurThread, ThreadState_Restart);

    return EXCEPTION_NONE;
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/object/objecttype.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>
#include <types.h>
#include <api/failures.h>
#include <kernel/vspace.h>
#include <object/structures.h>
#include <arch/machine.h>
#include <arch/model/statedata.h>
#include <arch/machine/fpu.h>
#include <arch/object/objecttype.h>
#include <arch/object/ioport.h>
#include <machine.h>
#include <kernel/vspace.h>
#include <object/structures.h>
#include <arch/object/ipi.h>

#ifdef CONFIG_IOMMU
#include <arch/object/iospace.h>
#include <plat/machine/intel-vtd.h>
#endif

#ifdef CONFIG_VTX
#include <arch/object/vtx.h>
#include <arch/object/vcpu.h>
#endif

deriveCap_ret_t Arch_deriveCap(cte_t* slot, cap_t cap)
{
    deriveCap_ret_t ret;

    switch (cap_get_capType(cap)) {
    case cap_page_table_cap:
        ret.cap = cap_page_table_cap_set_capPTMappedObject(cap, 0);
        ret.status = EXCEPTION_NONE;
        return ret;

    case cap_page_directory_cap:
        ret.cap = cap_page_directory_cap_set_capPDMappedObject(cap, 0);
        ret.status = EXCEPTION_NONE;
        return ret;

    case cap_pdpt_cap:
        ret.cap = cap;
        ret.status = EXCEPTION_NONE;
        return ret;

    case cap_frame_cap:
        ret.cap = cap_frame_cap_set_capFMappedObject(cap, 0);
        ret.status = EXCEPTION_NONE;
        return ret;
    case cap_io_port_cap:
        ret.cap = cap;
        ret.status = EXCEPTION_NONE;
        return ret;

#ifdef CONFIG_IOMMU
    case cap_io_space_cap:
        ret.cap = cap;
        ret.status = EXCEPTION_NONE;
        return ret;
    case cap_io_page_table_cap:
        ret.cap = cap_io_page_table_cap_set_capIOPTMappedObject(cap, 0);
        ret.status = EXCEPTION_NONE;
        return ret;
#endif
    case cap_ipi_cap:
        ret.cap = cap;
        ret.status = EXCEPTION_NONE;
        return ret;

#ifdef CONFIG_VTX
    case cap_ept_page_directory_pointer_table_cap:
        ret.cap = cap;
        ret.status = EXCEPTION_NONE;
        return ret;
    case cap_ept_page_directory_cap:
        ret.cap = cap_ept_page_directory_cap_set_capPDMappedObject(cap, 0);
        ret.status = EXCEPTION_NONE;
        return ret;
    case cap_ept_page_table_cap:
        ret.cap = cap_ept_page_table_cap_set_capPTMappedObject(cap, 0);
        ret.status = EXCEPTION_NONE;
        return ret;
#endif
#ifdef CONFIG_VTX
    case cap_vcpu_cap:
        ret.cap = cap;
        ret.status = EXCEPTION_NONE;
        return ret;
#endif

    default:
        /* This assert has no equivalent in haskell,
         * as the options are restricted by type */
        fail("Invalid arch cap type");
    }
}

cap_t CONST Arch_updateCapData(bool_t preserve, word_t data, cap_t cap)
{
    switch (cap_get_capType(cap)) {
#ifdef CONFIG_IOMMU
    case cap_io_space_cap: {
        io_space_capdata_t w = { { data } };
        uint16_t PCIDevice = io_space_capdata_get_PCIDevice(w);
        uint16_t domainID = io_space_capdata_get_domainID(w);
        if (!preserve && cap_io_space_cap_get_capPCIDevice(cap) == 0 &&
                domainID >= ia32KSFirstValidIODomain &&
                domainID != 0                        &&
                domainID <= MASK(ia32KSnumIODomainIDBits)) {
            return cap_io_space_cap_new(domainID, PCIDevice);
        } else {
            return cap_null_cap_new();
        }
    }
#endif
    case cap_io_port_cap: {
        io_port_capdata_t w = { .words = { data } };
        uint16_t firstPort = io_port_capdata_get_firstPort(w);
        uint16_t lastPort = io_port_capdata_get_lastPort(w);
        uint16_t capFirstPort = cap_io_port_cap_get_capIOPortFirstPort(cap);
        uint16_t capLastPort = cap_io_port_cap_get_capIOPortLastPort(cap);
        assert(capFirstPort <= capLastPort);

        /* Ensure input data is ordered correctly. */
        if (firstPort > lastPort) {
            return cap_null_cap_new();
        }

        /* Allow the update if the new cap has range no larger than the old
         * cap. */
        if ((firstPort >= capFirstPort) && (lastPort <= capLastPort)) {
            return cap_io_port_cap_new(firstPort, lastPort);
        } else {
            return cap_null_cap_new();
        }
    }

    default:
        return cap;
    }
}

cap_t CONST Arch_maskCapRights(cap_rights_t cap_rights_mask, cap_t cap)
{
    if (cap_get_capType(cap) == cap_frame_cap) {
        vm_rights_t vm_rights;

        vm_rights = vmRightsFromWord(cap_frame_cap_get_capFVMRights(cap));
        vm_rights = maskVMRights(vm_rights, cap_rights_mask);
        return cap_frame_cap_set_capFVMRights(cap, wordFromVMRights(vm_rights));
    } else {
        return cap;
    }
}

static void finalisePDMappedFrame(cap_t cap)
{
    void *object = (void*)cap_frame_cap_get_capFMappedObject(cap);
    uint32_t index = cap_frame_cap_get_capFMappedIndex(cap);
    switch (cap_frame_cap_get_capFSize(cap)) {
    case IA32_SmallPage:
        unmapPageSmall(PT_PTR(object), index);
        flushPageSmall(PT_PTR(object), index);
        break;
    case IA32_LargePage:
        unmapPageLarge(PD_PTR(object), index);
        flushPageLarge(PD_PTR(object), index);
        break;
    default:
        fail("Unknown frame size");
    }
}

cap_t Arch_finaliseCap(cap_t cap, bool_t final)
{
    switch (cap_get_capType(cap)) {
    case cap_pdpt_cap:
        if (final) {
            pdpte_t *capPtr = PDPTE_PTR(cap_pdpt_cap_get_capPDPTBasePtr(cap));
            unmapAllPageDirectories(capPtr);
            flushAllPageDirectories(capPtr);
            clearMemory((void *)cap_get_capPtr(cap), cap_get_capSizeBits(cap));
            copyGlobalMappings(capPtr);
        }
        break;

    case cap_page_directory_cap:
        if (cap_page_directory_cap_get_capPDMappedObject(cap)) {
            unmapPageDirectory(
                PDPT_PTR(cap_page_directory_cap_get_capPDMappedObject(cap)),
                cap_page_directory_cap_get_capPDMappedIndex(cap),
                PD_PTR(cap_page_directory_cap_get_capPDBasePtr(cap))
            );
        }
        if (final) {
            unmapAllPageTables(
                PD_PTR(cap_page_directory_cap_get_capPDBasePtr(cap))
            );
            flushAllPageTables(PD_PTR(cap_page_directory_cap_get_capPDBasePtr(cap)));
            clearMemory((void *)cap_get_capPtr(cap), cap_get_capSizeBits(cap));
#ifndef CONFIG_PAE_PAGING
            copyGlobalMappings((void*)cap_get_capPtr(cap));
#endif
        }
        if (cap_page_directory_cap_get_capPDMappedObject(cap) || final) {
            invalidateTLB();
            invalidatePageStructureCache();
        }
        break;

    case cap_page_table_cap:
        if (cap_page_table_cap_get_capPTMappedObject(cap)) {
            unmapPageTable(
                PD_PTR(cap_page_table_cap_get_capPTMappedObject(cap)),
                cap_page_table_cap_get_capPTMappedIndex(cap)
            );
        }
        if (final) {
            unmapAllPages(
                PT_PTR(cap_page_table_cap_get_capPTBasePtr(cap))
            );
            clearMemory((void *)cap_get_capPtr(cap), cap_get_capSizeBits(cap));
        }
        if (cap_page_table_cap_get_capPTMappedObject(cap) || final) {
            flushTable(
                PD_PTR(cap_page_table_cap_get_capPTMappedObject(cap)),
                cap_page_table_cap_get_capPTMappedIndex(cap),
                PT_PTR(cap_page_table_cap_get_capPTBasePtr(cap))
            );
        }
        break;

    case cap_frame_cap:
        if (cap_frame_cap_get_capFMappedObject(cap)) {
            switch (cap_frame_cap_get_capFMappedType(cap)) {
            case IA32_MAPPING_PD:
                finalisePDMappedFrame(cap);
                break;
#ifdef CONFIG_VTX
            case IA32_MAPPING_EPT:
                /* The unmap function for EPT will unmap and flush */
                IA32PageUnmapEPT(cap);
                break;
#endif
#ifdef CONFIG_IOMMU
            case IA32_MAPPING_IO:
                unmapIOPage(cap);
                break;
#endif
            default:
                fail("Unknown mapping type for frame");
            }
        }
        break;

#ifdef CONFIG_VTX
    case cap_vcpu_cap:
        if (final) {
            vcpu_finalise(VCPU_PTR(cap_vcpu_cap_get_capVCPUPtr(cap)));
        }
        break;
#endif

    case cap_io_port_cap:
        break;
#ifdef CONFIG_IOMMU
    case cap_io_space_cap:
        if (final) {
            /* Unmap any first level page table. */
            unmapVTDContextEntry(cap);
        }
        break;
    case cap_io_page_table_cap: {
        unmapIOPTCap(cap);
        if (final) {
            unmapAllIOPT(VTD_PTE_PTR(cap_get_capPtr(cap)),
                         cap_io_page_table_cap_get_capIOPTLevel(cap));
            memzero((void*)cap_get_capPtr(cap), BIT(cap_get_capSizeBits(cap)));
            flushCacheRange(cap_get_capPtr(cap), cap_get_capSizeBits(cap));
            invalidate_iotlb();
        }
    }
    break;
#endif
    case cap_ipi_cap:
        break;

#ifdef CONFIG_VTX
    case cap_ept_page_directory_pointer_table_cap:
        if (final) {
            ept_pdpte_t *pdpt = EPT_PDPT_PTR(cap_ept_page_directory_pointer_table_cap_get_capPDPTBasePtr(cap));
            unmapAllEPTPD(pdpt);
            memzero(pdpt, BIT(EPT_PDPT_SIZE_BITS));
            invept((void*) ((uint32_t)pdpt - EPT_PDPT_OFFSET));
        }
        break;

    case cap_ept_page_directory_cap: {
        ept_pdpte_t *pdpt = EPT_PDPT_PTR(cap_ept_page_directory_cap_get_capPDMappedObject(cap));
        int index = cap_ept_page_directory_cap_get_capPDMappedIndex(cap);
        ept_pde_t *pd = EPT_PD_PTR(cap_get_capPtr(cap));
        if (pdpt) {
            unmapEPTPD(pdpt, index, pd);
        }
        if (final) {
            unmapAllEPTPT(pd);
            memzero((void *)cap_get_capPtr(cap), BIT(cap_get_capSizeBits(cap)));
        }
        if (pdpt && final) {
            invept((void*) ((uint32_t)pdpt - EPT_PDPT_OFFSET));
        }
        break;
    }

    case cap_ept_page_table_cap: {
        ept_pde_t *pd = EPT_PD_PTR(cap_ept_page_table_cap_get_capPTMappedObject(cap));
        int index = cap_ept_page_table_cap_get_capPTMappedIndex(cap);
        ept_pte_t *pt = EPT_PT_PTR(cap_get_capPtr(cap));
        if (pd) {
            unmapEPTPT(pd, index, pt);
        }
        if (final) {
            unmapAllEPTPages(pt);
            memzero((void *)cap_get_capPtr(cap), BIT(cap_get_capSizeBits(cap)));
        }
        if (pd && final) {
            ept_pdpte_t *pdpt;
            pdpt = lookupEPTPDPTFromPD(pd);
            if (pdpt) {
                invept((void*) ((uint32_t)pdpt - EPT_PDPT_OFFSET));
            }
        }
        break;
    }
#endif /* VTX */

    default:
        fail("Invalid arch cap type");
    }

    return cap_null_cap_new();
}

static cap_t CONST
resetMemMapping(cap_t cap)
{
    switch (cap_get_capType(cap)) {
    case cap_frame_cap:
        return cap_frame_cap_set_capFMappedObject(cap, 0);
    case cap_page_table_cap:
        return cap_page_table_cap_set_capPTMappedObject(cap, 0);
    case cap_page_directory_cap:
        return cap_page_directory_cap_set_capPDMappedObject(cap, 0);
#ifdef CONFIG_VTX
    case cap_ept_page_directory_cap:
        return cap_ept_page_directory_cap_set_capPDMappedObject(cap, 0);
    case cap_ept_page_table_cap:
        return cap_ept_page_table_cap_set_capPTMappedObject(cap, 0);
#endif
#ifdef CONFIG_IOMMU
    case cap_io_page_table_cap:
        return cap_io_page_table_cap_set_capIOPTMappedObject(cap, 0);
#endif
    default:
        break;
    }

    return cap;
}

cap_t Arch_recycleCap(bool_t is_final, cap_t cap)
{
    switch (cap_get_capType(cap)) {
    case cap_frame_cap:
        Arch_finaliseCap(cap, is_final);
        if (inKernelWindow((void *)cap_get_capPtr(cap))) {
            clearMemory((void *)cap_get_capPtr(cap), cap_get_capSizeBits(cap));
        }
        return resetMemMapping(cap);

    case cap_page_table_cap:
        Arch_finaliseCap(cap, true);
        return resetMemMapping(cap);

    case cap_page_directory_cap:
        Arch_finaliseCap(cap, true);
        return resetMemMapping(cap);

    case cap_pdpt_cap:
        Arch_finaliseCap(cap, true);
        return cap;

#ifdef CONFIG_VTX
    case cap_vcpu_cap:
        vcpu_finalise(VCPU_PTR(cap_vcpu_cap_get_capVCPUPtr(cap)));
        vcpu_init(VCPU_PTR(cap_vcpu_cap_get_capVCPUPtr(cap)));
        return cap;
#endif


    case cap_io_port_cap:
        return cap;
#ifdef CONFIG_IOMMU
    case cap_io_space_cap:
        Arch_finaliseCap(cap, true);
        return cap;

    case cap_io_page_table_cap:
        Arch_finaliseCap(cap, true);
        return resetMemMapping(cap);
#endif
    case cap_ipi_cap:
        return cap;

#ifdef CONFIG_VTX
    case cap_ept_page_directory_pointer_table_cap:
        Arch_finaliseCap(cap, true);
        return cap;

    case cap_ept_page_directory_cap:
    case cap_ept_page_table_cap:
        Arch_finaliseCap(cap, true);
        return resetMemMapping(cap);
#endif /* VTX */
    default:
        fail("Invalid arch cap type");
    }
}


bool_t CONST
Arch_hasRecycleRights(cap_t cap)
{
    switch (cap_get_capType(cap)) {
    case cap_frame_cap:
        return cap_frame_cap_get_capFVMRights(cap) == VMReadWrite;

    default:
        return true;
    }
}


bool_t CONST Arch_sameRegionAs(cap_t cap_a, cap_t cap_b)
{
    switch (cap_get_capType(cap_a)) {
    case cap_frame_cap:
        if (cap_get_capType(cap_b) == cap_frame_cap) {
            word_t botA, botB, topA, topB;
            botA = cap_frame_cap_get_capFBasePtr(cap_a);
            botB = cap_frame_cap_get_capFBasePtr(cap_b);
            topA = botA + MASK (pageBitsForSize(cap_frame_cap_get_capFSize(cap_a)));
            topB = botB + MASK (pageBitsForSize(cap_frame_cap_get_capFSize(cap_b)));
            return ((botA <= botB) && (topA >= topB) && (botB <= topB));
        }
        break;

    case cap_page_table_cap:
        if (cap_get_capType(cap_b) == cap_page_table_cap) {
            return cap_page_table_cap_get_capPTBasePtr(cap_a) ==
                   cap_page_table_cap_get_capPTBasePtr(cap_b);
        }
        break;

    case cap_page_directory_cap:
        if (cap_get_capType(cap_b) == cap_page_directory_cap) {
            return cap_page_directory_cap_get_capPDBasePtr(cap_a) ==
                   cap_page_directory_cap_get_capPDBasePtr(cap_b);
        }
        break;
    case cap_pdpt_cap:
        if (cap_get_capType(cap_b) == cap_pdpt_cap) {
            return cap_pdpt_cap_get_capPDPTBasePtr(cap_a) ==
                   cap_pdpt_cap_get_capPDPTBasePtr(cap_b);
        }
        break;

#ifdef CONFIG_VTX
    case cap_vcpu_cap:
        if (cap_get_capType(cap_b) == cap_vcpu_cap) {
            return cap_vcpu_cap_get_capVCPUPtr(cap_a) ==
                   cap_vcpu_cap_get_capVCPUPtr(cap_b);
        }
        break;
#endif

    case cap_io_port_cap:
        if (cap_get_capType(cap_b) == cap_io_port_cap) {
            return true;
        }
        break;
#ifdef CONFIG_IOMMU
    case cap_io_space_cap:
        if (cap_get_capType(cap_b) == cap_io_space_cap) {
            return cap_io_space_cap_get_capPCIDevice(cap_a) ==
                   cap_io_space_cap_get_capPCIDevice(cap_b);
        }
        break;

    case cap_io_page_table_cap:
        if (cap_get_capType(cap_b) == cap_io_page_table_cap) {
            return cap_io_page_table_cap_get_capIOPTBasePtr(cap_a) ==
                   cap_io_page_table_cap_get_capIOPTBasePtr(cap_b);
        }
        break;
#endif
    case cap_ipi_cap:
        if (cap_get_capType(cap_b) == cap_ipi_cap) {
            return true;
        }
        break;

#ifdef CONFIG_VTX
    case cap_ept_page_directory_pointer_table_cap:
        if (cap_get_capType(cap_b) == cap_ept_page_directory_pointer_table_cap) {
            return cap_ept_page_directory_pointer_table_cap_get_capPDPTBasePtr(cap_a) ==
                   cap_ept_page_directory_pointer_table_cap_get_capPDPTBasePtr(cap_b);
        }
        break;
    case cap_ept_page_directory_cap:
        if (cap_get_capType(cap_b) == cap_ept_page_directory_cap) {
            return cap_ept_page_directory_cap_get_capPDBasePtr(cap_a) ==
                   cap_ept_page_directory_cap_get_capPDBasePtr(cap_b);
        }
        break;
    case cap_ept_page_table_cap:
        if (cap_get_capType(cap_b) == cap_ept_page_table_cap) {
            return cap_ept_page_table_cap_get_capPTBasePtr(cap_a) ==
                   cap_ept_page_table_cap_get_capPTBasePtr(cap_b);
        }
        break;
#endif /* VTX */
    }

    return false;
}

bool_t CONST Arch_sameObjectAs(cap_t cap_a, cap_t cap_b)
{
    if (cap_get_capType(cap_a) == cap_frame_cap) {
        if (cap_get_capType(cap_b) == cap_frame_cap) {
            return ((cap_frame_cap_get_capFBasePtr(cap_a) ==
                     cap_frame_cap_get_capFBasePtr(cap_b)) &&
                    (cap_frame_cap_get_capFSize(cap_a) ==
                     cap_frame_cap_get_capFSize(cap_b)));
        }
    }
    return Arch_sameRegionAs(cap_a, cap_b);
}

word_t
Arch_getObjectSize(word_t t)
{
    switch (t) {
    case seL4_IA32_4K:
        return pageBitsForSize(IA32_SmallPage);
    case seL4_IA32_LargePage:
        return pageBitsForSize(IA32_LargePage);
    case seL4_IA32_PageTableObject:
        return PTE_SIZE_BITS + PT_BITS;
    case seL4_IA32_PageDirectoryObject:
        return PDE_SIZE_BITS + PD_BITS;
    case seL4_IA32_PDPTObject:
        return PDPTE_SIZE_BITS + PDPT_BITS;
#ifdef CONFIG_IOMMU
    case seL4_IA32_IOPageTableObject:
        return VTD_PT_SIZE_BITS;
#endif
#ifdef CONFIG_VTX
    case seL4_IA32_VCPUObject:
        return VTX_VCPU_BITS;
    case seL4_IA32_EPTPageDirectoryPointerTableObject:
        return EPT_PDPTE_SIZE_BITS + EPT_PDPT_BITS + 1;
    case seL4_IA32_EPTPageDirectoryObject:
        return EPT_PDE_SIZE_BITS + EPT_PD_BITS;
    case seL4_IA32_EPTPageTableObject:
        return EPT_PTE_SIZE_BITS + EPT_PT_BITS;
#endif
    default:
        fail("Invalid object type");
        return 0;
    }
}

bool_t
Arch_isFrameType(word_t t)
{
    switch (t) {
    case seL4_IA32_4K:
        return true;
    case seL4_IA32_LargePage:
        return true;
    default:
        return false;
    }
}

cap_t
Arch_createObject(object_t t, void *regionBase, unsigned int userSize, bool_t deviceMemory)
{
    switch (t) {
    case seL4_IA32_4K:
        if (!deviceMemory) {
            memzero(regionBase, 1 << pageBitsForSize(IA32_SmallPage));
        }
        return cap_frame_cap_new(
                   IA32_SmallPage,         /* capFSize             */
                   0,                      /* capFMappedObject     */
                   0,                      /* capFMappedIndex      */
                   IA32_MAPPING_PD,        /* capFMappedType       */
                   VMReadWrite,            /* capFVMRights         */
                   (word_t)regionBase      /* capFBasePtr          */
               );

    case seL4_IA32_LargePage:
        if (!deviceMemory) {
            memzero(regionBase, 1 << pageBitsForSize(IA32_LargePage));
        }
        return cap_frame_cap_new(
                   IA32_LargePage,         /* capFSize             */
                   0,                      /* capFMappedObject     */
                   0,                      /* capFMappedIndex      */
                   IA32_MAPPING_PD,        /* capFMappedType       */
                   VMReadWrite,            /* capFVMRights         */
                   (word_t)regionBase      /* capFBasePtr          */
               );

    case seL4_IA32_PageTableObject:
        memzero(regionBase, 1 << PT_SIZE_BITS);
        return cap_page_table_cap_new(
                   0,                  /* capPTMappedObject    */
                   0,                  /* capPTMappedIndex     */
                   (word_t)regionBase  /* capPTBasePtr         */
               );

    case seL4_IA32_PageDirectoryObject:
        memzero(regionBase, 1 << PD_SIZE_BITS);
#ifndef CONFIG_PAE_PAGING
        copyGlobalMappings(regionBase);
#endif
        return cap_page_directory_cap_new(
                   0,                  /* capPDmappedObject */
                   0,                  /* capPDMappedIndex  */
                   (word_t)regionBase  /* capPDBasePtr      */
               );

#ifdef CONFIG_PAE_PAGING
    case seL4_IA32_PDPTObject:
        memzero(regionBase, 1 << PDPT_SIZE_BITS);
        copyGlobalMappings(regionBase);

        return cap_pdpt_cap_new(
                   (word_t)regionBase  /* capPDPTBasePtr */
               );
#endif

#ifdef CONFIG_IOMMU
    case seL4_IA32_IOPageTableObject:
        memzero(regionBase, 1 << VTD_PT_SIZE_BITS);
        return cap_io_page_table_cap_new(
                   0,  /* CapIOPTMappedLevel   */
                   0,  /* capIOPTMappedObject  */
                   0,  /* capIOPTMappedIndex   */
                   (word_t)regionBase  /* capIOPTBasePtr */
               );
#endif
#ifdef CONFIG_VTX
    case seL4_IA32_VCPUObject: {
        vcpu_t *vcpu;
        if (!vtx_enabled) {
            fail("vtx not enabled");
        }
        memzero(regionBase, 1 << VTX_VCPU_BITS);
        vcpu = VCPU_PTR((word_t)regionBase);
        vcpu_init(vcpu);
        return cap_vcpu_cap_new(VCPU_REF(vcpu));
    }
    case seL4_IA32_EPTPageDirectoryPointerTableObject: {
        ept_pml4e_t *pml4;
        memzero(regionBase, 1 << (EPT_PDPTE_SIZE_BITS + EPT_PDPT_BITS + 1));
        pml4 = (ept_pml4e_t*)((word_t)regionBase);
        IA32EptPdpt_Init(pml4);
        return cap_ept_page_directory_pointer_table_cap_new(
                   (word_t)regionBase + EPT_PDPT_OFFSET /* capPTBasePtr   */);
    }
    case seL4_IA32_EPTPageDirectoryObject:
        memzero(regionBase, 1 << (EPT_PDE_SIZE_BITS + EPT_PD_BITS));

        return cap_ept_page_directory_cap_new(
                   0,                  /* capPDMappedObject    */
                   0,                  /* capPDMappedIndex     */
                   (word_t)regionBase  /* capPTBasePtr         */
               );
    case seL4_IA32_EPTPageTableObject:
        memzero(regionBase, 1 << (EPT_PTE_SIZE_BITS + EPT_PT_BITS));

        return cap_ept_page_table_cap_new(
                   0,                  /* capPTMappedObject    */
                   0,                  /* capPTMappedIndex     */
                   (word_t)regionBase  /* capPTBasePtr         */
               );
#endif /* VTX */

    default:
        /*
         * This is a conflation of the haskell error: "Arch.createNewCaps
         * got an API type" and the case where an invalid object type is
         * passed (which is impossible in haskell).
         */
        fail("Arch_createObject got an API type or invalid object type");
    }
}

exception_t
Arch_decodeInvocation(
    word_t label,
    unsigned int length,
    cptr_t cptr,
    cte_t* slot,
    cap_t cap,
    extra_caps_t extraCaps,
    word_t* buffer
)
{
    switch (cap_get_capType(cap)) {
    case cap_pdpt_cap:
    case cap_page_directory_cap:
    case cap_page_table_cap:
    case cap_frame_cap:
        return decodeIA32MMUInvocation(label, length, cptr, slot, cap, extraCaps, buffer);

    case cap_io_port_cap:
        return decodeIA32PortInvocation(label, length, cptr, slot, cap, extraCaps, buffer);
#ifdef CONFIG_IOMMU
    case cap_io_space_cap:
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;
        return EXCEPTION_SYSCALL_ERROR;
    case cap_io_page_table_cap:
        return decodeIA32IOPTInvocation(label, length, slot, cap, extraCaps, buffer);
#endif
    case cap_ipi_cap:
        return decodeIA32IPIInvocation(label, length, cptr, slot, cap, extraCaps, buffer);

#ifdef CONFIG_VTX
    case cap_ept_page_directory_pointer_table_cap:
    case cap_ept_page_directory_cap:
    case cap_ept_page_table_cap:
        return decodeIA32EPTInvocation(label, length, cptr, slot, cap, extraCaps, buffer);
#endif

#ifdef CONFIG_VTX
    case cap_vcpu_cap:
        return decodeIA32VCPUInvocation(label, length, cptr, slot, cap, extraCaps, buffer);
#endif
    default:
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;
        return EXCEPTION_SYSCALL_ERROR;
    }
}

void
Arch_prepareThreadDelete(tcb_t *thread)
{
    /* Notify the lazy FPU module about this thread's deletion. */
    Arch_fpuThreadDelete(thread);

#ifdef CONFIG_VTX
    if (thread->tcbArch.vcpu) {
        dissociateVcpuTcb(thread, thread->tcbArch.vcpu);
    }
#endif
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/object/tcb.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>
#include <types.h>
#include <api/failures.h>
#include <machine/registerset.h>
#include <object/structures.h>
#include <arch/machine.h>
#include <arch/object/tcb.h>

/* NOTE: offset is either 1 or 3 */
static inline unsigned int
setMRs_lookup_failure(tcb_t *receiver, word_t* receiveIPCBuffer, lookup_fault_t luf, unsigned int offset)
{
    word_t lufType = lookup_fault_get_lufType(luf);

    assert(n_msgRegisters == 2);

    if (offset < n_msgRegisters) {
        setRegister(receiver, msgRegisters[offset], lufType + 1);
    }

    if (!receiveIPCBuffer) {
        return n_msgRegisters;
    }

    if (offset >= n_msgRegisters) {
        receiveIPCBuffer[offset + 1] = lufType + 1;
    }

    switch (lufType) {
    case lookup_fault_invalid_root:
        return offset + 1;

    case lookup_fault_missing_capability:
        receiveIPCBuffer[offset + 2] =
            lookup_fault_missing_capability_get_bitsLeft(luf);
        return offset + 2;

    case lookup_fault_depth_mismatch:
        receiveIPCBuffer[offset + 2] =
            lookup_fault_depth_mismatch_get_bitsLeft(luf);
        receiveIPCBuffer[offset + 3] =
            lookup_fault_depth_mismatch_get_bitsFound(luf);
        return offset + 3;

    case lookup_fault_guard_mismatch:
        receiveIPCBuffer[offset + 2] =
            lookup_fault_guard_mismatch_get_bitsLeft(luf);
        receiveIPCBuffer[offset + 3] =
            lookup_fault_guard_mismatch_get_guardFound(luf);
        receiveIPCBuffer[offset + 4] =
            lookup_fault_guard_mismatch_get_bitsFound(luf);
        return offset + 4;

    default:
        fail("Invalid lookup failure");
    }
}

unsigned int setMRs_fault(tcb_t *sender, tcb_t* receiver, word_t *receiveIPCBuffer)
{
    assert(n_msgRegisters == 2);

    switch (fault_get_faultType(sender->tcbFault)) {
    case fault_cap_fault:
        setRegister(receiver, msgRegisters[0], getRestartPC(sender));
        setRegister(receiver, msgRegisters[1],
                    fault_cap_fault_get_address(sender->tcbFault));
        if (!receiveIPCBuffer) {
            return n_msgRegisters;
        }
        receiveIPCBuffer[2 + 1] =
            fault_cap_fault_get_inReceivePhase(sender->tcbFault);
        return setMRs_lookup_failure(receiver, receiveIPCBuffer, sender->tcbLookupFailure, 3);

    case fault_vm_fault:
        setRegister(receiver, msgRegisters[0], getRestartPC(sender));
        setRegister(receiver, msgRegisters[1],
                    fault_vm_fault_get_address(sender->tcbFault));
        if (!receiveIPCBuffer) {
            return n_msgRegisters;
        }
        receiveIPCBuffer[2 + 1] =
            fault_vm_fault_get_instructionFault(sender->tcbFault);
        receiveIPCBuffer[3 + 1] = fault_vm_fault_get_FSR(sender->tcbFault);
        return 4;

    case fault_unknown_syscall: {
        unsigned int i;

        for (i = 0; i < n_msgRegisters; i++) {
            setRegister(receiver, msgRegisters[i],
                        getRegister(sender, syscallMessage[i]));
        }
        if (receiveIPCBuffer) {
            for (; i < n_syscallMessage; i++) {
                receiveIPCBuffer[i + 1] =
                    getRegister(sender, syscallMessage[i]);
            }

            receiveIPCBuffer[i + 1] =
                fault_unknown_syscall_get_syscallNumber(sender->tcbFault);
            return n_syscallMessage + 1;
        } else {
            return n_msgRegisters;
        }
    }

    case fault_user_exception: {
        unsigned int i;

        for (i = 0; i < n_msgRegisters; i++) {
            setRegister(receiver, msgRegisters[i],
                        getRegister(sender, exceptionMessage[i]));
        }
        if (receiveIPCBuffer) {
            for (; i < n_exceptionMessage; i++) {
                receiveIPCBuffer[i + 1] =
                    getRegister(sender, exceptionMessage[i]);
            }
            receiveIPCBuffer[n_exceptionMessage + 1] =
                fault_user_exception_get_number(sender->tcbFault);
            receiveIPCBuffer[n_exceptionMessage + 2] =
                fault_user_exception_get_code(sender->tcbFault);
            return n_exceptionMessage + 2;
        } else {
            return n_msgRegisters;
        }
    }

    default:
        fail("Invalid fault");
    }
}

unsigned int setMRs_syscall_error(tcb_t *thread, word_t *receiveIPCBuffer)
{
    assert(n_msgRegisters >= 2);

    switch (current_syscall_error.type) {
    case seL4_InvalidArgument:
        setRegister(thread, msgRegisters[0],
                    current_syscall_error.invalidArgumentNumber);
        return 1;

    case seL4_InvalidCapability:
        setRegister(thread, msgRegisters[0],
                    current_syscall_error.invalidCapNumber);
        return 1;

    case seL4_IllegalOperation:
        return 0;

    case seL4_RangeError:
        setRegister(thread, msgRegisters[0],
                    current_syscall_error.rangeErrorMin);
        setRegister(thread, msgRegisters[1],
                    current_syscall_error.rangeErrorMax);
        return 2;

    case seL4_AlignmentError:
        return 0;

    case seL4_FailedLookup:
        setRegister(thread, msgRegisters[0],
                    current_syscall_error.failedLookupWasSource ? 1 : 0);
        return setMRs_lookup_failure(thread, receiveIPCBuffer,
                                     current_lookup_fault, 1);

    case seL4_TruncatedMessage:
    case seL4_DeleteFirst:
    case seL4_RevokeFirst:
        return 0;
    case seL4_NotEnoughMemory:
        setRegister(thread, msgRegisters[0],
                    current_syscall_error.memoryLeft);
        return 0;
    default:
        fail("Invalid syscall error");
    }
}

word_t CONST Arch_decodeTransfer(word_t flags)
{
    return 0;
}

exception_t CONST Arch_performTransfer(word_t arch, tcb_t *tcb_src, tcb_t *tcb_dest)
{
    return EXCEPTION_NONE;
}

void Arch_leaveVMAsyncTransfer(tcb_t *tcb)
{
#ifdef CONFIG_VTX
    vcpu_t *vcpu = tcb->tcbArch.vcpu;
    word_t *buffer;
    if (vcpu) {
        if (current_vmcs != vcpu) {
            vmptrld(vcpu);
        }

        setRegister(tcb, msgRegisters[0], vmread(VMX_GUEST_RIP));
        setRegister(tcb, msgRegisters[1], vmread(VMX_CONTROL_PRIMARY_PROCESSOR_CONTROLS));
        buffer = lookupIPCBuffer(true, tcb);
        if (!buffer) {
            return;
        }

        buffer[3] = vmread(VMX_CONTROL_ENTRY_INTERRUPTION_INFO);
    }
#endif
}

#ifdef CONFIG_VTX
exception_t decodeSetEPTRoot(cap_t cap, extra_caps_t extraCaps)
{
    tcb_t *tcb;
    cte_t *rootSlot;
    exception_t e;

    if (extraCaps.excaprefs[0] == NULL) {
        userError("TCB SetEPTRoot: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (cap_get_capType(extraCaps.excaprefs[0]->cap) != cap_ept_page_directory_pointer_table_cap) {
        userError("TCB SetEPTRoot: EPT PDPT is invalid.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    tcb = TCB_PTR(cap_thread_cap_get_capTCBPtr(cap));
    rootSlot = TCB_PTR_CTE_PTR(tcb, tcbArchEPTRoot);
    e = cteDelete(rootSlot, true);
    if (e != EXCEPTION_NONE) {
        return e;
    }

    cteInsert(extraCaps.excaprefs[0]->cap, extraCaps.excaprefs[0], rootSlot);

    setThreadState(ksCurThread, ThreadState_Restart);
    return EXCEPTION_NONE;
}
#endif
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/object/vcpu.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>

#ifdef CONFIG_VTX

#include <types.h>
#include <machine/io.h>
#include <api/failures.h>
#include <api/syscall.h>
#include <kernel/thread.h>
#include <object/objecttype.h>
#include <arch/machine/cpu_registers.h>
#include <arch/model/statedata.h>
#include <arch/object/vcpu.h>
#include <arch/object/vtx.h>
#include <util.h>

#define MSR_VMX_PINBASED_CTLS 0x481
#define MSR_VMX_PROCBASED_CTLS 0x482
#define MSR_VMX_PROCBASED_CTLS2 0x48B
#define MSR_VMX_EXIT_CTLS 0x483
#define MSR_VMX_ENTRY_CTLS 0x484
#define MSR_VMX_TRUE_PINBASED_CTLS 0x48D
#define MSR_VMX_TRUE_PROCBASED_CTLS 0x48E
#define MSR_VMX_TRUE_EXIT_CTLS 0x48F
#define MSR_VMX_TRUE_ENTRY_CTLS 0x490
#define MSR_VMX_CR0_FIXED0 0x486
#define MSR_VMX_CR0_FIXED1 0x487
#define MSR_VMX_CR4_FIXED0 0x488
#define MSR_VMX_CR4_FIXED1 0x489


/* Store fixed field values. */
uint32_t pin_control_high;
uint32_t pin_control_low;
uint32_t primary_control_high;
uint32_t primary_control_low;
uint32_t secondary_control_high;
uint32_t secondary_control_low;
uint32_t entry_control_high;
uint32_t entry_control_low;
uint32_t exit_control_high;
uint32_t exit_control_low;
uint32_t cr0_high;
uint32_t cr0_low;
uint32_t cr4_high;
uint32_t cr4_low;

static void
applyHardwareFixedBits(uint32_t msr, uint32_t* high, uint32_t* low)
{
    uint32_t old_high UNUSED, old_low UNUSED;
    old_high = *high;
    old_low = *low;
    *high |= ia32_rdmsr_low(msr);
    *low &= ia32_rdmsr_high(msr);
}

static void
applyHardwareFixedBitsSplit(uint32_t msr_high, uint32_t msr_low, uint32_t* high, uint32_t* low)
{
    uint32_t old_high UNUSED, old_low UNUSED;
    old_high = *high;
    old_low = *low;
    *high |= ia32_rdmsr_low(msr_high);
    *low &= ia32_rdmsr_low(msr_low);
}

bool_t
init_vtx_fixed_values(bool_t useTrueMsrs)
{
    uint32_t msr_true_offset = useTrueMsrs ? MSR_VMX_TRUE_PINBASED_CTLS - MSR_VMX_PINBASED_CTLS : 0;
    const uint32_t pin_control_mask =
        BIT(0) | //Extern interrlt exiting
        BIT(3) | //NMI exiting
        BIT(5); //virtual NMIs
    const uint32_t primary_control_mask =
        BIT(25) | //Use I/O bitmaps
        BIT(28) | //Use MSR bitmaps
        BIT(31); //Activate secondary controls
    const uint32_t secondary_control_mask =
        BIT(1) | //Enable EPT
        BIT(5); //Enable VPID
    pin_control_high = pin_control_mask;
    pin_control_low = ~0;
    applyHardwareFixedBits(MSR_VMX_PINBASED_CTLS + msr_true_offset, &pin_control_high, &pin_control_low);
    if ((pin_control_low & pin_control_mask) != pin_control_mask) {
        return false;
    }
    primary_control_high = primary_control_mask;
    primary_control_low = ~0;
    applyHardwareFixedBits(MSR_VMX_PROCBASED_CTLS + msr_true_offset, &primary_control_high, &primary_control_low);
    if ((primary_control_low & primary_control_mask) != primary_control_mask) {
        return false;
    }
    secondary_control_high = secondary_control_mask;
    secondary_control_low = ~0;
    applyHardwareFixedBits(MSR_VMX_PROCBASED_CTLS2, &secondary_control_high, &secondary_control_low);
    if ((secondary_control_low & secondary_control_mask) != secondary_control_mask) {
        return false;
    }
    exit_control_high = BIT(15); //Acknowledge interrupt on exit
    exit_control_low = ~0;
    applyHardwareFixedBits(MSR_VMX_EXIT_CTLS + msr_true_offset, &exit_control_high, &exit_control_low);
    if ((exit_control_low & BIT(15)) != BIT(15)) {
        return false;
    }
    entry_control_high = 0;
    entry_control_low = ~0;
    applyHardwareFixedBits(MSR_VMX_ENTRY_CTLS + msr_true_offset, &entry_control_high, &entry_control_low);
    cr0_high = 0;
    cr0_low = ~0;
    applyHardwareFixedBitsSplit(0x486, 0x487, &cr0_high, &cr0_low);
    cr4_high = 0;
    cr4_low = ~0;
    applyHardwareFixedBitsSplit(0x488, 0x489, &cr4_high, &cr4_low);
    return true;
}

static uint32_t
applyFixedBits(uint32_t original, uint32_t high, uint32_t low)
{
    original |= high;
    original &= low;
    return original;
}

void
vcpu_init(vcpu_t *vcpu)
{
    uint32_t *vmcs = (uint32_t*)vcpu;
    vcpu->tcb = NULL;
    vcpu->launched = false;

    *vmcs = vmcs_revision;

    vmclear(vcpu);
    vmptrld(vcpu);

    /* Set fixed host state. */
    /*vmwrite(VMX_HOST_PAT, 0);
    vmwrite(VMX_HOST_EFER, 0);
    vmwrite(VMX_HOST_PERF_GLOBAL_CTRL, 0);*/
    vmwrite(VMX_HOST_CR0, read_cr0());
    /* CR3 is set dynamically. */
    vmwrite(VMX_HOST_CR4, read_cr4());
    vmwrite(VMX_HOST_FS_BASE, 0);
    vmwrite(VMX_HOST_GS_BASE, 0);
    vmwrite(VMX_HOST_TR_BASE, (uint32_t)&ia32KStss);
    vmwrite(VMX_HOST_GDTR_BASE, (uint32_t)ia32KSgdt);
    vmwrite(VMX_HOST_IDTR_BASE, (uint32_t)ia32KSidt);
    vmwrite(VMX_HOST_SYSENTER_CS, (uint32_t)SEL_CS_0);
    vmwrite(VMX_HOST_SYSENTER_EIP, (uint32_t)&handle_syscall);
    vmwrite(VMX_HOST_SYSENTER_ESP, (uint32_t)&ia32KStss.words[1]);
    /* Set host SP to point just beyond the first field to be stored on exit. */
    vmwrite(VMX_HOST_RSP, (uint32_t)&vcpu->gp_registers[EBP + 1]);
    vmwrite(VMX_HOST_RIP, (uint32_t)&handle_vmexit);

    vmwrite(VMX_HOST_ES_SELECTOR, SEL_DS_0);
    vmwrite(VMX_HOST_CS_SELECTOR, SEL_CS_0);
    vmwrite(VMX_HOST_SS_SELECTOR, SEL_DS_0);
    vmwrite(VMX_HOST_DS_SELECTOR, SEL_DS_0);
    vmwrite(VMX_HOST_FS_SELECTOR, 0);
    vmwrite(VMX_HOST_GS_SELECTOR, 0);
    vmwrite(VMX_HOST_TR_SELECTOR, SEL_TSS);

    /* Set fixed VMCS control fields. */
    vmwrite(VMX_CONTROL_PIN_EXECUTION_CONTROLS, applyFixedBits(0, pin_control_high, pin_control_low));
    vmwrite(VMX_CONTROL_PRIMARY_PROCESSOR_CONTROLS, applyFixedBits(0, primary_control_high, primary_control_low));
    vmwrite(VMX_CONTROL_SECONDARY_PROCESSOR_CONTROLS, applyFixedBits(0, secondary_control_high, secondary_control_low));
    vmwrite(VMX_CONTROL_EXIT_CONTROLS, applyFixedBits(0, exit_control_high, exit_control_low));
    vmwrite(VMX_CONTROL_ENTRY_CONTROLS, applyFixedBits(0, entry_control_high, entry_control_low));
    vmwrite(VMX_CONTROL_MSR_ADDRESS, (uint32_t)pptr_to_paddr(msr_bitmap));
    vmwrite(VMX_GUEST_CR0, applyFixedBits(0, cr0_high, cr0_low));
    vmwrite(VMX_GUEST_CR4, applyFixedBits(0, cr4_high, cr4_low));
    /* VPID is the same as the VCPU */
    vmwrite(VMX_CONTROL_VPID, vpid_for_vcpu(vcpu));

    vmwrite(VMX_GUEST_VMCS_LINK_POINTER, ~0);
    vmwrite(VMX_GUEST_VMCS_LINK_POINTER_HIGH, ~0);

    memset(vcpu->io, ~0, 8192);
    vmwrite(VMX_CONTROL_IOA_ADDRESS, pptr_to_paddr(vcpu->io));
    vmwrite(VMX_CONTROL_IOB_ADDRESS, pptr_to_paddr((char *)vcpu->io + 4096));
    vcpu->io_min = -1;
    vcpu->io_max = -1;
    vcpu->cr0 = applyFixedBits(0, cr0_high, cr0_low);
    vcpu->cr0_shadow = 0;
    vcpu->cr0_mask = 0;
    vcpu->exception_mask = 0;
}

void
vcpu_finalise(vcpu_t *vcpu)
{
    if (vcpu->tcb) {
        dissociateVcpuTcb(vcpu->tcb, vcpu);
    }
    if (current_vmcs == vcpu) {
        current_vmcs = NULL;
    }
    vmclear(vcpu);
}

void
associateVcpuTcb(tcb_t *tcb, vcpu_t *vcpu)
{
    if (tcb->tcbArch.vcpu) {
        dissociateVcpuTcb(tcb, tcb->tcbArch.vcpu);
    }
    if (vcpu->tcb) {
        dissociateVcpuTcb(vcpu->tcb, vcpu);
    }
    vcpu->tcb = tcb;
    tcb->tcbArch.vcpu = vcpu;
}

void
dissociateVcpuTcb(tcb_t *tcb, vcpu_t *vcpu)
{
    if (tcb->tcbArch.vcpu != vcpu || vcpu->tcb != tcb) {
        fail("TCB and VCPU not associated.");
    }
    tcb->tcbArch.vcpu = NULL;
    vcpu->tcb = NULL;
}

exception_t decodeIA32VCPUInvocation(
    word_t label,
    unsigned int length,
    cptr_t cptr,
    cte_t* slot,
    cap_t cap,
    extra_caps_t extraCaps,
    word_t* buffer
)
{
    switch (label) {
    case IA32VCPUSetTCB:
        return decodeSetTCB(cap, length, buffer, extraCaps);
    case IA32VCPUReadVMCS:
        return decodeReadVMCS(cap, length, buffer);
    case IA32VCPUWriteVMCS:
        return decodeWriteVMCS(cap, length, buffer);
    case IA32VCPUSetIOPort:
        return decodeSetIOPort(cap, length, buffer, extraCaps);
    case IA32VCPUSetIOPortMask:
        return decodeSetIOPortMask(cap, length, buffer);
    case IA32VCPUWriteRegisters:
        return decodeVCPUWriteRegisters(cap, length, buffer);
    default:
        userError("VCPU: Illegal operation.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }
}

exception_t
decodeVCPUWriteRegisters(cap_t cap, unsigned int length, word_t *buffer)
{
    vcpu_t *vcpu;
    if (length < 7) {
        userError("VCPU WriteRegisters: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }
    vcpu = VCPU_PTR(cap_vcpu_cap_get_capVCPUPtr(cap));
    setThreadState(ksCurThread, ThreadState_Restart);
    return invokeVCPUWriteRegisters(vcpu, buffer);
}

exception_t
decodeSetIOPortMask(cap_t cap, unsigned int length, word_t *buffer)
{
    uint32_t low, high;
    int mask;
    vcpu_t *vcpu;
    if (length < 3) {
        userError("VCPU SetIOPortMask: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }
    low = getSyscallArg(0, buffer);
    high = getSyscallArg(1, buffer);
    mask = getSyscallArg(2, buffer) == 0 ? 0 : 1;
    vcpu = VCPU_PTR(cap_vcpu_cap_get_capVCPUPtr(cap));
    if (low < vcpu->io_min || high > vcpu->io_max) {
        userError("VCPU SetIOPortMask: Invalid range.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }
    if (vcpu->io_min == -1 || vcpu->io_max == -1) {
        userError("VCPU SetIOPortMask: No IO port set.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }
    setThreadState(ksCurThread, ThreadState_Restart);
    return invokeSetIOPortMask(vcpu, low, high, mask);
}

exception_t
invokeSetIOPortMask(vcpu_t *vcpu, uint32_t low, uint32_t high, int mask)
{
    while (low <= high) {
        /* See if we can optimize a whole word of bits */
        if (low % 32 == 0 && low / 32 != high / 32) {
            vcpu->io[low / 32] = mask ? ~0 : 0;
            low += 32;
        } else {
            if (mask) {
                vcpu->io[low / 32] |= BIT(low % 32);
            } else {
                vcpu->io[low / 32] &= ~BIT(low % 32);
            }
            low++;
        }
    }
    return EXCEPTION_NONE;
}

exception_t
decodeSetIOPort(cap_t cap, unsigned int length, word_t* buffer, extra_caps_t extraCaps)
{
    cap_t ioCap;
    cte_t *tcbSlot;
    deriveCap_ret_t dc_ret;
    if (extraCaps.excaprefs[0] == NULL) {
        userError("VCPU SetIOPort: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }
    tcbSlot = extraCaps.excaprefs[0];
    ioCap  = extraCaps.excaprefs[0]->cap;

    dc_ret = deriveCap(tcbSlot, ioCap);
    if (dc_ret.status != EXCEPTION_NONE) {
        return dc_ret.status;
    }
    ioCap = dc_ret.cap;
    if (cap_get_capType(ioCap) != cap_io_port_cap) {
        userError("IOPort cap is not a IOPort cap.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    setThreadState(ksCurThread, ThreadState_Restart);
    return invokeSetIOPort(VCPU_PTR(cap_vcpu_cap_get_capVCPUPtr(cap)), ioCap);
}

exception_t
invokeVCPUWriteRegisters(vcpu_t *vcpu, word_t *buffer)
{
    int i;
    for (i = 0; i <= EBP; i++) {
        vcpu->gp_registers[i] = getSyscallArg(i, buffer);
    }
    return EXCEPTION_NONE;
}

exception_t
invokeSetIOPort(vcpu_t *vcpu, cap_t cap)
{
    uint32_t high, low;
    vcpu->io_port = cap;
    low = cap_io_port_cap_get_capIOPortFirstPort(cap);
    high = cap_io_port_cap_get_capIOPortLastPort(cap) + 1;
    // Set the range
    vcpu->io_min = low;
    vcpu->io_max = high;
    // Clear the IO ports
    /* There is no point clearing the IO ports as we have no
     * security model anyway, so might as well let multiple
     * io ports be set to allow different ranges to be
     * masked */
//    memset(vcpu->io, ~0, 8192);
    return EXCEPTION_NONE;
}

#define MAX_VMCS_FIELDS 32

exception_t
decodeWriteVMCS(cap_t cap, unsigned int length, word_t* buffer)
{
    uint32_t fields[MAX_VMCS_FIELDS];
    uint32_t values[MAX_VMCS_FIELDS];
    int num_fields;
    int i;
    if (length > MAX_VMCS_FIELDS * 2) {
        userError("VCPU WriteVMCS: Too many arguments.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }
    num_fields = length / 2;
    for (i = 0; i < num_fields; i++) {
        uint32_t field = getSyscallArg(i * 2 + 0, buffer);
        uint32_t value = getSyscallArg(i * 2 + 1, buffer);
        switch (field) {
        case VMX_GUEST_RIP:
        case VMX_GUEST_RSP:
        case VMX_GUEST_ES_SELECTOR:
        case VMX_GUEST_CS_SELECTOR:
        case VMX_GUEST_SS_SELECTOR:
        case VMX_GUEST_DS_SELECTOR:
        case VMX_GUEST_FS_SELECTOR:
        case VMX_GUEST_GS_SELECTOR:
        case VMX_GUEST_LDTR_SELECTOR:
        case VMX_GUEST_TR_SELECTOR:
        case VMX_GUEST_DEBUGCTRL:
        case VMX_GUEST_PAT:
        case VMX_GUEST_EFER:
        case VMX_GUEST_PERF_GLOBAL_CTRL:
        case VMX_GUEST_PDPTE0:
        case VMX_GUEST_PDPTE1:
        case VMX_GUEST_PDPTE2:
        case VMX_GUEST_PDPTE3:
        case VMX_GUEST_ES_LIMIT:
        case VMX_GUEST_CS_LIMIT:
        case VMX_GUEST_SS_LIMIT:
        case VMX_GUEST_DS_LIMIT:
        case VMX_GUEST_FS_LIMIT:
        case VMX_GUEST_GS_LIMIT:
        case VMX_GUEST_LDTR_LIMIT:
        case VMX_GUEST_TR_LIMIT:
        case VMX_GUEST_GDTR_LIMIT:
        case VMX_GUEST_IDTR_LIMIT:
        case VMX_GUEST_ES_ACCESS_RIGHTS:
        case VMX_GUEST_CS_ACCESS_RIGHTS:
        case VMX_GUEST_SS_ACCESS_RIGHTS:
        case VMX_GUEST_DS_ACCESS_RIGHTS:
        case VMX_GUEST_FS_ACCESS_RIGHTS:
        case VMX_GUEST_GS_ACCESS_RIGHTS:
        case VMX_GUEST_LDTR_ACCESS_RIGHTS:
        case VMX_GUEST_TR_ACCESS_RIGHTS:
        case VMX_GUEST_INTERRUPTABILITY:
        case VMX_GUEST_ACTIVITY:
        case VMX_GUEST_SMBASE:
        case VMX_GUEST_SYSENTER_CS:
        case VMX_GUEST_PREEMPTION_TIMER_VALUE:
        case VMX_GUEST_ES_BASE:
        case VMX_GUEST_CS_BASE:
        case VMX_GUEST_SS_BASE:
        case VMX_GUEST_DS_BASE:
        case VMX_GUEST_FS_BASE:
        case VMX_GUEST_GS_BASE:
        case VMX_GUEST_LDTR_BASE:
        case VMX_GUEST_TR_BASE:
        case VMX_GUEST_GDTR_BASE:
        case VMX_GUEST_IDTR_BASE:
        case VMX_GUEST_DR7:
        case VMX_GUEST_RFLAGS:
        case VMX_GUEST_PENDING_DEBUG_EXCEPTIONS:
        case VMX_GUEST_SYSENTER_ESP:
        case VMX_GUEST_SYSENTER_EIP:
        case VMX_CONTROL_CR0_MASK:
        case VMX_CONTROL_CR4_MASK:
        case VMX_CONTROL_CR0_READ_SHADOW:
        case VMX_CONTROL_CR4_READ_SHADOW:
        case VMX_GUEST_CR3:
        case VMX_CONTROL_EXCEPTION_BITMAP:
            break;
        case VMX_CONTROL_ENTRY_INTERRUPTION_INFO:
            value &= ~(MASK(31 - 12) << 12);
            break;
        case VMX_CONTROL_PIN_EXECUTION_CONTROLS:
            value = applyFixedBits(value, pin_control_high, pin_control_low);
            break;
        case VMX_CONTROL_PRIMARY_PROCESSOR_CONTROLS:
            value = applyFixedBits(value, primary_control_high, primary_control_low);
            break;
        case VMX_CONTROL_SECONDARY_PROCESSOR_CONTROLS:
            value = applyFixedBits(value, secondary_control_high, secondary_control_low);
            break;
        case VMX_CONTROL_EXIT_CONTROLS:
            value = applyFixedBits(value, exit_control_high, exit_control_low);
            break;
        case VMX_GUEST_CR0:
            value = applyFixedBits(value, cr0_high, cr0_low);
            break;
        case VMX_GUEST_CR4:
            value = applyFixedBits(value, cr4_high, cr4_low);
            break;
        default:
            userError("VCPU WriteVMCS: Invalid field %x.", field);
            current_syscall_error.type = seL4_IllegalOperation;
            return EXCEPTION_SYSCALL_ERROR;
        }
        fields[i] = field;
        values[i] = value;
    }
    setThreadState(ksCurThread, ThreadState_Restart);
    return invokeWriteVMCS(VCPU_PTR(cap_vcpu_cap_get_capVCPUPtr(cap)), num_fields, fields, values);
}
exception_t
invokeWriteVMCS(vcpu_t *vcpu, int num_fields, uint32_t *fields, uint32_t *values)
{
    tcb_t *thread;
    int i;
    thread = ksCurThread;
    if (current_vmcs != vcpu) {
        vmptrld(vcpu);
    }
    for (i = 0; i < num_fields; i++) {
        uint32_t field = fields[i];
        uint32_t value = values[i];
        switch (field) {
        case VMX_CONTROL_EXCEPTION_BITMAP:
            vcpu->exception_mask = vcpu->written_exception_mask = value;
            break;
        case VMX_GUEST_CR0:
            vcpu->cr0 = vcpu->written_cr0 = value;
            break;
        case VMX_CONTROL_CR0_MASK:
            vcpu->cr0_mask = vcpu->written_cr0_mask = value;
            break;
        case VMX_CONTROL_CR0_READ_SHADOW:
            vcpu->cr0_shadow = vcpu->written_cr0_shadow = value;
            break;
        }
        setRegister(thread, msgRegisters[0], value);
        vmwrite(field, value);
    }
    return EXCEPTION_NONE;
}

exception_t
decodeReadVMCS(cap_t cap, unsigned int length, word_t* buffer)
{
    uint32_t fields[MAX_VMCS_FIELDS];
    int num_fields;
    int i;
    if (length > MAX_VMCS_FIELDS) {
        userError("VCPU ReadVMCS: Too many arguments.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }
    num_fields = length;
    for (i = 0; i < num_fields; i++) {
        uint32_t field = getSyscallArg(i, buffer);
        switch (field) {
        case VMX_GUEST_RIP:
        case VMX_GUEST_RSP:
        case VMX_GUEST_ES_SELECTOR:
        case VMX_GUEST_CS_SELECTOR:
        case VMX_GUEST_SS_SELECTOR:
        case VMX_GUEST_DS_SELECTOR:
        case VMX_GUEST_FS_SELECTOR:
        case VMX_GUEST_GS_SELECTOR:
        case VMX_GUEST_LDTR_SELECTOR:
        case VMX_GUEST_TR_SELECTOR:
        case VMX_GUEST_DEBUGCTRL:
        case VMX_GUEST_PAT:
        case VMX_GUEST_EFER:
        case VMX_GUEST_PERF_GLOBAL_CTRL:
        case VMX_GUEST_PDPTE0:
        case VMX_GUEST_PDPTE1:
        case VMX_GUEST_PDPTE2:
        case VMX_GUEST_PDPTE3:
        case VMX_GUEST_ES_LIMIT:
        case VMX_GUEST_CS_LIMIT:
        case VMX_GUEST_SS_LIMIT:
        case VMX_GUEST_DS_LIMIT:
        case VMX_GUEST_FS_LIMIT:
        case VMX_GUEST_GS_LIMIT:
        case VMX_GUEST_LDTR_LIMIT:
        case VMX_GUEST_TR_LIMIT:
        case VMX_GUEST_GDTR_LIMIT:
        case VMX_GUEST_IDTR_LIMIT:
        case VMX_GUEST_ES_ACCESS_RIGHTS:
        case VMX_GUEST_CS_ACCESS_RIGHTS:
        case VMX_GUEST_SS_ACCESS_RIGHTS:
        case VMX_GUEST_DS_ACCESS_RIGHTS:
        case VMX_GUEST_FS_ACCESS_RIGHTS:
        case VMX_GUEST_GS_ACCESS_RIGHTS:
        case VMX_GUEST_LDTR_ACCESS_RIGHTS:
        case VMX_GUEST_TR_ACCESS_RIGHTS:
        case VMX_GUEST_INTERRUPTABILITY:
        case VMX_GUEST_ACTIVITY:
        case VMX_GUEST_SMBASE:
        case VMX_GUEST_SYSENTER_CS:
        case VMX_GUEST_PREEMPTION_TIMER_VALUE:
        case VMX_GUEST_ES_BASE:
        case VMX_GUEST_CS_BASE:
        case VMX_GUEST_SS_BASE:
        case VMX_GUEST_DS_BASE:
        case VMX_GUEST_FS_BASE:
        case VMX_GUEST_GS_BASE:
        case VMX_GUEST_LDTR_BASE:
        case VMX_GUEST_TR_BASE:
        case VMX_GUEST_GDTR_BASE:
        case VMX_GUEST_IDTR_BASE:
        case VMX_GUEST_DR7:
        case VMX_GUEST_RFLAGS:
        case VMX_GUEST_PENDING_DEBUG_EXCEPTIONS:
        case VMX_GUEST_SYSENTER_ESP:
        case VMX_GUEST_SYSENTER_EIP:
        case VMX_CONTROL_CR0_MASK:
        case VMX_CONTROL_CR4_MASK:
        case VMX_CONTROL_CR0_READ_SHADOW:
        case VMX_CONTROL_CR4_READ_SHADOW:
        case VMX_DATA_INSTRUCTION_ERROR:
        case VMX_DATA_EXIT_INTERRUPT_INFO:
        case VMX_DATA_EXIT_INTERRUPT_ERROR:
        case VMX_DATA_IDT_VECTOR_INFO:
        case VMX_DATA_IDT_VECTOR_ERROR:
        case VMX_DATA_EXIT_INSTRUCTION_LENGTH:
        case VMX_DATA_EXIT_INSTRUCTION_INFO:
        case VMX_DATA_GUEST_PHYSICAL:
        case VMX_DATA_IO_RCX:
        case VMX_DATA_IO_RSI:
        case VMX_DATA_IO_RDI:
        case VMX_DATA_IO_RIP:
        case VMX_DATA_GUEST_LINEAR_ADDRESS:
        case VMX_CONTROL_ENTRY_INTERRUPTION_INFO:
        case VMX_CONTROL_PIN_EXECUTION_CONTROLS:
        case VMX_CONTROL_PRIMARY_PROCESSOR_CONTROLS:
        case VMX_CONTROL_EXCEPTION_BITMAP:
        case VMX_CONTROL_EXIT_CONTROLS:
        case VMX_GUEST_CR0:
        case VMX_GUEST_CR3:
        case VMX_GUEST_CR4:
            break;
        default:
            userError("VCPU ReadVMCS: Invalid field %x.", field);
            current_syscall_error.type = seL4_IllegalOperation;
            return EXCEPTION_SYSCALL_ERROR;
        }
        fields[i] = field;
    }
    return invokeReadVMCS(VCPU_PTR(cap_vcpu_cap_get_capVCPUPtr(cap)), num_fields, fields);
}

static uint32_t readVMCSfield(vcpu_t *vcpu, uint32_t field)
{
    switch (field) {
    case VMX_DATA_EXIT_INTERRUPT_INFO:
        return vcpu->interrupt_info;
    case VMX_CONTROL_EXCEPTION_BITMAP:
        return vcpu->exception_mask;
    case VMX_GUEST_CR0:
        return vcpu->cr0;
    case VMX_CONTROL_CR0_MASK:
        return vcpu->cr0_mask;
    case VMX_CONTROL_CR0_READ_SHADOW:
        return vcpu->cr0_shadow;
    }
    if (current_vmcs != vcpu) {
        vmptrld(vcpu);
    }
    return vmread(field);
}

exception_t
invokeReadVMCS(vcpu_t *vcpu, int num_fields, uint32_t *fields)
{
    tcb_t *thread;
    int i;
    word_t *sendBuf;
    thread = ksCurThread;
    sendBuf = lookupIPCBuffer(true, thread);

    for (i = 0; i < n_msgRegisters && i < num_fields; i++) {
        setRegister(thread, msgRegisters[i], readVMCSfield(vcpu, fields[i]));
    }
    if (sendBuf) {
        for (; i < num_fields; i++) {
            sendBuf[i + 1] = readVMCSfield(vcpu, fields[i]);
        }
    }
    setRegister(thread, msgInfoRegister, wordFromMessageInfo(
                    message_info_new(0, 0, 0, i)));
    setThreadState(thread, ThreadState_Running);
    return EXCEPTION_NONE;
}


exception_t
decodeSetTCB(cap_t cap, unsigned int length, word_t* buffer, extra_caps_t extraCaps)
{
    cap_t tcbCap;
    cte_t *tcbSlot;
    deriveCap_ret_t dc_ret;
    if ( extraCaps.excaprefs[0] == NULL) {
        userError("VCPU SetTCB: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }
    tcbSlot = extraCaps.excaprefs[0];
    tcbCap  = extraCaps.excaprefs[0]->cap;

    dc_ret = deriveCap(tcbSlot, tcbCap);
    if (dc_ret.status != EXCEPTION_NONE) {
        return dc_ret.status;
    }
    tcbCap = dc_ret.cap;
    if (cap_get_capType(tcbCap) != cap_thread_cap) {
        userError("TCB cap is not a TCB cap.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    setThreadState(ksCurThread, ThreadState_Restart);
    return invokeSetTCB(VCPU_PTR(cap_vcpu_cap_get_capVCPUPtr(cap)), TCB_PTR(cap_thread_cap_get_capTCBPtr(tcbCap)));
}

exception_t
invokeSetTCB(vcpu_t *vcpu, tcb_t *tcb)
{
    associateVcpuTcb(tcb, vcpu);

    return EXCEPTION_NONE;
}

uint16_t vpid_for_vcpu(vcpu_t *vcpu)
{
    return (((uint32_t)vcpu) >> 13) & MASK(16);
}

void vcpu_update_vmenter_state(vcpu_t *vcpu)
{
    word_t *buffer;
    if (current_vmcs != vcpu) {
        vmptrld(vcpu);
    }
    vmwrite(VMX_GUEST_RIP, getRegister(ksCurThread, msgRegisters[0]));
    vmwrite(VMX_CONTROL_PRIMARY_PROCESSOR_CONTROLS, applyFixedBits(getRegister(ksCurThread, msgRegisters[1]), primary_control_high, primary_control_low));
    buffer = lookupIPCBuffer(false, ksCurThread);
    if (!buffer) {
        return;
    }
    vmwrite(VMX_CONTROL_ENTRY_INTERRUPTION_INFO, buffer[3] & (~(MASK(31 - 12) << 12)));
}

#endif
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/arch/x86/object/vtx.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>

#ifdef CONFIG_VTX

#include <stdint.h>
#include <machine/io.h>
#include <kernel/boot.h>
#include <kernel/faulthandler.h>
#include <kernel/thread.h>
#include <arch/machine.h>
#include <arch/machine/cpu_registers.h>
#include <api/failures.h>
#include <api/syscall.h>
#include <model/statedata.h>
#include <arch/machine/registerset.h>
#include <arch/object/vtx.h>
#include <arch/object/vcpu.h>
#include <arch/machine/fpu.h>

#define MSR_FEATURE_CONTROL 0x3A
#define MSR_VM_BASIC 0x480
#define FEATURE_CONTROL_MASK (BIT(0) | BIT(2))

uint32_t vtx_enabled = 0;
/* Address of the current VMCS: the one loaded by the CPU. */
void *current_vmcs = NULL;
uint32_t null_ept_space = 0;
uint32_t vmcs_revision = 0;
uint32_t* msr_bitmap = NULL;
uint64_t vpid_capability = 0;
uint32_t vtx_memory[3];

static int is_vtx_supported(void)
{
    uint32_t reg_ecx, reg_edx;

    asm volatile (
        "cpuid\n"
        : "=c"(reg_ecx), "=d"(reg_edx)
        : "a"(1)
        : "ebx"
    );
    return (reg_ecx & reg_edx & BIT(5));
}

BOOT_CODE bool_t vtx_allocate(void)
{
    vtx_memory[0] = alloc_region(PAGE_BITS);
    vtx_memory[1] = alloc_region(PAGE_BITS);
    vtx_memory[2] = alloc_region(PAGE_BITS);
    return vtx_memory[0] && vtx_memory[1] && vtx_memory[2];
}

BOOT_CODE void vtx_enable(void)
{
    if (is_vtx_supported()) {
        uint64_t feature_control = ((uint64_t)ia32_rdmsr_high(MSR_FEATURE_CONTROL)) << 32 | (uint64_t)ia32_rdmsr_low(MSR_FEATURE_CONTROL);
        uint64_t vm_basic = ((uint64_t)ia32_rdmsr_high(MSR_VM_BASIC)) << 32 | (uint64_t)ia32_rdmsr_low(MSR_VM_BASIC);
        vmcs_revision = vm_basic;
        if ((feature_control & FEATURE_CONTROL_MASK) == FEATURE_CONTROL_MASK) {
            uint32_t vm_basic_high = vm_basic >> 32;
            if (init_vtx_fixed_values((vm_basic_high & BIT(55 - 32)) == BIT(55 - 32))) {
                uint32_t *vmxon_region;
                uint64_t vmxon_region_arg;
                uint8_t error = 0;
                vmxon_region = (uint32_t *)vtx_memory[0];
                vmxon_region[0] = vmcs_revision;
                vmxon_region_arg = (uint64_t)(
                                       pptr_to_paddr(vmxon_region) & 0xFFFFF000L);
                write_cr4(read_cr4() | CR4_VMXE);
                asm volatile(
                    "vmxon %1; setnae %0"
                    : "=g"(error)
                    : "m"(vmxon_region_arg)
                    : "memory", "cc");
                if (error) {
                    printf("vt-x: vmxon failure\n");
                } else {
                    void *null_ept_space_ptr;
                    printf("vt-x: on!\n");
                    msr_bitmap = (uint32_t*)vtx_memory[1];
                    memset(msr_bitmap, ~0, BIT(PAGE_BITS));
                    /* Set sysenter MSRs to writeable and readable. */
                    msr_bitmap[11] = 0xff8fffff;
                    msr_bitmap[512 + 11] = 0xff8fffff;
                    null_ept_space_ptr = (void*)vtx_memory[2];
                    memset(null_ept_space_ptr, 0, BIT(PAGE_BITS));
                    null_ept_space = pptr_to_paddr(null_ept_space_ptr);
                    null_ept_space |= (3 << 3) | 6;
                    vtx_enabled = 1;
                }
                vpid_capability = ((uint64_t)ia32_rdmsr_high(MSR_VMX_EPT_VPID_CAP)) << 32 | (uint64_t)ia32_rdmsr_low(MSR_VMX_EPT_VPID_CAP);
            } else {
                printf("vt-x: disabled due to lack of required features\n");
            }
        } else if (!(feature_control & 1)) {
            printf("vt-x: feature control not locked\n");
        } else if (!(feature_control & (1 << 2))) {
            printf("vt-x: disabled by feature control\n");
        }
    } else {
        printf("vt-x: not supported\n");
    }
}

static void setMRs_vmexit(uint32_t reason, uint32_t qualification)
{
    word_t *buffer;
    int i;

    setRegister(ksCurThread, msgRegisters[0], vmread(VMX_GUEST_RIP));
    setRegister(ksCurThread, msgRegisters[1], vmread(VMX_CONTROL_PRIMARY_PROCESSOR_CONTROLS));

    buffer = lookupIPCBuffer(true, ksCurThread);
    if (!buffer) {
        return;
    }

    buffer[3] = vmread(VMX_CONTROL_ENTRY_INTERRUPTION_INFO);
    buffer[4] = reason;
    buffer[5] = qualification;

    buffer[6] = vmread(VMX_DATA_EXIT_INSTRUCTION_LENGTH);
    buffer[7] = vmread(VMX_DATA_GUEST_PHYSICAL);
    buffer[8] = vmread(VMX_GUEST_RFLAGS);
    buffer[9] = vmread(VMX_GUEST_INTERRUPTABILITY);
    buffer[10] = vmread(VMX_GUEST_CR3);

    for (i = 0; i <= EBP; i++) {
        buffer[11 + i] = ksCurThread->tcbArch.vcpu->gp_registers[i];
    }
}

static void handleVmxFault(uint32_t reason, uint32_t qualification)
{
    /* Indicate that we are returning the from VMEnter with a fault */
    setRegister(ksCurThread, msgInfoRegister, 1);

    setMRs_vmexit(reason, qualification);

    /* Set the thread back to running */
    setThreadState(ksCurThread, ThreadState_Running);

    /* No need to schedule because this wasn't an interrupt and
     * we run at the same priority */
    activateThread();
}

exception_t
handleVmexit(void)
{
    enum exit_reasons reason;
    uint32_t qualification, interrupt;
    finishVmexitSaving();
    reason = vmread(VMX_DATA_EXIT_REASON) & MASK(16);
    if (reason == EXTERNAL_INTERRUPT) {
        interrupt = vmread(VMX_DATA_EXIT_INTERRUPT_INFO);
        ia32KScurInterrupt = interrupt & 0xff;
        return handleInterruptEntry();
    } else if (ksCurThread != ia32KSfpuOwner) {
        if (reason == EXCEPTION_OR_NMI && !(ksCurThread->tcbArch.vcpu->exception_mask & BIT(7))) {
            interrupt = vmread(VMX_DATA_EXIT_INTERRUPT_INFO);
            if ((interrupt & 0xff) == 0x7) {
                return handleUnimplementedDevice();
            }
        } else if (reason == CONTROL_REGISTER && !(ksCurThread->tcbArch.vcpu->cr0_mask & BIT(3))) {
            qualification = vmread(VMX_DATA_EXIT_QUALIFICATION);
            if ((qualification & 0xF) == 0) {
                switch ((qualification >> 4) & 0x3) {
                case 0: { /* mov to CR0 */
                    register_t source = crExitRegs[(qualification >> 8) & 0x7];
                    uint32_t value;
                    if (source != ESP) {
                        value = ksCurThread->tcbArch.vcpu->gp_registers[source];
                        ksCurThread->tcbArch.vcpu->cr0 = (ksCurThread->tcbArch.vcpu->cr0 & ~BIT(0x3)) |
                                                         (value & BIT(0x3));
                        if (!((value ^ ksCurThread->tcbArch.vcpu->cr0_shadow) &
                                ksCurThread->tcbArch.vcpu->cr0_mask)) {
                            return EXCEPTION_NONE;
                        }
                    }
                    break;
                }
                case 2: { /* CLTS */
                    ksCurThread->tcbArch.vcpu->cr0 = (ksCurThread->tcbArch.vcpu->cr0 & ~BIT(0x3));
                    return EXCEPTION_NONE;
                }
                case 3: { /* LMSW */
                    uint32_t value = (qualification >> 16) & MASK(16);
                    ksCurThread->tcbArch.vcpu->cr0 = (ksCurThread->tcbArch.vcpu->cr0 & ~BIT(0x3)) | value;
                    if (!((value ^ ksCurThread->tcbArch.vcpu->cr0_shadow) &
                            ksCurThread->tcbArch.vcpu->cr0_mask & MASK(16))) {
                        return EXCEPTION_NONE;
                    }
                    break;
                }
                }
            }
        }
    }
    switch (reason) {
    case EXCEPTION_OR_NMI:
        ksCurThread->tcbArch.vcpu->interrupt_info = vmread(VMX_DATA_EXIT_INTERRUPT_INFO);
    case MOV_DR:
    case TASK_SWITCH:
    case CONTROL_REGISTER:
    case IO:
    case MWAIT:
    case SIPI:
    case INVLPG:
    case INVEPT:
    case INVVPID:
    case VMCLEAR:
    case VMPTRLD:
    case VMPTRST:
    case VMREAD:
    case VMWRITE:
    case VMXON:
    case EPT_VIOLATION:
    case GDTR_OR_IDTR:
    case LDTR_OR_TR:
    case TPR_BELOW_THRESHOLD:
    case APIC_ACCESS:
        qualification = vmread(VMX_DATA_EXIT_QUALIFICATION);
        break;
    default:
        qualification = 0;
    }

    handleVmxFault(reason, qualification);

    return EXCEPTION_NONE;
}

exception_t
handleVmEntryFail(void)
{
    handleVmxFault(-1, -1);

    return EXCEPTION_NONE;
}

void
finishVmexitSaving(void)
{
    vcpu_t *vcpu = ksCurThread->tcbArch.vcpu;
    vcpu->launched = true;
    vcpu->written_cr0 = vmread(VMX_GUEST_CR0);
    if (ksCurThread != ia32KSfpuOwner) {
        vcpu->cr0 = (vcpu->written_cr0 & ~BIT(3)) | (ksCurThread->tcbArch.vcpu->cr0 & BIT(3));
    } else {
        vcpu->cr0 = vcpu->written_cr0;
    }
}

static void
setIOPort(vcpu_t* vcpu)
{
    uint32_t high, low;
    if (cap_get_capType(vcpu->io_port) == cap_io_port_cap) {
        low = cap_io_port_cap_get_capIOPortFirstPort(vcpu->io_port);
        high = cap_io_port_cap_get_capIOPortLastPort(vcpu->io_port) + 1;
    } else {
        low = -1;
        high = -1;
    }
    /* Has the range changed at all */
    if (low != vcpu->io_min || high != vcpu->io_max) {
        /* We previously had some mappings, and now our range has changed
           Just knock all the ports out */
        if (vcpu->io_min != -1) {
            memset(&vcpu->io[vcpu->io_min / 32], ~0,
                   (1 + ((vcpu->io_max - 1) / 32) - (vcpu->io_min / 32)) * 4);
        }
        vcpu->io_min = low;
        vcpu->io_max = high;
    }
}

static void invvpid_context(uint16_t vpid)
{
    struct {
        uint64_t vpid : 16;
        uint64_t rsvd : 48;
        uint64_t address;
    } __attribute__((packed)) operand = {vpid, 0, 0};
    asm volatile(INVVPID_OPCODE :: "a"(&operand), "c"(1) : "cc");
}

static void
setEPTRoot(cap_t vmxSpace, vcpu_t* vcpu)
{
    uint32_t ept_root;
    if (cap_get_capType(vmxSpace) != cap_ept_page_directory_pointer_table_cap) {
        ept_root = null_ept_space;
    } else {
        ept_root = pptr_to_paddr((void*)cap_ept_page_directory_pointer_table_cap_get_capPDPTBasePtr(vmxSpace)) - EPT_PDPT_OFFSET;
    }
    if (ept_root != vcpu->last_ept_root) {
        vcpu->last_ept_root = ept_root;
        vmwrite(VMX_CONTROL_EPT_POINTER, ept_root | (3 << 3) | 6);
        invvpid_context(vpid_for_vcpu(vcpu));
    }
}

static void
handleLazyFpu(void)
{
    uint32_t cr0;
    uint32_t exception_bitmap;
    uint32_t cr0_mask;
    uint32_t cr0_shadow;
    vcpu_t *vcpu = ksCurThread->tcbArch.vcpu;
    if (ksCurThread != ia32KSfpuOwner) {
        cr0 = vcpu->cr0 | BIT(3);
        exception_bitmap = vcpu->exception_mask | BIT(0x7);
        if (ksCurThread->tcbArch.vcpu->cr0_mask & BIT(3)) {
            /* Don't replace the userland read shadow value if userland is
             * masking CR0.TS. The important thing is that it is masked so the
             * guest can't modify it. We don't care about the read shadow
             * value. */
            cr0_mask = vcpu->cr0_mask;
            cr0_shadow = vcpu->cr0_shadow;
        } else {
            cr0_mask = vcpu->cr0_mask | BIT(3);
            cr0_shadow = (vcpu->cr0 & BIT(3)) | (vcpu->cr0_shadow & ~BIT(3));
        }
    } else {
        cr0 = vcpu->cr0;
        exception_bitmap = vcpu->exception_mask;
        cr0_mask = vcpu->cr0_mask;
        cr0_shadow = vcpu->cr0_shadow;
    }
    if (cr0 != vcpu->written_cr0) {
        vmwrite(VMX_GUEST_CR0, cr0);
        vcpu->written_cr0 = cr0;
    }
    if (exception_bitmap != vcpu->written_exception_mask) {
        vmwrite(VMX_CONTROL_EXCEPTION_BITMAP, exception_bitmap);
        vcpu->written_exception_mask = exception_bitmap;
    }
    if (cr0_mask != vcpu->written_cr0_mask) {
        vmwrite(VMX_CONTROL_CR0_MASK, cr0_mask);
        vcpu->written_cr0_mask = cr0_mask;
    }
    if (cr0_shadow != vcpu->written_cr0_shadow) {
        vmwrite(VMX_CONTROL_CR0_READ_SHADOW, cr0_shadow);
        vcpu->written_cr0_shadow = cr0_shadow;
    }
}

void
restoreVMCS(void)
{
    vcpu_t *expected_vmcs = ksCurThread->tcbArch.vcpu;

    /* Check that the right VMCS is active and current. */
    if (current_vmcs != expected_vmcs) {
        vmptrld(expected_vmcs);
    }

    if (getCurrentPD() != expected_vmcs->last_host_cr3) {
        expected_vmcs->last_host_cr3 = getCurrentPD();
        vmwrite(VMX_HOST_CR3, getCurrentPD());
    }
    setEPTRoot(TCB_PTR_CTE_PTR(ksCurThread, tcbArchEPTRoot)->cap, expected_vmcs);
    setIOPort(ksCurThread->tcbArch.vcpu);
    handleLazyFpu();
}

uint32_t
vmread(uint32_t field)
{
    uint32_t value;
    asm volatile (
        "vmread %%eax, %%ecx"
        : "=c"(value)
        : "a"(field)
        : "cc"
    );
    return value;
}

void
vmwrite(uint32_t field, uint32_t value)
{
    uint8_t error = 0;
    asm volatile (
        "vmwrite %%eax, %%edx; setna %0"
        : "=q"(error)
        : "a"(value), "d"(field)
        : "cc"
    );
    if (error) {
        printf("error setting field %x\n", field);
    }
}

int
vmptrld(void *vmcs_ptr)
{
    uint64_t physical_address;
    uint8_t error;
    current_vmcs = vmcs_ptr;
    physical_address = pptr_to_paddr(vmcs_ptr);
    asm volatile (
        "vmptrld (%%eax); setna %0"
        : "=g"(error)
        : "a"(&physical_address), "m"(physical_address)
        : "cc"
    );
    return error;
}

void
vmclear(void *vmcs_ptr)
{
    uint64_t physical_address;
    physical_address = pptr_to_paddr((void*)vmcs_ptr);
    asm volatile (
        "vmclear (%%eax)"
        :
        : "a"(&physical_address), "m"(physical_address)
        : "cc"
    );
}

void
invept(void* ept_pml4)
{
    if (vpid_capability & BIT(20) && vpid_capability & (BIT(25) | BIT(26))) {
        uint64_t physical_address[2];
        uint32_t type = 2;
        if (vpid_capability & BIT(25)) {
            type = 1;
        }

        physical_address[0] = pptr_to_paddr((void*)ept_pml4);
        physical_address[1] = 0;
        asm volatile (
            INVEPT_OPCODE
            :
            : "a"(&physical_address),  "c"(type)
            : "memory"
        );
    }
}

#endif
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/assert.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <assert.h>
#include <machine/io.h>

#ifdef DEBUG

void _fail(
    const char*  s,
    const char*  file,
    unsigned int line,
    const char*  function)
{
    printf(
        "seL4 called fail at %s:%u in function %s, saying \"%s\"\n",
        file,
        line,
        function,
        s
    );
    halt();
}

void _assert_fail(
    const char*  assertion,
    const char*  file,
    unsigned int line,
    const char*  function)
{
    printf("seL4 failed assertion '%s' at %s:%u in function %s\n",
           assertion,
           file,
           line,
           function
          );
    halt();
}

#endif
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/inlines.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <types.h>
#include <api/failures.h>

lookup_fault_t current_lookup_fault;
fault_t current_fault;
syscall_error_t current_syscall_error;
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/kernel/boot.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <assert.h>
#include <kernel/boot.h>
#include <kernel/thread.h>
#include <kernel/cdt.h>
#include <machine/io.h>
#include <machine/registerset.h>
#include <model/statedata.h>
#include <arch/machine.h>
#include <arch/kernel/boot.h>
#include <arch/kernel/vspace.h>
#include <arch/linker.h>
#include <plat/machine/hardware.h>

/* (node-local) state accessed only during bootstrapping */

ndks_boot_t ndks_boot BOOT_DATA;

BOOT_CODE bool_t
insert_region(region_t reg)
{
    unsigned int i;

    assert(reg.start <= reg.end);
    if (is_reg_empty(reg)) {
        return true;
    }
    for (i = 0; i < MAX_NUM_FREEMEM_REG; i++) {
        if (is_reg_empty(ndks_boot.freemem[i])) {
            ndks_boot.freemem[i] = reg;
            return true;
        }
    }
    return false;
}

BOOT_CODE static inline uint32_t
reg_size(region_t reg)
{
    return reg.end - reg.start;
}

BOOT_CODE pptr_t
alloc_region(uint32_t size_bits)
{
    unsigned int i;
    unsigned int reg_index = 0; /* gcc cannot work out that this will not be used uninitialized */
    region_t reg = REG_EMPTY;
    region_t rem_small = REG_EMPTY;
    region_t rem_large = REG_EMPTY;
    region_t new_reg;
    region_t new_rem_small;
    region_t new_rem_large;

    /* Search for a freemem region that will be the best fit for an allocation. We favour allocations
     * that are aligned to either end of the region. If an allocation must split a region we favour
     * an unbalanced split. In both cases we attempt to use the smallest region possible. In general
     * this means we aim to make the size of the smallest remaining region smaller (ideally zero)
     * followed by making the size of the largest remaining region smaller */

    for (i = 0; i < MAX_NUM_FREEMEM_REG; i++) {
        /* Determine whether placing the region at the start or the end will create a bigger left over region */
        if (ROUND_UP(ndks_boot.freemem[i].start, size_bits) - ndks_boot.freemem[i].start <
                ndks_boot.freemem[i].end - ROUND_DOWN(ndks_boot.freemem[i].end, size_bits)) {
            new_reg.start = ROUND_UP(ndks_boot.freemem[i].start, size_bits);
            new_reg.end = new_reg.start + BIT(size_bits);
        } else {
            new_reg.end = ROUND_DOWN(ndks_boot.freemem[i].end, size_bits);
            new_reg.start = new_reg.end - BIT(size_bits);
        }
        if (new_reg.end > new_reg.start &&
                new_reg.start >= ndks_boot.freemem[i].start &&
                new_reg.end <= ndks_boot.freemem[i].end) {
            if (new_reg.start - ndks_boot.freemem[i].start < ndks_boot.freemem[i].end - new_reg.end) {
                new_rem_small.start = ndks_boot.freemem[i].start;
                new_rem_small.end = new_reg.start;
                new_rem_large.start = new_reg.end;
                new_rem_large.end = ndks_boot.freemem[i].end;
            } else {
                new_rem_large.start = ndks_boot.freemem[i].start;
                new_rem_large.end = new_reg.start;
                new_rem_small.start = new_reg.end;
                new_rem_small.end = ndks_boot.freemem[i].end;
            }
            if ( is_reg_empty(reg) ||
                    (reg_size(new_rem_small) < reg_size(rem_small)) ||
                    (reg_size(new_rem_small) == reg_size(rem_small) && reg_size(new_rem_large) < reg_size(rem_large)) ) {
                reg = new_reg;
                rem_small = new_rem_small;
                rem_large = new_rem_large;
                reg_index = i;
            }
        }
    }
    if (is_reg_empty(reg)) {
        printf("Kernel init failing: not enough memory\n");
        return 0;
    }
    /* Remove the region in question */
    ndks_boot.freemem[reg_index] = REG_EMPTY;
    /* Add the remaining regions in largest to smallest order */
    insert_region(rem_large);
    if (!insert_region(rem_small)) {
        printf("alloc_region(): wasted 0x%x bytes due to alignment, try to increase MAX_NUM_FREEMEM_REG\n",
               (unsigned int)(rem_small.end - rem_small.start));
    }
    return reg.start;
}

BOOT_CODE void
write_slot(slot_ptr_t slot_ptr, cap_t cap)
{
    slot_ptr->cap = cap;
    cdtInsert(NULL, slot_ptr);

    //slot_ptr->cteMDBNode = nullMDBNode;
    //mdb_node_ptr_set_mdbRevocable  (&slot_ptr->cteMDBNode, true);
    //mdb_node_ptr_set_mdbFirstBadged(&slot_ptr->cteMDBNode, true);
}

/* Our root CNode needs to be able to fit all the initial caps and not
 * cover all of memory.
 */
compile_assert(root_cnode_size_valid,
               CONFIG_ROOT_CNODE_SIZE_BITS < 32 - CTE_SIZE_BITS &&
               (1U << CONFIG_ROOT_CNODE_SIZE_BITS) >= BI_CAP_DYN_START)

BOOT_CODE cap_t
create_root_cnode(void)
{
    pptr_t  pptr;
    cap_t   cap;

    /* write the number of root CNode slots to global state */
    ndks_boot.slot_pos_max = BIT(CONFIG_ROOT_CNODE_SIZE_BITS);

    /* create an empty root CNode */
    pptr = alloc_region(CONFIG_ROOT_CNODE_SIZE_BITS + CTE_SIZE_BITS);
    if (!pptr) {
        printf("Kernel init failing: could not create root cnode\n");
        return cap_null_cap_new();
    }
    memzero(CTE_PTR(pptr), 1U << (CONFIG_ROOT_CNODE_SIZE_BITS + CTE_SIZE_BITS));
    cap =
        cap_cnode_cap_new(
            CONFIG_ROOT_CNODE_SIZE_BITS,      /* radix      */
            32 - CONFIG_ROOT_CNODE_SIZE_BITS, /* guard size */
            0,                                /* guard      */
            pptr                              /* pptr       */
        );

    /* write the root CNode cap into the root CNode */
    write_slot(SLOT_PTR(pptr, BI_CAP_IT_CNODE), cap);

    return cap;
}

compile_assert(irq_cnode_size, BIT(PAGE_BITS - CTE_SIZE_BITS) > maxIRQ)

BOOT_CODE bool_t
create_irq_cnode(void)
{
    pptr_t pptr;
    /* create an empty IRQ CNode */
    pptr = alloc_region(PAGE_BITS);
    if (!pptr) {
        printf("Kernel init failing: could not create irq cnode\n");
        return false;
    }
    memzero((void*)pptr, 1 << PAGE_BITS);
    intStateIRQNode = (cte_t*)pptr;
    return true;
}

/* Check domain scheduler assumptions. */
compile_assert(num_domains_valid,
               CONFIG_NUM_DOMAINS >= 1 && CONFIG_NUM_DOMAINS <= 256)
compile_assert(num_priorities_valid,
               CONFIG_NUM_PRIORITIES >= 1 && CONFIG_NUM_PRIORITIES <= 256)

BOOT_CODE void
create_domain_cap(cap_t root_cnode_cap)
{
    cap_t cap;
    unsigned int i;

    /* Check domain scheduler assumptions. */
    assert(ksDomScheduleLength > 0);
    for (i = 0; i < ksDomScheduleLength; i++) {
        assert(ksDomSchedule[i].domain < CONFIG_NUM_DOMAINS);
        assert(ksDomSchedule[i].length > 0);
    }

    cap = cap_domain_cap_new();
    write_slot(SLOT_PTR(pptr_of_cap(root_cnode_cap), BI_CAP_DOM), cap);
}


BOOT_CODE cap_t
create_ipcbuf_frame(cap_t root_cnode_cap, cap_t pd_cap, vptr_t vptr)
{
    cap_t cap;
    pptr_t pptr;

    /* allocate the IPC buffer frame */
    pptr = alloc_region(PAGE_BITS);
    if (!pptr) {
        printf("Kernel init failing: could not create ipc buffer frame\n");
        return cap_null_cap_new();
    }
    clearMemory((void*)pptr, PAGE_BITS);

    /* create a cap of it and write it into the root CNode */
    cap = create_mapped_it_frame_cap(pd_cap, pptr, vptr, false, false);
    write_slot(SLOT_PTR(pptr_of_cap(root_cnode_cap), BI_CAP_IT_IPCBUF), cap);

    return cap;
}

BOOT_CODE void
create_bi_frame_cap(
    cap_t      root_cnode_cap,
    cap_t      pd_cap,
    pptr_t     pptr,
    vptr_t     vptr
)
{
    cap_t cap;

    /* create a cap of it and write it into the root CNode */
    cap = create_mapped_it_frame_cap(pd_cap, pptr, vptr, false, false);
    write_slot(SLOT_PTR(pptr_of_cap(root_cnode_cap), BI_CAP_BI_FRAME), cap);
}

BOOT_CODE pptr_t
allocate_bi_frame(
    node_id_t  node_id,
    uint32_t   num_nodes,
    vptr_t ipcbuf_vptr
)
{
    pptr_t pptr;

    /* create the bootinfo frame object */
    pptr = alloc_region(BI_FRAME_SIZE_BITS);
    if (!pptr) {
        printf("Kernel init failed: could not allocate bootinfo frame\n");
        return 0;
    }
    clearMemory((void*)pptr, PAGE_BITS);

    /* initialise bootinfo-related global state */
    ndks_boot.bi_frame = BI_PTR(pptr);
    ndks_boot.slot_pos_cur = BI_CAP_DYN_START;

    BI_PTR(pptr)->node_id = node_id;
    BI_PTR(pptr)->num_nodes = num_nodes;
    BI_PTR(pptr)->num_iopt_levels = 0;
    BI_PTR(pptr)->ipcbuf_vptr = ipcbuf_vptr;
    BI_PTR(pptr)->it_cnode_size_bits = CONFIG_ROOT_CNODE_SIZE_BITS;
    BI_PTR(pptr)->it_domain = ksDomSchedule[ksDomScheduleIdx].domain;

    return pptr;
}

BOOT_CODE bool_t
provide_cap(cap_t root_cnode_cap, cap_t cap)
{
    if (ndks_boot.slot_pos_cur >= ndks_boot.slot_pos_max) {
        printf("Kernel init failed: ran out of cap slots\n");
        return false;
    }
    write_slot(SLOT_PTR(pptr_of_cap(root_cnode_cap), ndks_boot.slot_pos_cur), cap);
    ndks_boot.slot_pos_cur++;
    return true;
}

BOOT_CODE create_frames_of_region_ret_t
create_frames_of_region(
    cap_t    root_cnode_cap,
    cap_t    pd_cap,
    region_t reg,
    bool_t   do_map,
    int32_t  pv_offset
)
{
    pptr_t     f;
    cap_t      frame_cap;
    slot_pos_t slot_pos_before;
    slot_pos_t slot_pos_after;

    slot_pos_before = ndks_boot.slot_pos_cur;

    for (f = reg.start; f < reg.end; f += BIT(PAGE_BITS)) {
        if (do_map) {
            frame_cap = create_mapped_it_frame_cap(pd_cap, f, f - BASE_OFFSET - pv_offset, false, false);
        } else {
            frame_cap = create_unmapped_it_frame_cap(f, false);
        }
        if (!provide_cap(root_cnode_cap, frame_cap))
            return (create_frames_of_region_ret_t) {
            S_REG_EMPTY, false
        };
    }

    slot_pos_after = ndks_boot.slot_pos_cur;

    return (create_frames_of_region_ret_t) {
        (slot_region_t) { slot_pos_before, slot_pos_after }, true
    };
}

BOOT_CODE bool_t
create_idle_thread(void)
{
    pptr_t pptr;
    pptr = alloc_region(TCB_BLOCK_SIZE_BITS);
    if (!pptr) {
        printf("Kernel init failed: Unable to allocate tcb for idle thread\n");
        return false;
    }
    memzero((void *)pptr, 1 << TCB_BLOCK_SIZE_BITS);
    ksIdleThread = TCB_PTR(pptr + TCB_OFFSET);
    configureIdleThread(ksIdleThread);
    return true;
}

BOOT_CODE bool_t
create_initial_thread(
    cap_t  root_cnode_cap,
    cap_t  it_pd_cap,
    vptr_t ui_v_entry,
    vptr_t bi_frame_vptr,
    vptr_t ipcbuf_vptr,
    cap_t  ipcbuf_cap
)
{
    pptr_t pptr;
    cap_t  cap;
    tcb_t* tcb;
    deriveCap_ret_t dc_ret;

    /* allocate TCB */
    pptr = alloc_region(TCB_BLOCK_SIZE_BITS);
    if (!pptr) {
        printf("Kernel init failed: Unable to allocate tcb for initial thread\n");
        return false;
    }
    memzero((void*)pptr, 1 << TCB_BLOCK_SIZE_BITS);
    tcb = TCB_PTR(pptr + TCB_OFFSET);
    tcb->tcbTimeSlice = CONFIG_TIME_SLICE;
    Arch_initContext(&tcb->tcbArch.tcbContext);

    /* derive a copy of the IPC buffer cap for inserting */
    dc_ret = deriveCap(SLOT_PTR(pptr_of_cap(root_cnode_cap), BI_CAP_IT_IPCBUF), ipcbuf_cap);
    if (dc_ret.status != EXCEPTION_NONE) {
        printf("Failed to derive copy of IPC Buffer\n");
        return false;
    }

    /* initialise TCB (corresponds directly to abstract specification) */
    cteInsert(
        root_cnode_cap,
        SLOT_PTR(pptr_of_cap(root_cnode_cap), BI_CAP_IT_CNODE),
        SLOT_PTR(pptr, tcbCTable)
    );
    cteInsert(
        it_pd_cap,
        SLOT_PTR(pptr_of_cap(root_cnode_cap), BI_CAP_IT_VSPACE),
        SLOT_PTR(pptr, tcbVTable)
    );
    cteInsert(
        dc_ret.cap,
        SLOT_PTR(pptr_of_cap(root_cnode_cap), BI_CAP_IT_IPCBUF),
        SLOT_PTR(pptr, tcbBuffer)
    );
    tcb->tcbIPCBuffer = ipcbuf_vptr;
    setRegister(tcb, capRegister, bi_frame_vptr);
    setNextPC(tcb, ui_v_entry);

    /* initialise TCB */
    tcb->tcbPriority = seL4_MaxPrio;
    setupReplyMaster(tcb);
    setThreadState(tcb, ThreadState_Running);
    ksSchedulerAction = SchedulerAction_ResumeCurrentThread;
    ksCurThread = ksIdleThread;
    ksCurDomain = ksDomSchedule[ksDomScheduleIdx].domain;
    ksDomainTime = ksDomSchedule[ksDomScheduleIdx].length;
    assert(ksCurDomain < CONFIG_NUM_DOMAINS && ksDomainTime > 0);

    /* initialise current thread pointer */
    switchToThread(tcb); /* initialises ksCurThread */

    /* create initial thread's TCB cap */
    cap = cap_thread_cap_new(TCB_REF(tcb));
    write_slot(SLOT_PTR(pptr_of_cap(root_cnode_cap), BI_CAP_IT_TCB), cap);

#ifdef DEBUG
    setThreadName(tcb, "rootserver");
#endif

    return true;
}

BOOT_CODE static bool_t
provide_untyped_cap(
    cap_t      root_cnode_cap,
    bool_t     deviceMemory,
    pptr_t     pptr,
    uint32_t   size_bits,
    slot_pos_t first_untyped_slot
)
{
    bool_t ret;
    unsigned int i = ndks_boot.slot_pos_cur - first_untyped_slot;
    if (i < CONFIG_MAX_NUM_BOOTINFO_UNTYPED_CAPS) {
        ndks_boot.bi_frame->ut_obj_paddr_list[i] = pptr_to_paddr((void*)pptr);
        ndks_boot.bi_frame->ut_obj_size_bits_list[i] = size_bits;
        ret = provide_cap(root_cnode_cap, cap_untyped_cap_new(deviceMemory, size_bits, pptr));
    } else {
        printf("Kernel init: Too many untyped regions for boot info\n");
        ret = true;
    }
    return ret;
}

/**
  DONT_TRANSLATE
*/
BOOT_CODE static uint32_t boot_clz (uint32_t x)
{
    return CLZ (x);
}

/**
  DONT_TRANSLATE
*/
BOOT_CODE static uint32_t boot_ctz (uint32_t x)
{
    return CTZ (x);
}

BOOT_CODE bool_t
create_untypeds_for_region(
    cap_t      root_cnode_cap,
    bool_t     deviceMemory,
    region_t   reg,
    slot_pos_t first_untyped_slot
)
{
    uint32_t align_bits;
    uint32_t size_bits;

    while (!is_reg_empty(reg)) {
        /* Due to a limitation on the mdb we cannot give out an untyped to the
         * the start of the kernel region. The reason for this is that cte pointers
         * in mdb nodes are stored with the high bits masked out. To recreate a cte pointer
         * we then need to put the high bits back in after reading it out. HOWEVER, we
         * still need a way to store and identify a NULL pointer. This means reserving
         * the bottom address as the 'null' address so that no one creates an cnode
         * there resulting in a 'null' (yet valid) cte
         */
        if (!deviceMemory && reg.start == kernelBase) {
            reg.start += BIT(PAGE_BITS);
        }
        /* Determine the maximum size of the region */
        size_bits = WORD_BITS - 1 - boot_clz(reg.end - reg.start);

        /* Determine the alignment of the region */
        align_bits = boot_ctz(reg.start);

        /* Reduce size bits to align if needed */
        if (align_bits < size_bits) {
            size_bits = align_bits;
        }

        assert(size_bits >= WORD_BITS / 8);
        if (!provide_untyped_cap(root_cnode_cap, deviceMemory, reg.start, size_bits, first_untyped_slot)) {
            return false;
        }
        reg.start += BIT(size_bits);
    }
    return true;
}

BOOT_CODE bool_t
create_untypeds(cap_t root_cnode_cap, region_t boot_mem_reuse_reg)
{
    slot_pos_t slot_pos_before;
    slot_pos_t slot_pos_after;
    uint32_t   i;
    region_t   reg;

    slot_pos_before = ndks_boot.slot_pos_cur;

    /* if boot_mem_reuse_reg is not empty, we can create UT objs from boot code/data frames */
    if (!create_untypeds_for_region(root_cnode_cap, false, boot_mem_reuse_reg, slot_pos_before)) {
        return false;
    }

    /* convert remaining freemem into UT objects and provide the caps */
    for (i = 0; i < MAX_NUM_FREEMEM_REG; i++) {
        reg = ndks_boot.freemem[i];
        ndks_boot.freemem[i] = REG_EMPTY;
        if (!create_untypeds_for_region(root_cnode_cap, false, reg, slot_pos_before)) {
            return false;
        }
    }

    slot_pos_after = ndks_boot.slot_pos_cur;
    ndks_boot.bi_frame->ut_obj_caps = (slot_region_t) {
        slot_pos_before, slot_pos_after
    };
    return true;
}

BOOT_CODE void
bi_finalise(void)
{
    slot_pos_t slot_pos_start = ndks_boot.slot_pos_cur;
    slot_pos_t slot_pos_end = ndks_boot.slot_pos_max;
    ndks_boot.bi_frame->null_caps = (slot_region_t) {
        slot_pos_start, slot_pos_end
    };
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/kernel/cdt.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <machine.h>
#include <model/statedata.h>
#include <object/structures.h>
#include <object/objecttype.h>

#define GT  ( 1)
#define EQ  ( 0)
#define LT  (-1)

void printCTE(char *msg, cte_t *cte);

static cte_t *aaInsert(cte_t *rootSlot, cte_t *newSlot);
static cte_t *aaRemove(bool_t isSwapped, cte_t *rootSlot, cte_t *targetSlot);
static cte_t *aaTraverseBackward(cte_t *slot);
static cte_t *aaTraverseForward(cte_t *slot);

typedef int (*comp_t)(cte_t *, cte_t *);
typedef int (*tie_comp_t)(cte_t *, cte_t *, comp_t);
typedef int (*type_comp_t)(cte_t *, cte_t *, tie_comp_t);

#define compare(a, b) \
    ({ typeof(a) _a = (a); \
       typeof(b) _b = (b); \
       _a == _b ? EQ : (_a > _b ? GT : LT); \
    })

static inline bool_t
capsEqual(cap_t a, cap_t b)
{
    return (cap_get_capSpaceType(a) == cap_get_capSpaceType(b)) &&
           ((word_t)cap_get_capSpacePtr(a) == (word_t)cap_get_capSpacePtr(b)) &&
           (cap_get_capSpaceSize(a) == cap_get_capSpaceSize(b)) &&
           (cap_get_capBadge(a)   == cap_get_capBadge(b)) &&
           (cap_get_capExtraComp(a) == cap_get_capExtraComp(b));
}

static inline int
tie_break_comparator(cte_t *a, cte_t *b, comp_t pre_slot)
{
    int cmp;
    /* Check the depth */
    cmp = compare(mdb_node_get_cdtDepth(a->cteMDBNode), mdb_node_get_cdtDepth(b->cteMDBNode));
    if (cmp != EQ) {
        return cmp;
    }
    if (pre_slot) {
        cmp = pre_slot(a, b);
        if (cmp != EQ) {
            return cmp;
        }
    }
    /* compare on the slot as a last resort */
    return compare(a, b);
}

static inline int
untyped_comparator(cte_t *a, cte_t *b, tie_comp_t tie_break)
{
    int cmp;
    /* Compare base address and size of the untyped object */
    cmp = compare(cap_untyped_cap_get_capPtr(a->cap), cap_untyped_cap_get_capPtr(b->cap));
    if (cmp != EQ) {
        return cmp;
    }
    cmp = - compare(cap_untyped_cap_get_capBlockSize(a->cap), cap_untyped_cap_get_capBlockSize(b->cap));
    if (cmp != EQ) {
        return cmp;
    }
    /* Do common late comparisons */
    return tie_break(a, b, NULL);
}

static inline int
endpoint_cap_comparator(cte_t *a, cte_t *b, tie_comp_t tie_break)
{
    int cmp;
    /* compare on the badge */
    cmp = compare(cap_endpoint_cap_get_capEPBadge(a->cap), cap_endpoint_cap_get_capEPBadge(b->cap));
    if (cmp != EQ) {
        return cmp;
    }
    /* tiebreak as normal */
    return tie_break(a, b, NULL);
}

static inline int
async_endpoint_cap_comparator(cte_t *a, cte_t *b, tie_comp_t tie_break)
{
    int cmp;
    /* compare on the badge */
    cmp = compare(cap_async_endpoint_cap_get_capAEPBadge(a->cap), cap_async_endpoint_cap_get_capAEPBadge(b->cap));
    if (cmp != EQ) {
        return cmp;
    }
    /* tiebreak as normal */
    return tie_break(a, b, NULL);
}

static inline int cap_extra_comp(cte_t *a, cte_t *b)
{
    return compare(cap_get_capExtraComp(a->cap), cap_get_capExtraComp(b->cap));
}

static inline int
frame_cap_comparator(cte_t *a, cte_t *b, tie_comp_t tie_break)
{
    return tie_break(a, b, cap_extra_comp);
}

static inline int
page_table_comparator(cte_t *a, cte_t *b, tie_comp_t tie_break)
{
    return tie_break(a, b, cap_extra_comp);
}

static inline int
page_directory_comparator(cte_t *a, cte_t *b, tie_comp_t tie_break)
{
    return tie_break(a, b, cap_extra_comp);
}

static inline int
pdpt_comparator(cte_t *a, cte_t *b, tie_comp_t tie_break)
{
    return tie_break(a, b, cap_extra_comp);
}

static inline int
io_page_table_comparator(cte_t *a, cte_t *b, tie_comp_t tie_break)
{
    return tie_break(a, b, cap_extra_comp);
}

static inline int
io_space_comparator(cte_t *a, cte_t *b, tie_comp_t tie_break)
{
    return tie_break(a, b, cap_extra_comp);
}

static inline int
ept_pdpt_comparator(cte_t *a, cte_t *b, tie_comp_t tie_break)
{
    return tie_break(a, b, cap_extra_comp);
}

static inline int
ept_page_directory_comparator(cte_t *a, cte_t *b, tie_comp_t tie_break)
{
    return tie_break(a, b, cap_extra_comp);
}

static inline int
ept_page_table_comparator(cte_t *a, cte_t *b, tie_comp_t tie_break)
{
    return tie_break(a, b, cap_extra_comp);
}

static inline int
just_tie_break(cte_t *a, cte_t *b, tie_comp_t tie_break)
{
    return tie_break(a, b, NULL);
}

static inline int
typed_comparator(cte_t *a, cte_t *b, tie_comp_t tie_break)
{
    int cmp;
    cap_tag_t type;
    type_comp_t comp;
    static type_comp_t comparator[] = {
        [cap_endpoint_cap]       = endpoint_cap_comparator,
        [cap_async_endpoint_cap] = async_endpoint_cap_comparator,
        [cap_cnode_cap]          = just_tie_break,
        [cap_thread_cap]         = just_tie_break,
        [cap_frame_cap]          = frame_cap_comparator,
        [cap_page_table_cap]     = page_table_comparator,
        [cap_page_directory_cap] = page_directory_comparator,
#ifdef ARCH_IA32
        [cap_pdpt_cap]           = pdpt_comparator,
#endif
        [cap_zombie_cap]         = just_tie_break,
#ifdef CONFIG_IOMMU
        [cap_io_page_table_cap]  = io_page_table_comparator,
        [cap_io_space_cap]       = io_space_comparator,
#endif
#ifdef CONFIG_VTX
        [cap_vcpu_cap]           = just_tie_break,
        [cap_ept_page_directory_pointer_table_cap] = ept_pdpt_comparator,
        [cap_ept_page_directory_cap]               = ept_page_directory_comparator,
        [cap_ept_page_table_cap]                   = ept_page_table_comparator,
#endif
    };
    /* Typed objects do not overlap, so sufficient to compare base address */
    cmp = compare(cap_get_capPtr(a->cap), cap_get_capPtr(b->cap));
    if (cmp != EQ) {
        return cmp;
    }
    /* at this point we *know* the types must be equal, so call the
     * per cap type comparator, if it needs one. */
    type = cap_get_capType(a->cap);
    assert(type < ARRAY_SIZE(comparator));
    comp = comparator[type];
    assert(comp);
    return comp(a, b, tie_break);
}

static inline int
irq_comparator(cte_t *a, cte_t *b, tie_comp_t tie_break)
{
    int cmp;
    cap_tag_t typeA, typeB;
    /* The IRQ control cap can be thought of as having an 'address' of 0 and a 'size' of
     * the entire IRQ space. IRQ handlers then have an address that is their irq and a size
     * of 1. Since IRQ control caps cannot be subdivided this is equivalent to putting
     * all IRQ control caps first, then sorting IRQ handlers by their IRQ */
    typeA = cap_get_capType(a->cap);
    typeB = cap_get_capType(b->cap);
    if (typeA == typeB) {
        if (typeA == cap_irq_control_cap) {
            /* both control caps, tie break */
            return tie_break(a, b, NULL);
        } else {
            /* both irq handlers, compare on irq */
            assert(typeA == cap_irq_handler_cap);
            cmp = compare(cap_irq_handler_cap_get_capIRQ(a->cap), cap_irq_handler_cap_get_capIRQ(b->cap));
            if (cmp != EQ) {
                return cmp;
            }
            return tie_break(a, b, NULL);
        }
    } else if (typeA == cap_irq_control_cap) {
        return LT;
    } else {
        assert(typeA == cap_irq_handler_cap);
        return GT;
    }
}

#ifdef ARCH_IA32
static inline int
ioport_comparator(cte_t *a, cte_t *b, tie_comp_t tie_break)
{
    int cmp;
    uint32_t firstA, firstB, lastA, lastB;
    /* ioports have a base address and size that is defined by their start port and end port */
    firstA = cap_io_port_cap_get_capIOPortFirstPort(a->cap);
    firstB = cap_io_port_cap_get_capIOPortFirstPort(b->cap);
    cmp = compare(firstA, firstB);
    if (cmp != EQ) {
        return cmp;
    }
    lastA = cap_io_port_cap_get_capIOPortLastPort(a->cap);
    lastB = cap_io_port_cap_get_capIOPortLastPort(b->cap);
    cmp = - compare(lastA - firstA, lastB - firstB);
    if (cmp != EQ) {
        return cmp;
    }
    return tie_break(a, b, NULL);
}
#endif

#ifdef CONFIG_IOMMU
static inline int
iospace_comparator(cte_t *a, cte_t *b, tie_comp_t tie_break)
{
    int cmp;
    /* order by pci device this is assigned to and then by domain ID */
    cmp = compare(cap_io_space_cap_get_capPCIDevice(a->cap), cap_io_space_cap_get_capPCIDevice(b->cap));
    if (cmp != EQ) {
        return cmp;
    }
    cmp = compare(cap_io_space_cap_get_capDomainID(a->cap), cap_io_space_cap_get_capDomainID(b->cap));
    if (cmp != EQ) {
        return cmp;
    }
    return tie_break(a, b, NULL);
}
#endif

static inline int
compare_space(int space, cte_t *a, cte_t *b, tie_comp_t tie_break)
{
    type_comp_t comp;
    static type_comp_t comparator[] = {
        [capSpaceUntypedMemory] = untyped_comparator,
        [capSpaceTypedMemory] = typed_comparator,
        [capSpaceDomain] = just_tie_break,
        [capSpaceIRQ] = irq_comparator,
#ifdef CONFIG_IOMMU
        [capSpaceIOSpace] = iospace_comparator,
#endif
#ifdef ARCH_IA32
        [capSpaceIOPort] = ioport_comparator,
        [capSpaceIPI] = just_tie_break,
#endif
    };
    assert(space < ARRAY_SIZE(comparator));
    comp = comparator[space];
    assert(comp);
    return comp(a, b, tie_break);
}

static inline int
compSlotWith(cte_t *a, cte_t *b, tie_comp_t tie_break)
{
    /* check space */
    int spaceA = cap_get_capSpaceType(a->cap);
    int spaceB = cap_get_capSpaceType(b->cap);
    int cmp = compare(spaceA, spaceB);
    if (cmp != EQ) {
        return cmp;
    }
    /* now call the space specific comparator */
    return compare_space(spaceA, a, b, tie_break);
}

static inline int
compSlot(cte_t *a, cte_t *b)
{
    /* We know nothing, call general comparator for caps and tie break on slots */
    return compSlotWith(a, b, tie_break_comparator);
}

static inline int has_extra_comparator(cte_t *a, cte_t *b, comp_t pre_slot)
{
    int cmp;
    /* Check depth as per normal */
    cmp = compare(mdb_node_get_cdtDepth(a->cteMDBNode), mdb_node_get_cdtDepth(b->cteMDBNode));
    if (cmp != EQ) {
        return cmp;
    }
    assert(pre_slot);
    cmp = pre_slot(a, b);
    /* if the extra comparison was not equal then we found something, so we will claim that we found equality,
     * otherwise return a psudo-random result */
    if (cmp != EQ) {
        return EQ;
    }
    return LT;
}

cte_t *
cdtFindWithExtra(cap_t hypothetical)
{
    uint32_t i;
    unsigned int depth_bits = cte_depth_bits_cap(hypothetical);
    for (i = 0; i < BIT(depth_bits); i++) {
        cte_t *current;
        cte_t *next;

        cte_t slot = (cte_t) {
            .cap = hypothetical,
             .cteMDBNode = mdb_node_new(0, i, 0, 0)
        };

        next = ksRootCTE;
        do {
            int cmp;
            current = next;
            /* we are searching for a slot that is mostly equal to this node,
             * except that it has a non zero extra component */
            cmp = compSlotWith(current, &slot, has_extra_comparator);
            switch (cmp) {
            case LT:
                next = CTE_PTR(mdb_node_get_cdtRight(current->cteMDBNode));
                break;
            case GT:
                next = CTE_PTR(mdb_node_get_cdtLeft(current->cteMDBNode));
                break;
            case EQ:
                return current;
            }
        } while (next);
    }
    return NULL;
}

static inline int slot_eq_comparator(cte_t *a, cte_t *b, comp_t pre_slot)
{
    int cmp;
    /* Check depth and pre_slot as per normal */
    cmp = compare(mdb_node_get_cdtDepth(a->cteMDBNode), mdb_node_get_cdtDepth(b->cteMDBNode));
    if (cmp != EQ) {
        return cmp;
    }
    if (pre_slot) {
        cmp = pre_slot(a, b);
        if (cmp != EQ) {
            return cmp;
        }
    }
    /* Slot is always EQ */
    return EQ;
}

cte_t *
cdtFindAtDepth(cap_t hypothetical, uint32_t depth)
{
    cte_t *current;
    cte_t *next;

    cte_t slot = (cte_t) {
        .cap = hypothetical,
         .cteMDBNode = mdb_node_new(0, depth, 0, 0)
    };

    next = ksRootCTE;
    /* we want to find the entry in the tree that is equal to this node
     * in every way except that it will have a different slot. So we will
     * do a search with a comparator that always returns equality on slots */
    do {
        current = next;
        switch (compSlotWith(current, &slot, slot_eq_comparator)) {
        case LT:
            next = CTE_PTR(mdb_node_get_cdtRight(current->cteMDBNode));
            break;
        case GT:
            next = CTE_PTR(mdb_node_get_cdtLeft(current->cteMDBNode));
            break;
        case EQ:
            return current;
        }
    } while (next);
    return NULL;
}

cte_t *
cdtFind(cap_t hypothetical)
{
    uint32_t i;
    cte_t *ret;
    unsigned int depth_bits = cte_depth_bits_cap(hypothetical);
    for (i = 0; i < BIT(depth_bits); i++) {
        ret = cdtFindAtDepth(hypothetical, i);
        if (ret) {
            return ret;
        }
    }
    return NULL;
}

bool_t
cdtIsFinal(cte_t *slot)
{
    cte_t *closest;

    /* For finality testing it is sufficient to check the objects immediately
     * before and after us in cdt ordering. This is because we are only
     * interested in equivalent objects, not whether something is actually
     * a parent or not */
    closest = aaTraverseForward(slot);
    if (closest && sameObjectAs(closest->cap, slot->cap)) {
        return false;
    }
    closest = aaTraverseBackward(slot);
    if (closest && sameObjectAs(closest->cap, slot->cap)) {
        return false;
    }
    return true;
}

static inline cap_t
build_largest_child(cap_t cap)
{
    switch (cap_get_capType(cap)) {
    case cap_domain_cap:
#ifdef ARCH_IA32
    case cap_ipi_cap:
#endif
    case cap_irq_handler_cap:
    case cap_cnode_cap:
    case cap_thread_cap:
#ifdef CONFIG_VTX
    case cap_vcpu_cap:
#endif
    case cap_zombie_cap:
        return cap;
#ifdef CONFIG_IA32
    case cap_io_port_cap:
        /* We order on base address first, so set it has high as possible, size doesn't matter then.
           But size shouldn't be zero */
        return cap_io_port_cap_new(cap_io_port_cap_get_capIOPortLastPort(cap) - 1, 1);
#endif
    case cap_irq_control_cap:
        /* Largest child is a irq handler with biggest irq */
        return cap_irq_handler_cap_new(0xff);
    case cap_untyped_cap:
        /* untyped cap of smallest size at the end of this region */
        return cap_untyped_cap_new(0, 4, cap_untyped_cap_get_capPtr(cap) + BIT(cap_untyped_cap_get_capBlockSize(cap)) - BIT(4));
    case cap_endpoint_cap:
        if (cap_endpoint_cap_get_capEPBadge(cap) == 0) {
            return cap_endpoint_cap_new(BIT(28) - 1, 0, 0, 0, cap_endpoint_cap_get_capEPPtr(cap));
        }
        return cap;
    case cap_async_endpoint_cap:
        if (cap_async_endpoint_cap_get_capAEPBadge(cap) == 0) {
            return cap_async_endpoint_cap_new(BIT(28) - 1, 0, 0, cap_async_endpoint_cap_get_capAEPPtr(cap));
        }
        return cap;
        /* We get away with not setting the extra higher as we will always be comparing
         * with an infinite depth, hence any 'extra' is not relevant */
    case cap_frame_cap:
    case cap_page_table_cap:
    case cap_page_directory_cap:
#ifdef ARCH_IA32
    case cap_pdpt_cap:
#endif
#ifdef CONFIG_VTX
    case cap_ept_page_directory_pointer_table_cap:
    case cap_ept_page_directory_cap:
    case cap_ept_page_table_cap:
#endif
#ifdef CONFIG_IOMMU
    case cap_io_page_table_cap:
    case cap_io_space_cap:
#endif
        return cap;
    default:
        fail("Unknown cap type");
    }
}

static inline int largest_child_comparator(cte_t *a, cte_t *b, comp_t pre_slot)
{
    /* Tie breaking for largest child is easy. Their depth is always less than ours */
    return LT;
}

static inline int slot_lt_comparator(cte_t *a, cte_t *b, comp_t pre_slot)
{
    int cmp;
    /* Check depth and pre_slot as per normal */
    cmp = compare(mdb_node_get_cdtDepth(a->cteMDBNode), mdb_node_get_cdtDepth(b->cteMDBNode));
    if (cmp != EQ) {
        return cmp;
    }
    if (pre_slot) {
        cmp = pre_slot(a, b);
        if (cmp != EQ) {
            return cmp;
        }
    }
    /* Slot is always LT */
    return LT;
}

static inline cte_t *
aaFindFromBelow(cte_t *hypothetical, tie_comp_t tie_break)
{
    cte_t *current;
    cte_t *largest;
    cte_t *next;
    next = ksRootCTE;
    largest = NULL;
    do {
        int cmp;
        current = next;
        cmp = compSlotWith(current, hypothetical, tie_break);
        if (cmp == LT) {
            next = CTE_PTR(mdb_node_get_cdtRight(current->cteMDBNode));
            if (!largest || compSlot(current, largest) == GT) {
                largest = current;
            }
        } else {
            next = CTE_PTR(mdb_node_get_cdtLeft(current->cteMDBNode));
        }
    } while (next);
    return largest;
}

/* Finding a child is complicated because your child may not
 * live directly after you in cdt order. That is, if you take
 * ever node in the tree and squash it into a list, directly
 * after you may be some N number of siblings, then your
 * children. This is why we need to do a creative search
 * where as cdtIsFinal was able to get away with checking
 * neighbouring nodes */
static cte_t *
_cdtFindChild(cte_t *parentSlot)
{
    cte_t *child;
    /* Construct a hypothetical child. This needs to be the largest
     * possible child such that anything greater than it would no
     * longer be our child and anything less than it is either
     * our sibling or our child. We do not worry about the depth
     * as we will use a fake comparator that assumes our node
     * is of infinite depth */
    cte_t hypothetical = {
        .cap = build_largest_child(parentSlot->cap),
    };

    /* Search for hypothetical cap from below. */
    child = aaFindFromBelow(&hypothetical, largest_child_comparator);

    /* Verify that this is in fact a child (we could have none). To ensure
     * we did not find ourself or a sibling we ensure that we are strictly
     * greater than ignoring slot tie breaks */
    if (!child || compSlotWith(child, parentSlot, slot_lt_comparator) != GT) {
        return NULL;
    }
    return child;
}

cte_t *
cdtFindTypedInRange(word_t base, unsigned int size_bits)
{
    cte_t *child;
    /* Construct the smallest typed object we know about at the top
     * of the memory range and search for it */
    cte_t hypothetical = {
        .cap = cap_endpoint_cap_new(0, 0, 0, 0, base + BIT(size_bits) - BIT(EP_SIZE_BITS)),
    };
    /* Search for it from below */
    child = aaFindFromBelow(&hypothetical, largest_child_comparator);
    /* Check we found something in the right range. Construct a fake untyped
     * to reuse existing range checking */
    if (child && sameRegionAs(cap_untyped_cap_new(0, size_bits, base), child->cap)) {
        return child;
    }
    return NULL;
}

cte_t *
cdtFindChild(cte_t *parentSlot)
{
    if (cap_get_capSpaceType(parentSlot->cap) == capSpaceUntypedMemory) {
        /* Find anything in this range that is typed */
        cte_t *result = cdtFindTypedInRange(cap_untyped_cap_get_capPtr(parentSlot->cap), cap_untyped_cap_get_capBlockSize(parentSlot->cap));
        if (result) {
            return result;
        }
    }
    return _cdtFindChild(parentSlot);
}

static inline void
cdtInsertTree(cte_t *slot)
{
    ksRootCTE = aaInsert(ksRootCTE, slot);
}

void
cdtInsert(cte_t *parentSlot, cte_t *newSlot)
{
    word_t depth;
    assert(cap_get_capType(newSlot->cap) != cap_null_cap);
    assert(!parentSlot || cap_get_capType(parentSlot->cap) != cap_null_cap);
    if (!parentSlot || (cap_get_capSpaceType(parentSlot->cap) != cap_get_capSpaceType(newSlot->cap))) {
        depth = 0;
    } else {
        depth = mdb_node_get_cdtDepth(parentSlot->cteMDBNode) + 1;
        if (depth == BIT(cte_depth_bits_cap(newSlot->cap))) {
            depth--;
        }
    }
    newSlot->cteMDBNode = mdb_node_new(0, depth, 0, 0);
    cdtInsertTree(newSlot);
}

void
cdtRemove(cte_t *slot)
{
    assert(cap_get_capType(slot->cap) != cap_null_cap);
    ksRootCTE = aaRemove(false, ksRootCTE, slot);
    slot->cteMDBNode = nullMDBNode;
}

void
cdtMove(cte_t *oldSlot, cte_t *newSlot)
{
    assert(cap_get_capType(oldSlot->cap) != cap_null_cap);
    assert(cap_get_capType(newSlot->cap) != cap_null_cap);
    ksRootCTE = aaRemove(false, ksRootCTE, oldSlot);

    newSlot->cteMDBNode = mdb_node_new(0, mdb_node_get_cdtDepth(oldSlot->cteMDBNode), 0, 0);
    oldSlot->cteMDBNode = mdb_node_new(0, 0, 0, 0);

    ksRootCTE = aaInsert(ksRootCTE, newSlot);
}

void
cdtUpdate(cte_t *slot, cap_t newCap)
{
    if (capsEqual(slot->cap, newCap)) {
        slot->cap = newCap;
    } else {
        ksRootCTE = aaRemove(false, ksRootCTE, slot);
        slot->cteMDBNode = mdb_node_new(0, mdb_node_get_cdtDepth(slot->cteMDBNode), 0, 0);
        slot->cap = newCap;
        ksRootCTE = aaInsert(ksRootCTE, slot);
    }
}

void
cdtSwap(cap_t cap1, cte_t *slot1, cap_t cap2, cte_t *slot2)
{
    word_t depth1, depth2;
    assert(slot1 != slot2);
    if (cap_get_capType(slot1->cap) != cap_null_cap) {
        ksRootCTE = aaRemove(false, ksRootCTE, slot1);
    }
    if (cap_get_capType(slot2->cap) != cap_null_cap) {
        ksRootCTE = aaRemove(false, ksRootCTE, slot2);
    }
    depth1 = mdb_node_get_cdtDepth(slot1->cteMDBNode);
    depth2 = mdb_node_get_cdtDepth(slot2->cteMDBNode);
    slot1->cteMDBNode = mdb_node_new(0, depth2, 0, 0);
    slot2->cteMDBNode = mdb_node_new(0, depth1, 0, 0);

    slot1->cap = cap2;
    slot2->cap = cap1;

    if (cap_get_capType(slot1->cap) != cap_null_cap) {
        ksRootCTE = aaInsert(ksRootCTE, slot1);
    }
    if (cap_get_capType(slot2->cap) != cap_null_cap) {
        ksRootCTE = aaInsert(ksRootCTE, slot2);
    }
}

/*****************************************************************************
 * AA Tree implementation
 *****************************************************************************/

/* AA Tree rebalancing functions */
static cte_t *aaRemoveNode(bool_t isSwapped, cte_t *rootSlot);
static cte_t *aaRebalance(cte_t *slot);
static cte_t *aaDecLevel(cte_t *slot);
static cte_t *aaSkew(cte_t *slot);
static cte_t *aaSplit(cte_t *slot);

static cte_t * aaSucc(cte_t *slot)
{
    cte_t *left;

    left = CTE_PTR(mdb_node_get_cdtLeft(slot->cteMDBNode));
    while (left) {
        slot = left;
        left = CTE_PTR(mdb_node_get_cdtLeft(slot->cteMDBNode));
    }
    return slot;
}

static cte_t * aaPred(cte_t *slot)
{
    cte_t *right;

    right = CTE_PTR(mdb_node_get_cdtRight(slot->cteMDBNode));
    while (right) {
        slot = right;
        right = CTE_PTR(mdb_node_get_cdtRight(slot->cteMDBNode));
    }
    return slot;
}

static cte_t *aaParent(cte_t *slot)
{
    cte_t *current = NULL;
    cte_t *next;

    next = ksRootCTE;
    while (next != slot) {
        current = next;
        switch (compSlot(current, slot)) {
        case LT:
            next = CTE_PTR(mdb_node_get_cdtRight(current->cteMDBNode));
            break;
        case GT:
            next = CTE_PTR(mdb_node_get_cdtLeft(current->cteMDBNode));
            break;
        case EQ:
            return current;
        }
    }
    return current;
}

static cte_t *aaTraverseBackward(cte_t *slot)
{
    cte_t *parent;
    cte_t *left;
    /* Optimistically see if we our predecessor is a child */
    left = CTE_PTR(mdb_node_get_cdtLeft(slot->cteMDBNode));
    if (left) {
        return aaPred(left);
    }
    /* We need to find our parent. This is actually hard so we
     * need to find ourselves and perform a trace as we do so */

    /* search upwards until we find an ancestor on a right link,
     * we have then found something before us */
    parent = aaParent(slot);
    while (parent && CTE_PTR(mdb_node_get_cdtRight(parent->cteMDBNode)) != slot) {
        slot = parent;
        parent = aaParent(parent);
    }
    return parent;
}

static cte_t *aaTraverseForward(cte_t *slot)
{
    cte_t *parent;
    cte_t *right;
    /* Optimistically see if we our successor is a child */
    right = CTE_PTR(mdb_node_get_cdtRight(slot->cteMDBNode));
    if (right) {
        return aaSucc(right);
    }
    /* We need to find our parent. This is actually hard so we
     * need to find ourselves and perform a trace as we do so */


    /* search upwards until we find an ancestor on a left link,
     * we have then found something before us */
    parent = aaParent(slot);
    while (parent && CTE_PTR(mdb_node_get_cdtLeft(parent->cteMDBNode)) != slot) {
        slot = parent;
        parent = aaParent(parent);
    }
    return parent;
}

static inline int
aaLevel(cte_t *slot)
{
    if (!slot) {
        return 0;
    }
    return mdb_node_get_cdtLevel(slot->cteMDBNode);
}

static inline int CONST min(int a, int b)
{
    return (a < b) ? a : b;
}

static cte_t *aaInsert(cte_t *rootSlot, cte_t *newSlot)
{
    cte_t *left, *right;

    if (!newSlot) {
        fail("inserting null CTE");
    }
    assert(newSlot != rootSlot);

    if (!rootSlot) {

        mdb_node_ptr_set_cdtLevel(&newSlot->cteMDBNode, 1);
        return newSlot;

    } else {

        switch (compSlot(newSlot, rootSlot)) {
        case GT:
            right = CTE_PTR(mdb_node_get_cdtRight(rootSlot->cteMDBNode));
            right = aaInsert(right, newSlot);
            mdb_node_ptr_set_cdtRight(&rootSlot->cteMDBNode, CTE_REF(right));
            break;

        case LT:
            left = CTE_PTR(mdb_node_get_cdtLeft(rootSlot->cteMDBNode));
            left = aaInsert(left, newSlot);
            mdb_node_ptr_set_cdtLeft(&rootSlot->cteMDBNode, CTE_REF(left));
            break;

        default:
            fail("Inserting duplicate");
        }

        rootSlot = aaSkew(rootSlot);
        rootSlot = aaSplit(rootSlot);

        return rootSlot;
    }
}

static cte_t *aaRemove(bool_t isSwapped, cte_t *rootSlot, cte_t *targetSlot)
{
    cte_t *left, *right;

    if (!targetSlot) {
        fail("removing null");
    }
    if (!rootSlot) {
        fail("removing from null");
    }

    switch (compSlot(targetSlot, rootSlot)) {
    case GT:
        right = CTE_PTR(mdb_node_get_cdtRight(rootSlot->cteMDBNode));
        right = aaRemove(isSwapped, right, targetSlot);
        mdb_node_ptr_set_cdtRight(&rootSlot->cteMDBNode, CTE_REF(right));
        break;
    case LT:
        left = CTE_PTR(mdb_node_get_cdtLeft(rootSlot->cteMDBNode));
        left = aaRemove(isSwapped, left, targetSlot);
        mdb_node_ptr_set_cdtLeft(&rootSlot->cteMDBNode, CTE_REF(left));
        break;
    default:
        rootSlot = aaRemoveNode(isSwapped, rootSlot);
    }
    rootSlot = aaRebalance(rootSlot);
    return rootSlot;
}

/* AA Tree rebalancing functions */

static cte_t *aaRemoveNode(bool_t isSwapped, cte_t *rootSlot)
{
    cte_t *left, *right, *pred, *succ;
    mdb_node_t mdb;

    mdb = rootSlot->cteMDBNode;

    left = CTE_PTR(mdb_node_get_cdtLeft(mdb));
    right = CTE_PTR(mdb_node_get_cdtRight(mdb));
    if (left) {
        pred = aaPred(left);
        left = aaRemove(true, left, pred);

        mdb_node_ptr_set_cdtLevel(&pred->cteMDBNode, mdb_node_get_cdtLevel(mdb));
        mdb_node_ptr_set_cdtRight(&pred->cteMDBNode, mdb_node_get_cdtRight(mdb));
        mdb_node_ptr_set_cdtLeft(&pred->cteMDBNode, CTE_REF(left));

        return pred;

    } else if (right) {
        succ = aaSucc(right);
        right = aaRemove(true, right, succ);

        mdb_node_ptr_set_cdtLevel(&succ->cteMDBNode, mdb_node_get_cdtLevel(mdb));
        mdb_node_ptr_set_cdtRight(&succ->cteMDBNode, CTE_REF(right));
        mdb_node_ptr_set_cdtLeft(&succ->cteMDBNode, CTE_REF(NULL));

        return succ;

    } else {
        return NULL;
    }
}

static cte_t *aaRebalance(cte_t *slot)
{
    cte_t *right, *right_right;

    if (!slot) {
        return NULL;
    }

    slot = aaDecLevel(slot);
    slot = aaSkew(slot);

    right = aaSkew(CTE_PTR(mdb_node_get_cdtRight(slot->cteMDBNode)));
    mdb_node_ptr_set_cdtRight(&slot->cteMDBNode, CTE_REF(right));

    if (right) {
        right_right = aaSkew(CTE_PTR(mdb_node_get_cdtRight(right->cteMDBNode)));
        mdb_node_ptr_set_cdtRight(&right->cteMDBNode, CTE_REF(right_right));
    }

    slot = aaSplit(slot);

    right = aaSplit(CTE_PTR(mdb_node_get_cdtRight(slot->cteMDBNode)));
    mdb_node_ptr_set_cdtRight(&slot->cteMDBNode, CTE_REF(right));

    return slot;
}

static cte_t *aaDecLevel(cte_t *slot)
{
    cte_t *left, *right;
    int should_be;

    if (!slot) {
        return NULL;
    }

    left = CTE_PTR(mdb_node_get_cdtLeft(slot->cteMDBNode));
    right = CTE_PTR(mdb_node_get_cdtRight(slot->cteMDBNode));

    should_be = min(aaLevel(left), aaLevel(right)) + 1;

    if (should_be < mdb_node_get_cdtLevel(slot->cteMDBNode)) {
        mdb_node_ptr_set_cdtLevel(&slot->cteMDBNode, should_be);

        if (right && should_be < mdb_node_get_cdtLevel(right->cteMDBNode)) {
            mdb_node_ptr_set_cdtLevel(&right->cteMDBNode, should_be);
        }
    }

    return slot;
}

static cte_t *aaSplit(cte_t *slot)
{
    cte_t *right, *right_right;
    int level;

    /*
     *                             |
     *     |                      |R|
     *    |T|->|R|->|X|   =>     /   \
     *   /    /                |T|   |X|
     * |A|  |B|               /   \
     *                      |A|   |B|
     */

    if (!slot) {
        return NULL;
    }

    right = CTE_PTR(mdb_node_get_cdtRight(slot->cteMDBNode));
    if (right) {

        right_right = CTE_PTR(mdb_node_get_cdtRight(right->cteMDBNode));
        if (right_right && mdb_node_get_cdtLevel(slot->cteMDBNode)
                == mdb_node_get_cdtLevel(right_right->cteMDBNode)) {

            mdb_node_ptr_set_cdtRight(&slot->cteMDBNode,
                                      mdb_node_get_cdtLeft(right->cteMDBNode));

            level = mdb_node_get_cdtLevel(right->cteMDBNode) + 1;
            mdb_node_ptr_set_cdtLevel(&right->cteMDBNode, level);

            /* check level dosn't overflow */
            assert(mdb_node_get_cdtLevel(right->cteMDBNode) == level);

            mdb_node_ptr_set_cdtLeft(&right->cteMDBNode, CTE_REF(slot));

            return right;
        }
    }

    return slot;
}

static cte_t *aaSkew(cte_t *slot)
{
    cte_t *left;

    /*
     *          |              |
     *    |L|<-|T|     =>     |L|->|T|
     *   /   \    \          /    /   \
     * |A|   |B|  |R|      |A|  |B|   |R|
     */

    if (!slot) {
        return NULL;
    }

    left = CTE_PTR(mdb_node_get_cdtLeft(slot->cteMDBNode));
    if (left && mdb_node_get_cdtLevel(left->cteMDBNode)
            == mdb_node_get_cdtLevel(slot->cteMDBNode)) {

        mdb_node_ptr_set_cdtLeft(&slot->cteMDBNode,
                                 mdb_node_get_cdtRight(left->cteMDBNode));
        mdb_node_ptr_set_cdtRight(&left->cteMDBNode, CTE_REF(slot));

        return left;
    }

    return slot;
}

/*****************************************************************************
 * AA Tree Debug Functions
 *****************************************************************************/

static char *
printCap(cap_t cap)
{
    switch (cap_get_capType(cap)) {
    case cap_null_cap:
        return "NullCap";
    case cap_untyped_cap:
        return "Untyped";
    case cap_endpoint_cap:
        return "Endpoint";
    case cap_async_endpoint_cap:
        return "AsyncEndpoint";
    case cap_reply_cap:
        return "Reply";
    case cap_cnode_cap:
        return "CNode";
    case cap_thread_cap:
        return "Thread";
    default:
        return "?";
    }
}

void
printCTE(char *msg, cte_t *cte)
{
    (void)printCap;
    if (!cte) {
        printf("%s [NULL]@0x%x", msg, cte);
    } else  {
        printf("%s [%d %s(%d) { addr = 0x%x, size = 0x%x } left: 0x%x right: 0x%x badge: %d depth: %d extra: 0x%x]@0x%x\n",
               msg,
               mdb_node_get_cdtLevel(cte->cteMDBNode),
               printCap(cte->cap),
               cap_get_capType(cte->cap),
               cap_get_capType(cte->cap) == cap_null_cap ? 0 : (word_t)cap_get_capSpacePtr(cte->cap),
               cap_get_capType(cte->cap) == cap_null_cap ? 0 : cap_get_capSpaceSize(cte->cap),
               mdb_node_get_cdtLeft(cte->cteMDBNode),
               mdb_node_get_cdtRight(cte->cteMDBNode),
               cap_get_capBadge(cte->cap),
               mdb_node_get_cdtDepth(cte->cteMDBNode),
               cap_get_capType(cte->cap) == cap_null_cap ? 0 : cap_get_capExtraComp(cte->cap),
               cte);
    }
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/kernel/cspace.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <types.h>
#include <object.h>
#include <api/failures.h>
#include <kernel/thread.h>
#include <kernel/cspace.h>
#include <model/statedata.h>
#include <arch/machine.h>

lookupCap_ret_t
lookupCap(tcb_t *thread, cptr_t cPtr)
{
    lookupSlot_raw_ret_t lu_ret;
    lookupCap_ret_t ret;

    lu_ret = lookupSlot(thread, cPtr);
    if (unlikely(lu_ret.status != EXCEPTION_NONE)) {
        ret.status = lu_ret.status;
        ret.cap = cap_null_cap_new();
        return ret;
    }

    ret.status = EXCEPTION_NONE;
    ret.cap = lu_ret.slot->cap;
    return ret;
}

lookupCapAndSlot_ret_t
lookupCapAndSlot(tcb_t *thread, cptr_t cPtr)
{
    lookupSlot_raw_ret_t lu_ret;
    lookupCapAndSlot_ret_t ret;

    lu_ret = lookupSlot(thread, cPtr);
    if (unlikely(lu_ret.status != EXCEPTION_NONE)) {
        ret.status = lu_ret.status;
        ret.slot = NULL;
        ret.cap = cap_null_cap_new();
        return ret;
    }

    ret.status = EXCEPTION_NONE;
    ret.slot = lu_ret.slot;
    ret.cap = lu_ret.slot->cap;
    return ret;
}

lookupSlot_raw_ret_t
lookupSlot(tcb_t *thread, cptr_t capptr)
{
    cap_t threadRoot;
    resolveAddressBits_ret_t res_ret;
    lookupSlot_raw_ret_t ret;

    threadRoot = TCB_PTR_CTE_PTR(thread, tcbCTable)->cap;
    res_ret = resolveAddressBits(threadRoot, capptr, wordBits);

    ret.status = res_ret.status;
    ret.slot = res_ret.slot;
    return ret;
}

lookupSlot_ret_t
lookupSlotForCNodeOp(bool_t isSource, cap_t root, cptr_t capptr,
                     unsigned int depth)
{
    resolveAddressBits_ret_t res_ret;
    lookupSlot_ret_t ret;

    ret.slot = NULL;

    if (unlikely(cap_get_capType(root) != cap_cnode_cap)) {
        current_syscall_error.type = seL4_FailedLookup;
        current_syscall_error.failedLookupWasSource = isSource;
        current_lookup_fault = lookup_fault_invalid_root_new();
        ret.status = EXCEPTION_SYSCALL_ERROR;
        return ret;
    }

    if (unlikely(depth < 1 || depth > wordBits)) {
        current_syscall_error.type = seL4_RangeError;
        current_syscall_error.rangeErrorMin = 1;
        current_syscall_error.rangeErrorMax = wordBits;
        ret.status = EXCEPTION_SYSCALL_ERROR;
        return ret;
    }

    res_ret = resolveAddressBits(root, capptr, depth);
    if (unlikely(res_ret.status != EXCEPTION_NONE)) {
        current_syscall_error.type = seL4_FailedLookup;
        current_syscall_error.failedLookupWasSource = isSource;
        /* current_lookup_fault will have been set by resolveAddressBits */
        ret.status = EXCEPTION_SYSCALL_ERROR;
        return ret;
    }

    if (unlikely(res_ret.bitsRemaining != 0)) {
        current_syscall_error.type = seL4_FailedLookup;
        current_syscall_error.failedLookupWasSource = isSource;
        current_lookup_fault =
            lookup_fault_depth_mismatch_new(0, res_ret.bitsRemaining);
        ret.status = EXCEPTION_SYSCALL_ERROR;
        return ret;
    }

    ret.slot = res_ret.slot;
    ret.status = EXCEPTION_NONE;
    return ret;
}

lookupSlot_ret_t
lookupSourceSlot(cap_t root, cptr_t capptr, unsigned int depth)
{
    return lookupSlotForCNodeOp(true, root, capptr, depth);
}

lookupSlot_ret_t
lookupTargetSlot(cap_t root, cptr_t capptr, unsigned int depth)
{
    return lookupSlotForCNodeOp(false, root, capptr, depth);
}

lookupSlot_ret_t
lookupPivotSlot(cap_t root, cptr_t capptr, unsigned int depth)
{
    return lookupSlotForCNodeOp(true, root, capptr, depth);
}

resolveAddressBits_ret_t
resolveAddressBits(cap_t nodeCap, cptr_t capptr, unsigned int n_bits)
{
    resolveAddressBits_ret_t ret;
    unsigned int radixBits, guardBits, levelBits, offset;
    cte_t *slot;

    ret.bitsRemaining = n_bits;
    ret.slot = NULL;

    if (unlikely(cap_get_capType(nodeCap) != cap_cnode_cap)) {
        current_lookup_fault = lookup_fault_invalid_root_new();
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    }

    guardBits = cap_cnode_cap_get_capCNodeGuardSize(nodeCap);
    if (unlikely(guardBits > n_bits)) {
        current_lookup_fault =
            lookup_fault_guard_mismatch_new(0, n_bits, guardBits);
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    }
    n_bits -= guardBits;

    while (1) {
        radixBits = cap_cnode_cap_get_capCNodeRadix(nodeCap);
        levelBits = radixBits;

        /* Haskell error: "All CNodes must resolve bits" */
        assert(levelBits != 0);

        if (unlikely(levelBits > n_bits)) {
            current_lookup_fault =
                lookup_fault_depth_mismatch_new(levelBits, n_bits);
            ret.status = EXCEPTION_LOOKUP_FAULT;
            return ret;
        }

        offset = (capptr >> (n_bits - levelBits)) & MASK(radixBits);
        slot = CTE_PTR(cap_cnode_cap_get_capCNodePtr(nodeCap)) + offset;

        if (likely(n_bits <= levelBits)) {
            ret.status = EXCEPTION_NONE;
            ret.slot = slot;
            ret.bitsRemaining = 0;
            return ret;
        }

        /** GHOSTUPD: "(\<acute>levelBits > 0, id)" */

        n_bits -= levelBits;
        nodeCap = slot->cap;

        if (unlikely(cap_get_capType(nodeCap) != cap_cnode_cap)) {
            ret.status = EXCEPTION_NONE;
            ret.slot = slot;
            ret.bitsRemaining = n_bits;
            return ret;
        }
    }

    ret.status = EXCEPTION_NONE;
    return ret;
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/kernel/faulthandler.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <api/failures.h>
#include <kernel/cspace.h>
#include <kernel/faulthandler.h>
#include <kernel/thread.h>
#include <machine/io.h>
#include <arch/machine.h>

void
handleFault(tcb_t *tptr)
{
    exception_t status;
    fault_t fault = current_fault;

    status = sendFaultIPC(tptr);
    if (status != EXCEPTION_NONE) {
        handleDoubleFault(tptr, fault);
    }
}

exception_t
sendFaultIPC(tcb_t *tptr)
{
    cptr_t handlerCPtr;
    cap_t  handlerCap;
    lookupCap_ret_t lu_ret;
    lookup_fault_t original_lookup_fault;

    original_lookup_fault = current_lookup_fault;

    handlerCPtr = tptr->tcbFaultHandler;
    lu_ret = lookupCap(tptr, handlerCPtr);
    if (lu_ret.status != EXCEPTION_NONE) {
        current_fault = fault_cap_fault_new(handlerCPtr, false);
        return EXCEPTION_FAULT;
    }
    handlerCap = lu_ret.cap;

    if (cap_get_capType(handlerCap) == cap_endpoint_cap &&
            cap_endpoint_cap_get_capCanSend(handlerCap) &&
            cap_endpoint_cap_get_capCanGrant(handlerCap)) {
        tptr->tcbFault = current_fault;
        if (fault_get_faultType(current_fault) == fault_cap_fault) {
            tptr->tcbLookupFailure = original_lookup_fault;
        }
        sendIPC(true, false,
                cap_endpoint_cap_get_capEPBadge(handlerCap),
                true, tptr,
                EP_PTR(cap_endpoint_cap_get_capEPPtr(handlerCap)));

        return EXCEPTION_NONE;
    } else {
        current_fault = fault_cap_fault_new(handlerCPtr, false);
        current_lookup_fault = lookup_fault_missing_capability_new(0);

        return EXCEPTION_FAULT;
    }
}

#ifdef DEBUG
static void
print_fault(fault_t f)
{
    switch (fault_get_faultType(f)) {
    case fault_null_fault:
        printf("null fault");
        break;
    case fault_cap_fault:
        printf("cap fault in %s phase at address 0x%x",
               fault_cap_fault_get_inReceivePhase(f) ? "receive" : "send",
               (unsigned int)fault_cap_fault_get_address(f));
        break;
    case fault_vm_fault:
        printf("vm fault on %s at address 0x%x with status 0x%x",
               fault_vm_fault_get_instructionFault(f) ? "code" : "data",
               (unsigned int)fault_vm_fault_get_address(f),
               (unsigned int)fault_vm_fault_get_FSR(f));
        break;
    case fault_unknown_syscall:
        printf("unknown syscall 0x%x",
               (unsigned int)fault_unknown_syscall_get_syscallNumber(f));
        break;
    case fault_user_exception:
        printf("user exception 0x%x code 0x%x",
               (unsigned int)fault_user_exception_get_number(f),
               (unsigned int)fault_user_exception_get_code(f));
        break;
    default:
        printf("unknown fault");
        break;
    }
}
#endif

/* The second fault, ex2, is stored in the global current_fault */
void
handleDoubleFault(tcb_t *tptr, fault_t ex1)
{
#ifdef DEBUG
    fault_t ex2 = current_fault;
    printf("Caught ");
    print_fault(ex2);
    printf("\nwhile trying to handle:\n");
    print_fault(ex1);
    printf("\nin thread 0x%x \"%s\" ", (unsigned int)tptr, tptr->tcbName);
    printf("at address 0x%x\n", (unsigned int)getRestartPC(tptr));
#endif

    setThreadState(tptr, ThreadState_Inactive);
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/kernel/thread.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>
#include <object.h>
#include <util.h>
#include <api/faults.h>
#include <api/types.h>
#include <kernel/cspace.h>
#include <kernel/thread.h>
#include <kernel/vspace.h>
#include <model/statedata.h>
#include <arch/machine.h>
#include <arch/kernel/thread.h>
#include <machine/registerset.h>
#include <arch/linker.h>

static message_info_t
transferCaps(message_info_t info, extra_caps_t caps,
             endpoint_t *endpoint, tcb_t *receiver,
             word_t *receiveBuffer, bool_t diminish);

static inline bool_t PURE
isBlocked(const tcb_t *thread)
{
    switch (thread_state_get_tsType(thread->tcbState)) {
    case ThreadState_Inactive:
    case ThreadState_BlockedOnReceive:
    case ThreadState_BlockedOnSend:
    case ThreadState_BlockedOnAsyncEvent:
    case ThreadState_BlockedOnReply:
        return true;

    default:
        return false;
    }
}

static inline bool_t PURE
isRunnable(const tcb_t *thread)
{
    switch (thread_state_get_tsType(thread->tcbState)) {
    case ThreadState_Running:
    case ThreadState_RunningVM:
    case ThreadState_Restart:
        return true;

    default:
        return false;
    }
}

BOOT_CODE void
configureIdleThread(tcb_t *tcb)
{
    Arch_configureIdleThread(tcb);
    setThreadState(tcb, ThreadState_IdleThreadState);
}

void
activateThread(void)
{
    switch (thread_state_get_tsType(ksCurThread->tcbState)) {
    case ThreadState_Running:
    case ThreadState_RunningVM:
        break;

    case ThreadState_Restart: {
        word_t pc;

        pc = getRestartPC(ksCurThread);
        setNextPC(ksCurThread, pc);
        setThreadState(ksCurThread, ThreadState_Running);
        break;
    }

    case ThreadState_IdleThreadState:
        Arch_activateIdleThread(ksCurThread);
        break;

    default:
        fail("Current thread is blocked");
    }
}

void
suspend(tcb_t *target)
{
    ipcCancel(target);
    /*if (cap_get_capType(TCB_PTR_CTE_PTR(target, tcbCaller)->cap) == cap_reply_cap)*/
    {
        deleteCallerCap(target);
    }
    setThreadState(target, ThreadState_Inactive);
    tcbSchedDequeue(target);
}

void
restart(tcb_t *target)
{
    if (isBlocked(target)) {
        ipcCancel(target);
        setupReplyMaster(target);
        setThreadState(target, ThreadState_Restart);
        tcbSchedEnqueue(target);
        switchIfRequiredTo(target);
    }
}

void
doIPCTransfer(tcb_t *sender, endpoint_t *endpoint, word_t badge,
              bool_t grant, tcb_t *receiver, bool_t diminish)
{
    void *receiveBuffer, *sendBuffer;

    receiveBuffer = lookupIPCBuffer(true, receiver);

    if (likely(!fault_get_faultType(sender->tcbFault) != fault_null_fault)) {
        sendBuffer = lookupIPCBuffer(false, sender);
        doNormalTransfer(sender, sendBuffer, endpoint, badge, grant,
                         receiver, receiveBuffer, diminish);
    } else {
        doFaultTransfer(badge, sender, receiver, receiveBuffer);
    }
}

void
doReplyTransfer(tcb_t *sender, tcb_t *receiver, cte_t *slot)
{
    assert(thread_state_get_tsType(receiver->tcbState) ==
           ThreadState_BlockedOnReply);

    if (likely(fault_get_faultType(receiver->tcbFault) == fault_null_fault)) {
        doIPCTransfer(sender, NULL, 0, true, receiver, false);
        /** GHOSTUPD: "(True, gs_set_assn cteDeleteOne_'proc (ucast cap_reply_cap))" */
        setThreadState(receiver, ThreadState_Running);
        attemptSwitchTo(receiver);
    } else {
        bool_t restart;

        /** GHOSTUPD: "(True, gs_set_assn cteDeleteOne_'proc (ucast cap_reply_cap))" */
        restart = handleFaultReply(receiver, sender);
        fault_null_fault_ptr_new(&receiver->tcbFault);
        if (restart) {
            setThreadState(receiver, ThreadState_Restart);
            attemptSwitchTo(receiver);
        } else {
            setThreadState(receiver, ThreadState_Inactive);
        }
    }
    finaliseCap(slot->cap, true, true);
    slot->cap = cap_null_cap_new();
}

void
doNormalTransfer(tcb_t *sender, word_t *sendBuffer, endpoint_t *endpoint,
                 word_t badge, bool_t canGrant, tcb_t *receiver,
                 word_t *receiveBuffer, bool_t diminish)
{
    unsigned int msgTransferred;
    message_info_t tag;
    exception_t status;
    extra_caps_t caps;

    tag = messageInfoFromWord(getRegister(sender, msgInfoRegister));

    if (canGrant) {
        status = lookupExtraCaps(sender, sendBuffer, tag);
        caps = current_extra_caps;
        if (unlikely(status != EXCEPTION_NONE)) {
            caps.excaprefs[0] = NULL;
        }
    } else {
        caps = current_extra_caps;
        caps.excaprefs[0] = NULL;
    }

    msgTransferred = copyMRs(sender, sendBuffer, receiver, receiveBuffer,
                             message_info_get_msgLength(tag));

    tag = transferCaps(tag, caps, endpoint, receiver, receiveBuffer, diminish);

    tag = message_info_set_msgLength(tag, msgTransferred);
    setRegister(receiver, msgInfoRegister, wordFromMessageInfo(tag));
    setRegister(receiver, badgeRegister, badge);
}

void
doFaultTransfer(word_t badge, tcb_t *sender, tcb_t *receiver,
                word_t *receiverIPCBuffer)
{
    unsigned int sent;
    message_info_t msgInfo;

    sent = setMRs_fault(sender, receiver, receiverIPCBuffer);
    msgInfo = message_info_new(
                  fault_get_faultType(sender->tcbFault), 0, 0, sent);
    setRegister(receiver, msgInfoRegister, wordFromMessageInfo(msgInfo));
    setRegister(receiver, badgeRegister, badge);
}

/* Like getReceiveSlots, this is specialised for single-cap transfer. */
static message_info_t
transferCaps(message_info_t info, extra_caps_t caps,
             endpoint_t *endpoint, tcb_t *receiver,
             word_t *receiveBuffer, bool_t diminish)
{
    unsigned int i;
    cte_t* destSlot;

    info = message_info_set_msgExtraCaps(info, 0);
    info = message_info_set_msgCapsUnwrapped(info, 0);

    if (likely(!caps.excaprefs[0] || !receiveBuffer)) {
        return info;
    }

    destSlot = getReceiveSlots(receiver, receiveBuffer);

    for (i = 0; i < seL4_MsgMaxExtraCaps && caps.excaprefs[i] != NULL; i++) {
        cte_t *slot = caps.excaprefs[i];
        cap_t cap = slot->cap;

        if (cap_get_capType(cap) == cap_endpoint_cap &&
                EP_PTR(cap_endpoint_cap_get_capEPPtr(cap)) == endpoint) {
            /* If this is a cap to the endpoint on which the message was sent,
             * only transfer the badge, not the cap. */
            setExtraBadge(receiveBuffer,
                          cap_endpoint_cap_get_capEPBadge(cap), i);

            info = message_info_set_msgCapsUnwrapped(info,
                                                     message_info_get_msgCapsUnwrapped(info) | (1 << i));

        } else {
            deriveCap_ret_t dc_ret;

            if (!destSlot) {
                break;
            }

            if (diminish) {
                dc_ret = deriveCap(slot, maskCapRights(noWrite, cap));
            } else {
                dc_ret = deriveCap(slot, cap);
            }

            if (dc_ret.status != EXCEPTION_NONE) {
                break;
            }
            if (cap_get_capType(dc_ret.cap) == cap_null_cap) {
                break;
            }

            cteInsert(dc_ret.cap, slot, destSlot);

            destSlot = NULL;
        }
    }

    return message_info_set_msgExtraCaps(info, i);
}

void doPollFailedTransfer(tcb_t *thread)
{
    /* Set the badge register to 0 to indicate there was no message */
    setRegister(thread, badgeRegister, 0);
}

static void
nextDomain(void)
{
    ksDomScheduleIdx++;
    if (ksDomScheduleIdx >= ksDomScheduleLength) {
        ksDomScheduleIdx = 0;
    }
    ksWorkUnitsCompleted = 0;
    ksCurDomain = ksDomSchedule[ksDomScheduleIdx].domain;
    ksDomainTime = ksDomSchedule[ksDomScheduleIdx].length;
}

void
schedule(void)
{
    word_t action;

    action = (word_t)ksSchedulerAction;
    if (action == (word_t)SchedulerAction_ChooseNewThread) {
        if (isRunnable(ksCurThread)) {
            tcbSchedEnqueue(ksCurThread);
        }
        if (CONFIG_NUM_DOMAINS > 1 && ksDomainTime == 0) {
            nextDomain();
        }
        chooseThread();
        ksSchedulerAction = SchedulerAction_ResumeCurrentThread;
    } else if (action != (word_t)SchedulerAction_ResumeCurrentThread) {
        if (isRunnable(ksCurThread)) {
            tcbSchedEnqueue(ksCurThread);
        }
        /* SwitchToThread */
        switchToThread(ksSchedulerAction);
        ksSchedulerAction = SchedulerAction_ResumeCurrentThread;
    }
}

void
chooseThread(void)
{
    word_t prio;
    word_t dom;
    tcb_t *thread;

    if (CONFIG_NUM_DOMAINS > 1) {
        dom = ksCurDomain;
    } else {
        dom = 0;
    }

    if (likely(ksReadyQueuesL1Bitmap[dom])) {
        uint32_t l1index = (wordBits - 1) - CLZ(ksReadyQueuesL1Bitmap[dom]);
        uint32_t l2index = (wordBits - 1) - CLZ(ksReadyQueuesL2Bitmap[dom][l1index]);
        prio = l1index_to_prio(l1index) | l2index;
        thread = ksReadyQueues[ready_queues_index(dom, prio)].head;
        assert(thread);
        assert(isRunnable(thread));
        switchToThread(thread);
        return;
    }

    switchToIdleThread();

}

void
switchToThread(tcb_t *thread)
{
    Arch_switchToThread(thread);
    tcbSchedDequeue(thread);
    ksCurThread = thread;
}

void
switchToIdleThread(void)
{
    Arch_switchToIdleThread();
    ksCurThread = ksIdleThread;
}

void
setDomain(tcb_t *tptr, dom_t dom)
{
    tcbSchedDequeue(tptr);
    tptr->tcbDomain = dom;
    if (isRunnable(tptr)) {
        tcbSchedEnqueue(tptr);
    }
    if (tptr == ksCurThread) {
        rescheduleRequired();
    }
}

void
setPriority(tcb_t *tptr, prio_t prio)
{
    tcbSchedDequeue(tptr);
    tptr->tcbPriority = prio;
    if (isRunnable(tptr)) {
        tcbSchedEnqueue(tptr);
    }
    if (tptr == ksCurThread) {
        rescheduleRequired();
    }
}

static void
possibleSwitchTo(tcb_t* target, bool_t onSamePriority)
{
    prio_t curPrio, targetPrio;
    tcb_t *action;

    curPrio = ksCurThread->tcbPriority;
    targetPrio = target->tcbPriority;
    action = ksSchedulerAction;

    if (CONFIG_NUM_DOMAINS > 1) {
        dom_t curDom = ksCurDomain;
        dom_t targetDom = target->tcbDomain;

        if (targetDom != curDom) {
            tcbSchedEnqueue(target);
        }
    } else {
        if ((targetPrio > curPrio || (targetPrio == curPrio && onSamePriority))
                && action == SchedulerAction_ResumeCurrentThread) {
            ksSchedulerAction = target;
        } else {
            tcbSchedEnqueue(target);
        }
        if (action != SchedulerAction_ResumeCurrentThread
                && action != SchedulerAction_ChooseNewThread) {
            rescheduleRequired();
        }
    }
}

void
attemptSwitchTo(tcb_t* target)
{
    possibleSwitchTo(target, true);
}

void
switchIfRequiredTo(tcb_t* target)
{
    possibleSwitchTo(target, false);
}

void
setThreadState(tcb_t *tptr, _thread_state_t ts)
{
    thread_state_ptr_set_tsType(&tptr->tcbState, ts);
    scheduleTCB(tptr);
}

void
scheduleTCB(tcb_t *tptr)
{
    if (tptr == ksCurThread &&
            ksSchedulerAction == SchedulerAction_ResumeCurrentThread &&
            !isRunnable(tptr)) {
        rescheduleRequired();
    }
}

void
timerTick(void)
{
    if (likely(isRunnable(ksCurThread))) {
        if (ksCurThread->tcbTimeSlice > 1) {
            ksCurThread->tcbTimeSlice--;
        } else {
            ksCurThread->tcbTimeSlice = CONFIG_TIME_SLICE;
            tcbSchedAppend(ksCurThread);
            rescheduleRequired();
        }
    }

    if (CONFIG_NUM_DOMAINS > 1) {
        ksDomainTime--;
        if (ksDomainTime == 0) {
            rescheduleRequired();
        }
    }
}

void
rescheduleRequired(void)
{
    if (ksSchedulerAction != SchedulerAction_ResumeCurrentThread
            && ksSchedulerAction != SchedulerAction_ChooseNewThread) {
        tcbSchedEnqueue(ksSchedulerAction);
    }
    ksSchedulerAction = SchedulerAction_ChooseNewThread;
}

#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/machine/io.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <stdarg.h>
#include <machine/io.h>

#if defined DEBUG || defined RELEASE_PRINTF

static unsigned int
print_string(const char *s)
{
    unsigned int n;

    for (n = 0; *s; s++, n++) {
        kernel_putchar(*s);
    }

    return n;
}

static unsigned long
xdiv(unsigned long x, unsigned int denom)
{
    switch (denom) {
    case 16:
        return x / 16;
    case 10:
        return x / 10;
    default:
        return 0;
    }
}

static unsigned long
xmod(unsigned long x, unsigned int denom)
{
    switch (denom) {
    case 16:
        return x % 16;
    case 10:
        return x % 10;
    default:
        return 0;
    }
}

unsigned int
print_unsigned_long(unsigned long x, unsigned int ui_base)
{
    char out[11];
    unsigned int i, j;
    unsigned int d;

    /*
     * Only base 10 and 16 supported for now. We want to avoid invoking the
     * compiler's support libraries through doing arbitrary divisions.
     */
    if (ui_base != 10 && ui_base != 16) {
        return 0;
    }

    if (x == 0) {
        kernel_putchar('0');
        return 1;
    }

    for (i = 0; x; x = xdiv(x, ui_base), i++) {
        d = xmod(x, ui_base);

        if (d >= 10) {
            out[i] = 'a' + d - 10;
        } else {
            out[i] = '0' + d;
        }
    }

    for (j = i; j > 0; j--) {
        kernel_putchar(out[j - 1]);
    }

    return i;
}


static unsigned int
print_unsigned_long_long(unsigned long long x, unsigned int ui_base)
{
    unsigned long upper, lower;
    unsigned int n = 0;
    unsigned int mask = 0xF0000000u;

    /* only implemented for hex, decimal is harder without 64 bit division */
    if (ui_base != 16) {
        return 0;
    }

    /* we can't do 64 bit division so break it up into two hex numbers */
    upper = (unsigned long) (x >> 32llu);
    lower = (unsigned long) x;

    /* print first 32 bits if they exist */
    if (upper > 0) {
        n += print_unsigned_long(upper, ui_base);

        /* print leading 0s */
        while (!(mask & lower)) {
            kernel_putchar('0');
            n++;
            mask = mask >> 4;
        }
    }

    /* print last 32 bits */
    n += print_unsigned_long(lower, ui_base);

    return n;
}


static int
vprintf(const char *format, va_list ap)
{
    unsigned int n;
    unsigned int formatting;

    if (!format) {
        return 0;
    }

    n = 0;
    formatting = 0;
    while (*format) {
        if (formatting) {
            switch (*format) {
            case '%':
                kernel_putchar('%');
                n++;
                format++;
                break;

            case 'd': {
                int x = va_arg(ap, int);

                if (x < 0) {
                    kernel_putchar('-');
                    n++;
                    x = -x;
                }

                n += print_unsigned_long((unsigned long)x, 10);
                format++;
                break;
            }

            case 'u':
                n += print_unsigned_long(va_arg(ap, unsigned long), 10);
                format++;
                break;

            case 'x':
                n += print_unsigned_long(va_arg(ap, unsigned long), 16);
                format++;
                break;

            case 'p': {
                unsigned long p = va_arg(ap, unsigned long);
                if (p == 0) {
                    n += print_string("(nil)");
                } else {
                    n += print_string("0x");
                    n += print_unsigned_long(p, 16);
                }
                format++;
                break;
            }

            case 's':
                n += print_string(va_arg(ap, char *));
                format++;
                break;

            case 'l':
                if (*(format + 1) == 'l' && *(format + 2) == 'x') {
                    uint64_t arg = va_arg(ap, unsigned long long);
                    n += print_unsigned_long_long(arg, 16);
                }
                format += 3;
                break;
            default:
                format++;
                break;
            }

            formatting = 0;
        } else {
            switch (*format) {
            case '%':
                formatting = 1;
                format++;
                break;

            default:
                kernel_putchar(*format);
                n++;
                format++;
                break;
            }
        }
    }

    return n;
}

unsigned int puts(const char *s)
{
    for (; *s; s++) {
        kernel_putchar(*s);
    }
    kernel_putchar('\n');
    return 0;
}

unsigned int
kprintf(const char *format, ...)
{
    va_list args;
    unsigned int i;

    va_start(args, format);
    i = vprintf(format, args);
    va_end(args);
    return i;
}

#endif /* defined DEBUG || RELEASE_PRINTF */
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/model/preemption.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <api/failures.h>
#include <model/preemption.h>
#include <model/statedata.h>
#include <plat/machine/hardware.h>
#include <config.h>

/*
 * Possibly preempt the current thread to allow an interrupt to be handled.
 */
exception_t
preemptionPoint(void)
{
    /* Record that we have performed some work. */
    ksWorkUnitsCompleted++;

    /*
     * If we have performed a non-trivial amount of work since last time we
     * checked for preemption, and there is an interrupt pending, handle the
     * interrupt.
     *
     * We avoid checking for pending IRQs every call, as our callers tend to
     * call us in a tight loop and checking for pending IRQs can be quite slow.
     */
    if (ksWorkUnitsCompleted >= CONFIG_MAX_NUM_WORK_UNITS_PER_PREEMPTION) {
        ksWorkUnitsCompleted = 0;
        if (isIRQPending()) {
            return EXCEPTION_PREEMPTED;
        }
    }

    return EXCEPTION_NONE;
}

#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/model/statedata.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <types.h>
#include <plat/machine.h>
#include <model/statedata.h>
#include <object/structures.h>
#include <object/tcb.h>

/* Pointer to the head of the scheduler queue for each priority */
tcb_queue_t ksReadyQueues[NUM_READY_QUEUES];
word_t ksReadyQueuesL1Bitmap[CONFIG_NUM_DOMAINS];
word_t ksReadyQueuesL2Bitmap[CONFIG_NUM_DOMAINS][(CONFIG_NUM_PRIORITIES / wordBits) + 1];
compile_assert(ksReadyQueuesL1BitmapBigEnough, (CONFIG_NUM_PRIORITIES / wordBits) <= wordBits);

/* Current thread TCB pointer */
tcb_t *ksCurThread;

/* Idle thread TCB pointer */
tcb_t *ksIdleThread;

/* Values of 0 and ~0 encode ResumeCurrentThread and ChooseNewThread
 * respectively; other values encode SwitchToThread and must be valid
 * tcb pointers */
tcb_t *ksSchedulerAction;

/* Units of work we have completed since the last time we checked for
 * pending interrupts */
word_t ksWorkUnitsCompleted;

/* Root of the cap derivation tree structure */
cte_t *ksRootCTE;

/* CNode containing interrupt handler endpoints */
irq_state_t intStateIRQTable[maxIRQ + 1];
cte_t *intStateIRQNode;

/* Currently active domain */
dom_t ksCurDomain;

/* Domain timeslice remaining */
word_t ksDomainTime;

/* An index into ksDomSchedule for active domain and length. */
uint32_t ksDomScheduleIdx;

#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/object/asyncendpoint.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <assert.h>

#include <types.h>
#include <kernel/thread.h>
#include <object/structures.h>
#include <object/tcb.h>
#include <object/endpoint.h>
#include <model/statedata.h>
#include <machine/io.h>

#include <object/asyncendpoint.h>

static inline tcb_queue_t PURE
aep_ptr_get_queue(async_endpoint_t *aepptr)
{
    tcb_queue_t aep_queue;

    aep_queue.head = (tcb_t*)async_endpoint_ptr_get_aepQueue_head(aepptr);
    aep_queue.end = (tcb_t*)async_endpoint_ptr_get_aepQueue_tail(aepptr);

    return aep_queue;
}

static inline void
aep_ptr_set_queue(async_endpoint_t *aepptr, tcb_queue_t aep_queue)
{
    async_endpoint_ptr_set_aepQueue_head(aepptr, (word_t)aep_queue.head);
    async_endpoint_ptr_set_aepQueue_tail(aepptr, (word_t)aep_queue.end);
}

static inline void
aep_set_active(async_endpoint_t *aepptr, word_t badge)
{
    async_endpoint_ptr_set_state(aepptr, AEPState_Active);
    async_endpoint_ptr_set_aepMsgIdentifier(aepptr, badge);
}


void
sendAsyncIPC(async_endpoint_t *aepptr, word_t badge)
{
    switch (async_endpoint_ptr_get_state(aepptr)) {
    case AEPState_Idle: {
        tcb_t *tcb = (tcb_t*)async_endpoint_ptr_get_aepBoundTCB(aepptr);
        /* Check if we are bound and that thread is waiting for a message */
        if (tcb) {
            if (thread_state_ptr_get_tsType(&tcb->tcbState) == ThreadState_BlockedOnReceive) {
                /* Send and start thread running */
                ipcCancel(tcb);
                setThreadState(tcb, ThreadState_Running);
                setRegister(tcb, badgeRegister, badge);
                attemptSwitchTo(tcb);
            } else if (thread_state_ptr_get_tsType(&tcb->tcbState) == ThreadState_RunningVM) {
                setThreadState(tcb, ThreadState_Running);
                setRegister(tcb, badgeRegister, badge);
                setRegister(tcb, msgInfoRegister, 0);
                Arch_leaveVMAsyncTransfer(tcb);
                attemptSwitchTo(tcb);
            } else {
                aep_set_active(aepptr, badge);
            }
        } else {
            aep_set_active(aepptr, badge);
        }
        break;
    }
    case AEPState_Waiting: {
        tcb_queue_t aep_queue;
        tcb_t *dest;

        aep_queue = aep_ptr_get_queue(aepptr);
        dest = aep_queue.head;

        /* Haskell error "WaitingAEP AEP must have non-empty queue" */
        assert(dest);

        /* Dequeue TCB */
        aep_queue = tcbEPDequeue(dest, aep_queue);
        aep_ptr_set_queue(aepptr, aep_queue);

        /* set the thread state to idle if the queue is empty */
        if (!aep_queue.head) {
            async_endpoint_ptr_set_state(aepptr, AEPState_Idle);
        }

        setThreadState(dest, ThreadState_Running);
        setRegister(dest, badgeRegister, badge);
        switchIfRequiredTo(dest);
        break;
    }

    case AEPState_Active: {
        word_t badge2;

        badge2 = async_endpoint_ptr_get_aepMsgIdentifier(aepptr);
        badge2 |= badge;

        async_endpoint_ptr_set_aepMsgIdentifier(aepptr, badge2);
        break;
    }
    }
}

void
receiveAsyncIPC(tcb_t *thread, cap_t cap, bool_t isBlocking)
{
    async_endpoint_t *aepptr;

    aepptr = AEP_PTR(cap_async_endpoint_cap_get_capAEPPtr(cap));

    switch (async_endpoint_ptr_get_state(aepptr)) {
    case AEPState_Idle:
        /* Fall through */
    case AEPState_Waiting: {
        tcb_queue_t aep_queue;

        if (isBlocking) {
            /* Block thread on endpoint */
            thread_state_ptr_set_tsType(&thread->tcbState,
                                        ThreadState_BlockedOnAsyncEvent);
            thread_state_ptr_set_blockingIPCEndpoint(&thread->tcbState,
                                                     AEP_REF(aepptr));
            scheduleTCB(thread);

            /* Enqueue TCB */
            aep_queue = aep_ptr_get_queue(aepptr);
            aep_queue = tcbEPAppend(thread, aep_queue);

            async_endpoint_ptr_set_state(aepptr, AEPState_Waiting);
            aep_ptr_set_queue(aepptr, aep_queue);
        } else {
            doPollFailedTransfer(thread);
        }
        break;
    }

    case AEPState_Active:
        setRegister(
            thread, badgeRegister,
            async_endpoint_ptr_get_aepMsgIdentifier(aepptr));
        async_endpoint_ptr_set_state(aepptr, AEPState_Idle);
        break;
    }
}

void
aepCancelAll(async_endpoint_t *aepptr)
{
    if (async_endpoint_ptr_get_state(aepptr) == AEPState_Waiting) {
        tcb_t *thread = TCB_PTR(async_endpoint_ptr_get_aepQueue_head(aepptr));

        async_endpoint_ptr_set_state(aepptr, AEPState_Idle);
        async_endpoint_ptr_set_aepQueue_head(aepptr, 0);
        async_endpoint_ptr_set_aepQueue_tail(aepptr, 0);

        /* Set all waiting threads to Restart */
        for (; thread; thread = thread->tcbEPNext) {
            setThreadState(thread, ThreadState_Restart);
            tcbSchedEnqueue(thread);
        }
        rescheduleRequired();
    }
}

void
asyncIPCCancel(tcb_t *threadPtr, async_endpoint_t *aepptr)
{
    tcb_queue_t aep_queue;

    /* Haskell error "asyncIPCCancel: async endpoint must be waiting" */
    assert(async_endpoint_ptr_get_state(aepptr) == AEPState_Waiting);

    /* Dequeue TCB */
    aep_queue = aep_ptr_get_queue(aepptr);
    aep_queue = tcbEPDequeue(threadPtr, aep_queue);
    aep_ptr_set_queue(aepptr, aep_queue);

    /* Make endpoint idle */
    if (!aep_queue.head) {
        async_endpoint_ptr_set_state(aepptr, AEPState_Idle);
    }

    /* Make thread inactive */
    setThreadState(threadPtr, ThreadState_Inactive);
}

void
completeAsyncIPC(async_endpoint_t *aepptr, tcb_t *tcb)
{
    word_t badge;

    if (likely(tcb && async_endpoint_ptr_get_state(aepptr) == AEPState_Active)) {
        async_endpoint_ptr_set_state(aepptr, AEPState_Idle);
        badge = async_endpoint_ptr_get_aepMsgIdentifier(aepptr);
        setRegister(tcb, badgeRegister, badge);
    } else {
        fail("tried to complete async ipc with inactive AEP");
    }
}

void
unbindAsyncEndpoint(tcb_t *tcb)
{
    async_endpoint_t *aepptr;
    aepptr = tcb->boundAsyncEndpoint;

    if (aepptr) {
        async_endpoint_ptr_set_aepBoundTCB(aepptr, (word_t) 0);
        tcb->boundAsyncEndpoint = NULL;
    }
}

void
bindAsyncEndpoint(tcb_t *tcb, async_endpoint_t *aepptr)
{
    async_endpoint_ptr_set_aepBoundTCB(aepptr, (word_t)tcb);
    tcb->boundAsyncEndpoint = aepptr;
}


#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/object/cnode.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <assert.h>
#include <types.h>
#include <api/failures.h>
#include <api/invocation.h>
#include <api/syscall.h>
#include <api/types.h>
#include <machine/io.h>
#include <object/structures.h>
#include <object/objecttype.h>
#include <object/cnode.h>
#include <object/interrupt.h>
#include <object/untyped.h>
#include <kernel/cspace.h>
#include <kernel/thread.h>
#include <kernel/cdt.h>
#include <model/preemption.h>
#include <model/statedata.h>
#include <util.h>

struct finaliseSlot_ret {
    exception_t status;
    bool_t success;
    irq_t irq;
};
typedef struct finaliseSlot_ret finaliseSlot_ret_t;

static finaliseSlot_ret_t finaliseSlot(cte_t *slot, bool_t exposed);
static void emptySlot(cte_t *slot, irq_t irq);
static exception_t reduceZombie(cte_t* slot, bool_t exposed);

exception_t
decodeCNodeInvocation(word_t label, unsigned int length, cap_t cap,
                      extra_caps_t extraCaps, word_t *buffer)
{
    lookupSlot_ret_t lu_ret;
    cte_t *destSlot;
    word_t index, w_bits;
    exception_t status;

    /* Haskell error: "decodeCNodeInvocation: invalid cap" */
    assert(cap_get_capType(cap) == cap_cnode_cap);

    if (label < CNodeRevoke || label > CNodeSaveCaller) {
        userError("CNodeCap: Illegal Operation attempted.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (length < 2) {
        userError("CNode operation: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }
    index = getSyscallArg(0, buffer);
    w_bits = getSyscallArg(1, buffer);

    lu_ret = lookupTargetSlot(cap, index, w_bits);
    if (lu_ret.status != EXCEPTION_NONE) {
        userError("CNode operation: Target slot invalid.");
        return lu_ret.status;
    }
    destSlot = lu_ret.slot;

    if (label >= CNodeCopy && label <= CNodeMutate) {
        cte_t *srcSlot;
        word_t srcIndex, srcDepth, capData;
        bool_t isMove;
        cap_rights_t cap_rights;
        cap_t srcRoot, newCap;
        deriveCap_ret_t dc_ret;
        cap_t srcCap;

        if (length < 4 || extraCaps.excaprefs[0] == NULL) {
            userError("CNode Copy/Mint/Move/Mutate: Truncated message.");
            current_syscall_error.type = seL4_TruncatedMessage;
            return EXCEPTION_SYSCALL_ERROR;
        }
        srcIndex = getSyscallArg(2, buffer);
        srcDepth = getSyscallArg(3, buffer);

        srcRoot = extraCaps.excaprefs[0]->cap;

        status = ensureEmptySlot(destSlot);
        if (status != EXCEPTION_NONE) {
            userError("CNode Copy/Mint/Move/Mutate: Destination not empty.");
            return status;
        }

        lu_ret = lookupSourceSlot(srcRoot, srcIndex, srcDepth);
        if (lu_ret.status != EXCEPTION_NONE) {
            userError("CNode Copy/Mint/Move/Mutate: Invalid source slot.");
            return lu_ret.status;
        }
        srcSlot = lu_ret.slot;

        if (cap_get_capType(srcSlot->cap) == cap_null_cap) {
            userError("CNode Copy/Mint/Move/Mutate: Source slot invalid or empty.");
            current_syscall_error.type = seL4_FailedLookup;
            current_syscall_error.failedLookupWasSource = 1;
            current_lookup_fault =
                lookup_fault_missing_capability_new(srcDepth);
            return EXCEPTION_SYSCALL_ERROR;
        }

        switch (label) {
        case CNodeCopy:

            if (length < 5) {
                userError("Truncated message for CNode Copy operation.");
                current_syscall_error.type = seL4_TruncatedMessage;
                return EXCEPTION_SYSCALL_ERROR;
            }

            cap_rights = rightsFromWord(getSyscallArg(4, buffer));
            srcCap = maskCapRights(cap_rights, srcSlot->cap);
            dc_ret = deriveCap(srcSlot, srcCap);
            if (dc_ret.status != EXCEPTION_NONE) {
                userError("Error deriving cap for CNode Copy operation.");
                return dc_ret.status;
            }
            newCap = dc_ret.cap;
            isMove = false;

            break;

        case CNodeMint:
            if (length < 6) {
                userError("CNode Mint: Truncated message.");
                current_syscall_error.type = seL4_TruncatedMessage;
                return EXCEPTION_SYSCALL_ERROR;
            }

            cap_rights = rightsFromWord(getSyscallArg(4, buffer));
            capData = getSyscallArg(5, buffer);
            srcCap = maskCapRights(cap_rights, srcSlot->cap);
            dc_ret = deriveCap(srcSlot,
                               updateCapData(false, capData, srcCap));
            if (dc_ret.status != EXCEPTION_NONE) {
                userError("Error deriving cap for CNode Mint operation.");
                return dc_ret.status;
            }
            newCap = dc_ret.cap;
            isMove = false;

            break;

        case CNodeMove:
            newCap = srcSlot->cap;
            isMove = true;

            break;

        case CNodeMutate:
            if (length < 5) {
                userError("CNode Mutate: Truncated message.");
                current_syscall_error.type = seL4_TruncatedMessage;
                return EXCEPTION_SYSCALL_ERROR;
            }

            capData = getSyscallArg(4, buffer);
            newCap = updateCapData(true, capData, srcSlot->cap);
            isMove = true;

            break;

        default:
            assert (0);
            return EXCEPTION_NONE;
        }

        if (cap_get_capType(newCap) == cap_null_cap) {
            userError("CNode Copy/Mint/Move/Mutate: Mutated cap would be invalid.");
            current_syscall_error.type = seL4_IllegalOperation;
            return EXCEPTION_SYSCALL_ERROR;
        }

        setThreadState(ksCurThread, ThreadState_Restart);
        if (isMove) {
            return invokeCNodeMove(newCap, srcSlot, destSlot);
        } else {
            return invokeCNodeInsert(newCap, srcSlot, destSlot);
        }
    }

    if (label == CNodeRevoke) {
        setThreadState(ksCurThread, ThreadState_Restart);
        return invokeCNodeRevoke(destSlot);
    }

    if (label == CNodeDelete) {
        setThreadState(ksCurThread, ThreadState_Restart);
        return invokeCNodeDelete(destSlot);
    }

    if (label == CNodeSaveCaller) {
        status = ensureEmptySlot(destSlot);
        if (status != EXCEPTION_NONE) {
            userError("CNode SaveCaller: Destination slot not empty.");
            return status;
        }

        setThreadState(ksCurThread, ThreadState_Restart);
        return invokeCNodeSaveCaller(destSlot);
    }

    if (label == CNodeRecycle) {
        if (!hasRecycleRights(destSlot->cap)) {
            userError("CNode Recycle: Target cap invalid.");
            current_syscall_error.type = seL4_IllegalOperation;
            return EXCEPTION_SYSCALL_ERROR;
        }
        setThreadState(ksCurThread, ThreadState_Restart);
        return invokeCNodeRecycle(destSlot);
    }

    if (label == CNodeRotate) {
        word_t pivotNewData, pivotIndex, pivotDepth;
        word_t srcNewData, srcIndex, srcDepth;
        cte_t *pivotSlot, *srcSlot;
        cap_t pivotRoot, srcRoot, newSrcCap, newPivotCap;

        if (length < 8 || extraCaps.excaprefs[0] == NULL
                || extraCaps.excaprefs[1] == NULL) {
            current_syscall_error.type = seL4_TruncatedMessage;
            return EXCEPTION_SYSCALL_ERROR;
        }
        pivotNewData = getSyscallArg(2, buffer);
        pivotIndex   = getSyscallArg(3, buffer);
        pivotDepth   = getSyscallArg(4, buffer);
        srcNewData   = getSyscallArg(5, buffer);
        srcIndex     = getSyscallArg(6, buffer);
        srcDepth     = getSyscallArg(7, buffer);

        pivotRoot = extraCaps.excaprefs[0]->cap;
        srcRoot   = extraCaps.excaprefs[1]->cap;

        lu_ret = lookupSourceSlot(srcRoot, srcIndex, srcDepth);
        if (lu_ret.status != EXCEPTION_NONE) {
            return lu_ret.status;
        }
        srcSlot = lu_ret.slot;

        lu_ret = lookupPivotSlot(pivotRoot, pivotIndex, pivotDepth);
        if (lu_ret.status != EXCEPTION_NONE) {
            return lu_ret.status;
        }
        pivotSlot = lu_ret.slot;

        if (pivotSlot == srcSlot || pivotSlot == destSlot) {
            userError("CNode Rotate: Pivot slot the same as source or dest slot.");
            current_syscall_error.type = seL4_IllegalOperation;
            return EXCEPTION_SYSCALL_ERROR;
        }

        if (srcSlot != destSlot) {
            status = ensureEmptySlot(destSlot);
            if (status != EXCEPTION_NONE) {
                return status;
            }
        }

        if (cap_get_capType(srcSlot->cap) == cap_null_cap) {
            current_syscall_error.type = seL4_FailedLookup;
            current_syscall_error.failedLookupWasSource = 1;
            current_lookup_fault = lookup_fault_missing_capability_new(srcDepth);
            return EXCEPTION_SYSCALL_ERROR;
        }

        if (cap_get_capType(pivotSlot->cap) == cap_null_cap) {
            current_syscall_error.type = seL4_FailedLookup;
            current_syscall_error.failedLookupWasSource = 0;
            current_lookup_fault = lookup_fault_missing_capability_new(pivotDepth);
            return EXCEPTION_SYSCALL_ERROR;
        }

        newSrcCap = updateCapData(true, srcNewData, srcSlot->cap);
        newPivotCap = updateCapData(true, pivotNewData, pivotSlot->cap);

        if (cap_get_capType(newSrcCap) == cap_null_cap) {
            userError("CNode Rotate: Source cap invalid.");
            current_syscall_error.type = seL4_IllegalOperation;
            return EXCEPTION_SYSCALL_ERROR;
        }

        if (cap_get_capType(newPivotCap) == cap_null_cap) {
            userError("CNode Rotate: Pivot cap invalid.");
            current_syscall_error.type = seL4_IllegalOperation;
            return EXCEPTION_SYSCALL_ERROR;
        }

        setThreadState(ksCurThread, ThreadState_Restart);
        return invokeCNodeRotate(newSrcCap, newPivotCap,
                                 srcSlot, pivotSlot, destSlot);
    }

    return EXCEPTION_NONE;
}

exception_t
invokeCNodeRevoke(cte_t *destSlot)
{
    return cteRevoke(destSlot);
}

exception_t
invokeCNodeDelete(cte_t *destSlot)
{
    return cteDelete(destSlot, true);
}

exception_t
invokeCNodeRecycle(cte_t *destSlot)
{
    return cteRecycle(destSlot);
}

exception_t
invokeCNodeInsert(cap_t cap, cte_t *srcSlot, cte_t *destSlot)
{
    cteInsert(cap, srcSlot, destSlot);

    return EXCEPTION_NONE;
}

exception_t
invokeCNodeMove(cap_t cap, cte_t *srcSlot, cte_t *destSlot)
{
    cteMove(cap, srcSlot, destSlot);

    return EXCEPTION_NONE;
}

exception_t
invokeCNodeRotate(cap_t cap1, cap_t cap2, cte_t *slot1,
                  cte_t *slot2, cte_t *slot3)
{
    if (slot1 == slot3) {
        cdtSwap(cap1, slot1, cap2, slot2);
    } else {
        cteMove(cap2, slot2, slot3);
        cteMove(cap1, slot1, slot2);
    }

    return EXCEPTION_NONE;
}

exception_t
invokeCNodeSaveCaller(cte_t *destSlot)
{
    cap_t cap;
    cte_t *srcSlot;

    srcSlot = TCB_PTR_CTE_PTR(ksCurThread, tcbCaller);
    cap = srcSlot->cap;

    switch (cap_get_capType(cap)) {
    case cap_null_cap:
        userError("CNode SaveCaller: Reply cap not present.");
        break;

    case cap_reply_cap:
        if (!cap_reply_cap_get_capReplyMaster(cap)) {
            cteMove(cap, srcSlot, destSlot);
        }
        break;

    default:
        fail("caller capability must be null or reply");
        break;
    }

    return EXCEPTION_NONE;
}

void
cteInsert(cap_t newCap, cte_t *srcSlot, cte_t *destSlot)
{
    /* Haskell error: "cteInsert to non-empty destination" */
    assert(cap_get_capType(destSlot->cap) == cap_null_cap);

    destSlot->cap = newCap;
    cdtInsert(srcSlot, destSlot);
}

void
cteMove(cap_t newCap, cte_t *srcSlot, cte_t *destSlot)
{
    /* Haskell error: "cteMove to non-empty destination" */
    assert(cap_get_capType(destSlot->cap) == cap_null_cap);

    destSlot->cap = newCap;
    if (cap_get_capType(newCap) == cap_reply_cap) {
        tcb_t *replyTCB = TCB_PTR(cap_reply_cap_get_capTCBPtr(newCap));
        cte_t *replySlot = TCB_PTR_CTE_PTR(replyTCB, tcbReply);
        cap_reply_cap_ptr_set_capCallerSlot(&replySlot->cap, CTE_REF(destSlot));
    } else {
        cdtMove(srcSlot, destSlot);
    }
    srcSlot->cap = cap_null_cap_new();
}

void
capSwapForDelete(cte_t *slot1, cte_t *slot2)
{
    cap_t cap1, cap2;

    if (slot1 == slot2) {
        return;
    }

    cap1 = slot1->cap;
    cap2 = slot2->cap;

    cdtSwap(cap1, slot1, cap2, slot2);
}

exception_t
cteRevoke(cte_t *slot)
{
    cte_t *childPtr;
    exception_t status;

    if (cap_get_capType(slot->cap) == cap_null_cap) {
        return EXCEPTION_NONE;
    }
    for (childPtr = cdtFindChild(slot); childPtr; childPtr = cdtFindChild(slot)) {
        status = cteDelete(childPtr, true);
        if (status != EXCEPTION_NONE) {
            return status;
        }

        status = preemptionPoint();
        if (status != EXCEPTION_NONE) {
            return status;
        }
    }

    return EXCEPTION_NONE;
}

exception_t
cteDelete(cte_t *slot, bool_t exposed)
{
    finaliseSlot_ret_t fs_ret;

    fs_ret = finaliseSlot(slot, exposed);
    if (fs_ret.status != EXCEPTION_NONE) {
        return fs_ret.status;
    }

    if (exposed || fs_ret.success) {
        emptySlot(slot, fs_ret.irq);
    }
    return EXCEPTION_NONE;
}

static void
emptySlot(cte_t *slot, irq_t irq)
{
    if (cap_get_capType(slot->cap) != cap_null_cap) {
        cdtRemove(slot);
        slot->cap = cap_null_cap_new();

        if (irq != irqInvalid) {
            deletedIRQHandler(irq);
        }
    }
}

static inline bool_t CONST
capRemovable(cap_t cap, cte_t* slot)
{
    switch (cap_get_capType(cap)) {
    case cap_null_cap:
        return true;
    case cap_zombie_cap: {
        word_t n = cap_zombie_cap_get_capZombieNumber(cap);
        cte_t* z_slot = (cte_t*)cap_zombie_cap_get_capZombiePtr(cap);
        return (n == 0 || (n == 1 && slot == z_slot));
    }
    default:
        fail("finaliseCap should only return Zombie or NullCap");
    }
}

static inline bool_t CONST
capCyclicZombie(cap_t cap, cte_t *slot)
{
    return cap_get_capType(cap) == cap_zombie_cap &&
           CTE_PTR(cap_zombie_cap_get_capZombiePtr(cap)) == slot;
}

static finaliseSlot_ret_t
finaliseSlot(cte_t *slot, bool_t immediate)
{
    bool_t final;
    finaliseCap_ret_t fc_ret;
    exception_t status;
    finaliseSlot_ret_t ret;

    while (cap_get_capType(slot->cap) != cap_null_cap) {
        /* If we have a zombie cap then we know it is final and can
         * avoid an expensive cdtIsFinal check */
        final = (cap_get_capType(slot->cap) == cap_zombie_cap) || cdtIsFinal(slot);
        fc_ret = finaliseCap(slot->cap, final, false);

        if (capRemovable(fc_ret.remainder, slot)) {
            ret.status = EXCEPTION_NONE;
            ret.success = true;
            ret.irq = fc_ret.irq;
            return ret;
        }

        /* if we have a zombie then we actually don't need to call
         * cdtUpdate as the cap actually hasn't changed */
        if (cap_get_capType(slot->cap) != cap_zombie_cap) {
            cdtUpdate(slot, fc_ret.remainder);
        }

        if (!immediate && capCyclicZombie(fc_ret.remainder, slot)) {
            ret.status = EXCEPTION_NONE;
            ret.success = false;
            ret.irq = fc_ret.irq;
            return ret;
        }

        status = reduceZombie(slot, immediate);
        if (status != EXCEPTION_NONE) {
            ret.status = status;
            ret.success = false;
            ret.irq = irqInvalid;
            return ret;
        }

        status = preemptionPoint();
        if (status != EXCEPTION_NONE) {
            ret.status = status;
            ret.success = false;
            ret.irq = irqInvalid;
            return ret;
        }
    }
    ret.status = EXCEPTION_NONE;
    ret.success = true;
    ret.irq = irqInvalid;
    return ret;
}

static exception_t
reduceZombie(cte_t* slot, bool_t immediate)
{
    cte_t* ptr;
    word_t n, type;
    exception_t status;

    assert(cap_get_capType(slot->cap) == cap_zombie_cap);
    ptr = (cte_t*)cap_zombie_cap_get_capZombiePtr(slot->cap);
    n = cap_zombie_cap_get_capZombieNumber(slot->cap);
    type = cap_zombie_cap_get_capZombieType(slot->cap);

    /* Haskell error: "reduceZombie: expected unremovable zombie" */
    assert(n > 0);

    if (immediate) {
        cte_t* endSlot = &ptr[n - 1];

        status = cteDelete(endSlot, false);
        if (status != EXCEPTION_NONE) {
            return status;
        }

        switch (cap_get_capType(slot->cap)) {
        case cap_null_cap:
            break;

        case cap_zombie_cap: {
            cte_t* ptr2 =
                (cte_t*)cap_zombie_cap_get_capZombiePtr(slot->cap);

            if (ptr == ptr2 &&
                    cap_zombie_cap_get_capZombieNumber(slot->cap) == n &&
                    cap_zombie_cap_get_capZombieType(slot->cap) == type) {
                assert(cap_get_capType(endSlot->cap) == cap_null_cap);
                /* We could call cdtUpdate here, but we know it is not necessary
                 * because a zombie is not ordered in the aaTree by its zombieNumber
                 * and so cdtUpdate will always be a noop. Skipping the call to cdtUpdate
                 * here is to make revoking large cnodes faster as this gets called
                 * for every slot in the cnode */
                slot->cap =  cap_zombie_cap_set_capZombieNumber(slot->cap, n - 1);
            } else {
                /* Haskell error:
                 * "Expected new Zombie to be self-referential."
                 */
                assert(ptr2 == slot && ptr != slot);
            }
            break;
        }

        default:
            fail("Expected recursion to result in Zombie.");
        }
    } else {
        /* Haskell error: "Cyclic zombie passed to unexposed reduceZombie" */
        assert(ptr != slot);

        if (cap_get_capType(ptr->cap) == cap_zombie_cap) {
            /* Haskell error: "Moving self-referential Zombie aside." */
            assert(ptr != CTE_PTR(cap_zombie_cap_get_capZombiePtr(ptr->cap)));
        }

        capSwapForDelete(ptr, slot);
    }
    return EXCEPTION_NONE;
}

void
cteDeleteOne(cte_t* slot)
{
    uint32_t cap_type = cap_get_capType(slot->cap);
    if (cap_type != cap_null_cap) {
        bool_t final;
        finaliseCap_ret_t fc_ret UNUSED;
        final = cdtIsFinal(slot);
        /** GHOSTUPD: "(gs_get_assn cteDeleteOne_'proc \<acute>ghost'state = (-1)
            \<or> gs_get_assn cteDeleteOne_'proc \<acute>ghost'state = \<acute>cap_type, id)" */
        fc_ret = finaliseCap(slot->cap, final, true);
        /* Haskell error: "cteDeleteOne: cap should be removable" */
        assert(capRemovable(fc_ret.remainder, slot) &&
               fc_ret.irq == irqInvalid);
        emptySlot(slot, irqInvalid);
    }
}

exception_t
cteRecycle(cte_t* slot)
{
    exception_t status;
    finaliseSlot_ret_t fc_ret;

    status = cteRevoke(slot);
    if (status != EXCEPTION_NONE) {
        return status;
    }

    fc_ret = finaliseSlot(slot, true);
    if (fc_ret.status != EXCEPTION_NONE) {
        return fc_ret.status;
    }

    if (cap_get_capType(slot->cap) != cap_null_cap) {
        cap_t new_cap;
        bool_t is_final;
        is_final = cdtIsFinal(slot);
        new_cap = recycleCap(is_final, slot->cap);
        cdtUpdate(slot, new_cap);
    }

    return EXCEPTION_NONE;
}

void
insertNewCap(cte_t *parent, cte_t *slot, cap_t cap)
{
    slot->cap = cap;
    cdtInsert(parent, slot);
}

void
setupReplyMaster(tcb_t *thread)
{
    cte_t *slot;

    slot = TCB_PTR_CTE_PTR(thread, tcbReply);
    if (cap_get_capType(slot->cap) == cap_null_cap) {
        /* Haskell asserts that no reply caps exist for this thread here. This
         * cannot be translated. */
        slot->cap = cap_reply_cap_new(CTE_REF(NULL), true, TCB_REF(NULL));
    }
}

exception_t
ensureEmptySlot(cte_t *slot)
{
    if (cap_get_capType(slot->cap) != cap_null_cap) {
        current_syscall_error.type = seL4_DeleteFirst;
        return EXCEPTION_SYSCALL_ERROR;
    }

    return EXCEPTION_NONE;
}

bool_t PURE
slotCapLongRunningDelete(cte_t *slot)
{
    if (cap_get_capType(slot->cap) == cap_null_cap) {
        return false;
    } else if (! cdtIsFinal(slot)) {
        return false;
    }
    switch (cap_get_capType(slot->cap)) {
    case cap_thread_cap:
    case cap_zombie_cap:
    case cap_cnode_cap:
        return true;
    default:
        return false;
    }
}

/* This implementation is specialised to the (current) limit
 * of one cap receive slot. */
cte_t *
getReceiveSlots(tcb_t *thread, word_t *buffer)
{
    cap_transfer_t ct;
    cptr_t cptr;
    lookupCap_ret_t luc_ret;
    lookupSlot_ret_t lus_ret;
    cte_t *slot;
    cap_t cnode;

    if (!buffer) {
        return NULL;
    }

    ct = loadCapTransfer(buffer);
    cptr = ct.ctReceiveRoot;

    luc_ret = lookupCap(thread, cptr);
    if (luc_ret.status != EXCEPTION_NONE) {
        return NULL;
    }
    cnode = luc_ret.cap;

    lus_ret = lookupTargetSlot(cnode, ct.ctReceiveIndex, ct.ctReceiveDepth);
    if (lus_ret.status != EXCEPTION_NONE) {
        return NULL;
    }
    slot = lus_ret.slot;

    if (cap_get_capType(slot->cap) != cap_null_cap) {
        return NULL;
    }

    return slot;
}

cap_transfer_t PURE
loadCapTransfer(word_t *buffer)
{
    const int offset = seL4_MsgMaxLength + seL4_MsgMaxExtraCaps + 2;
    return capTransferFromWords(buffer + offset);
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/object/endpoint.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <types.h>
#include <kernel/thread.h>
#include <kernel/vspace.h>
#include <machine/registerset.h>
#include <model/statedata.h>
#include <object/asyncendpoint.h>
#include <object/cnode.h>
#include <object/endpoint.h>
#include <object/tcb.h>

static inline tcb_queue_t PURE
ep_ptr_get_queue(endpoint_t *epptr)
{
    tcb_queue_t queue;

    queue.head = (tcb_t*)endpoint_ptr_get_epQueue_head(epptr);
    queue.end = (tcb_t*)endpoint_ptr_get_epQueue_tail(epptr);

    return queue;
}

static inline void
ep_ptr_set_queue(endpoint_t *epptr, tcb_queue_t queue)
{
    endpoint_ptr_set_epQueue_head(epptr, (word_t)queue.head);
    endpoint_ptr_set_epQueue_tail(epptr, (word_t)queue.end);
}

void
sendIPC(bool_t blocking, bool_t do_call, word_t badge,
        bool_t canGrant, tcb_t *thread, endpoint_t *epptr)
{
    switch (endpoint_ptr_get_state(epptr)) {
    case EPState_Idle:
    case EPState_Send:
        if (blocking) {
            tcb_queue_t queue;

            /* Set thread state to BlockedOnSend */
            thread_state_ptr_set_tsType(&thread->tcbState,
                                        ThreadState_BlockedOnSend);
            thread_state_ptr_set_blockingIPCEndpoint(
                &thread->tcbState, EP_REF(epptr));
            thread_state_ptr_set_blockingIPCBadge(
                &thread->tcbState, badge);
            thread_state_ptr_set_blockingIPCCanGrant(
                &thread->tcbState, canGrant);
            thread_state_ptr_set_blockingIPCIsCall(
                &thread->tcbState, do_call);

            scheduleTCB(thread);

            /* Place calling thread in endpoint queue */
            queue = ep_ptr_get_queue(epptr);
            queue = tcbEPAppend(thread, queue);
            endpoint_ptr_set_state(epptr, EPState_Send);
            ep_ptr_set_queue(epptr, queue);
        }
        break;

    case EPState_Recv: {
        tcb_queue_t queue;
        tcb_t *dest;
        bool_t diminish;

        /* Get the head of the endpoint queue. */
        queue = ep_ptr_get_queue(epptr);
        dest = queue.head;

        /* Haskell error "Receive endpoint queue must not be empty" */
        assert(dest);

        /* Dequeue the first TCB */
        queue = tcbEPDequeue(dest, queue);
        ep_ptr_set_queue(epptr, queue);

        if (!queue.head) {
            endpoint_ptr_set_state(epptr, EPState_Idle);
        }

        /* Do the transfer */
        diminish =
            thread_state_get_blockingIPCDiminishCaps(dest->tcbState);
        doIPCTransfer(thread, epptr, badge, canGrant, dest, diminish);

        setThreadState(dest, ThreadState_Running);
        attemptSwitchTo(dest);

        if (do_call ||
                fault_ptr_get_faultType(&thread->tcbFault) != fault_null_fault) {
            if (canGrant && !diminish) {
                setupCallerCap(thread, dest);
            } else {
                setThreadState(thread, ThreadState_Inactive);
            }
        }

        break;
    }
    }
}

void
receiveIPC(tcb_t *thread, cap_t cap)
{
    endpoint_t *epptr;
    bool_t diminish;
    async_endpoint_t *aepptr;

    /* Haskell error "receiveIPC: invalid cap" */
    assert(cap_get_capType(cap) == cap_endpoint_cap);

    epptr = EP_PTR(cap_endpoint_cap_get_capEPPtr(cap));
    diminish = !cap_endpoint_cap_get_capCanSend(cap);

    /* Check for anything waiting in the async endpoint*/
    aepptr = thread->boundAsyncEndpoint;
    if (aepptr && async_endpoint_ptr_get_state(aepptr) == AEPState_Active) {
        completeAsyncIPC(aepptr, thread);
    } else {
        switch (endpoint_ptr_get_state(epptr)) {
        case EPState_Idle:
        case EPState_Recv: {
            tcb_queue_t queue;

            /* Set thread state to BlockedOnReceive */
            thread_state_ptr_set_tsType(&thread->tcbState,
                                        ThreadState_BlockedOnReceive);
            thread_state_ptr_set_blockingIPCEndpoint(
                &thread->tcbState, EP_REF(epptr));
            thread_state_ptr_set_blockingIPCDiminishCaps(
                &thread->tcbState, diminish);

            scheduleTCB(thread);

            /* Place calling thread in endpoint queue */
            queue = ep_ptr_get_queue(epptr);
            queue = tcbEPAppend(thread, queue);
            endpoint_ptr_set_state(epptr, EPState_Recv);
            ep_ptr_set_queue(epptr, queue);
            break;
        }

        case EPState_Send: {
            tcb_queue_t queue;
            tcb_t *sender;
            word_t badge;
            bool_t canGrant;
            bool_t do_call;

            /* Get the head of the endpoint queue. */
            queue = ep_ptr_get_queue(epptr);
            sender = queue.head;

            /* Haskell error "Send endpoint queue must not be empty" */
            assert(sender);

            /* Dequeue the first TCB */
            queue = tcbEPDequeue(sender, queue);
            ep_ptr_set_queue(epptr, queue);

            if (!queue.head) {
                endpoint_ptr_set_state(epptr, EPState_Idle);
            }

            /* Get sender IPC details */
            badge = thread_state_ptr_get_blockingIPCBadge(&sender->tcbState);
            canGrant =
                thread_state_ptr_get_blockingIPCCanGrant(&sender->tcbState);

            /* Do the transfer */
            doIPCTransfer(sender, epptr, badge,
                          canGrant, thread, diminish);

            do_call = thread_state_ptr_get_blockingIPCIsCall(&sender->tcbState);

            if (do_call ||
                    fault_get_faultType(sender->tcbFault) != fault_null_fault) {
                if (canGrant && !diminish) {
                    setupCallerCap(sender, thread);
                } else {
                    setThreadState(sender, ThreadState_Inactive);
                }
            } else {
                setThreadState(sender, ThreadState_Running);
                switchIfRequiredTo(sender);
            }

            break;
        }
        }
    }
}

void
replyFromKernel_error(tcb_t *thread)
{
    unsigned int len;
    word_t *ipcBuffer;

    ipcBuffer = lookupIPCBuffer(true, thread);
    setRegister(thread, badgeRegister, 0);
    len = setMRs_syscall_error(thread, ipcBuffer);
    setRegister(thread, msgInfoRegister, wordFromMessageInfo(
                    message_info_new(current_syscall_error.type, 0, 0, len)));
}

void
replyFromKernel_success_empty(tcb_t *thread)
{
    setRegister(thread, badgeRegister, 0);
    setRegister(thread, msgInfoRegister, wordFromMessageInfo(
                    message_info_new(0, 0, 0, 0)));
}

void
ipcCancel(tcb_t *tptr)
{
    thread_state_t *state = &tptr->tcbState;

    switch (thread_state_ptr_get_tsType(state)) {
    case ThreadState_BlockedOnSend:
    case ThreadState_BlockedOnReceive: {
        /* blockedIPCCancel state */
        endpoint_t *epptr;
        tcb_queue_t queue;

        epptr = EP_PTR(thread_state_ptr_get_blockingIPCEndpoint(state));

        /* Haskell error "blockedIPCCancel: endpoint must not be idle" */
        assert(endpoint_ptr_get_state(epptr) != EPState_Idle);

        /* Dequeue TCB */
        queue = ep_ptr_get_queue(epptr);
        queue = tcbEPDequeue(tptr, queue);
        ep_ptr_set_queue(epptr, queue);

        if (!queue.head) {
            endpoint_ptr_set_state(epptr, EPState_Idle);
        }

        setThreadState(tptr, ThreadState_Inactive);
        break;
    }

    case ThreadState_BlockedOnAsyncEvent:
        asyncIPCCancel(tptr,
                       AEP_PTR(thread_state_ptr_get_blockingIPCEndpoint(state)));
        break;

    case ThreadState_BlockedOnReply: {
        cte_t *slot, *callerCap;

        fault_null_fault_ptr_new(&tptr->tcbFault);

        /* Get the reply cap slot */
        slot = TCB_PTR_CTE_PTR(tptr, tcbReply);

        callerCap = CTE_PTR(cap_reply_cap_get_capCallerSlot(slot->cap));
        if (callerCap) {
            finaliseCap(callerCap->cap, true, true);
            callerCap->cap = cap_null_cap_new();
        }
        cap_reply_cap_ptr_set_capCallerSlot(&slot->cap, CTE_REF(NULL));

        break;
    }
    }
}

void
epCancelAll(endpoint_t *epptr)
{
    switch (endpoint_ptr_get_state(epptr)) {
    case EPState_Idle:
        break;

    default: {
        tcb_t *thread = TCB_PTR(endpoint_ptr_get_epQueue_head(epptr));

        /* Make endpoint idle */
        endpoint_ptr_set_state(epptr, EPState_Idle);
        endpoint_ptr_set_epQueue_head(epptr, 0);
        endpoint_ptr_set_epQueue_tail(epptr, 0);

        /* Set all blocked threads to restart */
        for (; thread; thread = thread->tcbEPNext) {
            setThreadState (thread, ThreadState_Restart);
            tcbSchedEnqueue(thread);
        }

        rescheduleRequired();
        break;
    }
    }
}

void
epCancelBadgedSends(endpoint_t *epptr, word_t badge)
{
    switch (endpoint_ptr_get_state(epptr)) {
    case EPState_Idle:
    case EPState_Recv:
        break;

    case EPState_Send: {
        tcb_t *thread, *next;
        tcb_queue_t queue = ep_ptr_get_queue(epptr);

        /* this is a de-optimisation for verification
         * reasons. it allows the contents of the endpoint
         * queue to be ignored during the for loop. */
        endpoint_ptr_set_state(epptr, EPState_Idle);
        endpoint_ptr_set_epQueue_head(epptr, 0);
        endpoint_ptr_set_epQueue_tail(epptr, 0);

        for (thread = queue.head; thread; thread = next) {
            word_t b = thread_state_ptr_get_blockingIPCBadge(
                           &thread->tcbState);
            next = thread->tcbEPNext;
            if (b == badge) {
                setThreadState(thread, ThreadState_Restart);
                tcbSchedEnqueue(thread);
                queue = tcbEPDequeue(thread, queue);
            }
        }
        ep_ptr_set_queue(epptr, queue);

        if (queue.head) {
            endpoint_ptr_set_state(epptr, EPState_Send);
        }

        rescheduleRequired();

        break;
    }

    default:
        fail("invalid EP state");
    }
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/object/interrupt.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <assert.h>
#include <types.h>
#include <api/failures.h>
#include <api/invocation.h>
#include <api/syscall.h>
#include <machine/io.h>
#include <object/structures.h>
#include <object/interrupt.h>
#include <object/cnode.h>
#include <object/asyncendpoint.h>
#include <kernel/cspace.h>
#include <kernel/thread.h>
#include <model/statedata.h>

exception_t
decodeIRQControlInvocation(word_t label, unsigned int length,
                           cte_t *srcSlot, extra_caps_t extraCaps,
                           word_t *buffer)
{
    if (label == IRQIssueIRQHandler) {
        word_t index, depth, irq_w;
        irq_t irq;
        cte_t *destSlot;
        cap_t cnodeCap;
        lookupSlot_ret_t lu_ret;
        exception_t status;

        if (length < 3 || extraCaps.excaprefs[0] == NULL) {
            current_syscall_error.type = seL4_TruncatedMessage;
            return EXCEPTION_SYSCALL_ERROR;
        }
        irq_w = getSyscallArg(0, buffer);
        irq = (irq_t) irq_w;
        index = getSyscallArg(1, buffer);
        depth = getSyscallArg(2, buffer);

        cnodeCap = extraCaps.excaprefs[0]->cap;

        if (irq_w > maxIRQ) {
            current_syscall_error.type = seL4_RangeError;
            current_syscall_error.rangeErrorMin = 0;
            current_syscall_error.rangeErrorMax = maxIRQ;
            return EXCEPTION_SYSCALL_ERROR;
        }

        if (isIRQActive(irq)) {
            current_syscall_error.type = seL4_RevokeFirst;
            return EXCEPTION_SYSCALL_ERROR;
        }

        lu_ret = lookupTargetSlot(cnodeCap, index, depth);
        if (lu_ret.status != EXCEPTION_NONE) {
            return lu_ret.status;
        }
        destSlot = lu_ret.slot;

        status = ensureEmptySlot(destSlot);
        if (status != EXCEPTION_NONE) {
            return status;
        }

        setThreadState(ksCurThread, ThreadState_Restart);
        return invokeIRQControl(irq, destSlot, srcSlot);
    } else if (label == IRQInterruptControl) {
        return Arch_decodeInterruptControl(length, extraCaps);
    } else {
        userError("IRQControl: Illegal operation.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }
}

exception_t
invokeIRQControl(irq_t irq, cte_t *handlerSlot, cte_t *controlSlot)
{
    setIRQState(IRQNotifyAEP, irq);
    cteInsert(cap_irq_handler_cap_new(irq), controlSlot, handlerSlot);

    return EXCEPTION_NONE;
}

exception_t
decodeIRQHandlerInvocation(word_t label, unsigned int length, irq_t irq,
                           extra_caps_t extraCaps, word_t *buffer)
{
    switch (label) {
    case IRQAckIRQ:
        setThreadState(ksCurThread, ThreadState_Restart);
        invokeIRQHandler_AckIRQ(irq);
        return EXCEPTION_NONE;

    case IRQSetIRQHandler: {
        cap_t aepCap;
        cte_t *slot;

        if (extraCaps.excaprefs[0] == NULL) {
            current_syscall_error.type = seL4_TruncatedMessage;
            return EXCEPTION_SYSCALL_ERROR;
        }
        aepCap = extraCaps.excaprefs[0]->cap;
        slot = extraCaps.excaprefs[0];

        if (cap_get_capType(aepCap) != cap_async_endpoint_cap ||
                !cap_async_endpoint_cap_get_capAEPCanSend(aepCap)) {
            if (cap_get_capType(aepCap) != cap_async_endpoint_cap) {
                userError("IRQSetHandler: provided cap is not an async endpoint capability.");
            } else {
                userError("IRQSetHandler: caller does not have send rights on the endpoint.");
            }
            current_syscall_error.type = seL4_InvalidCapability;
            current_syscall_error.invalidCapNumber = 0;
            return EXCEPTION_SYSCALL_ERROR;
        }

        setThreadState(ksCurThread, ThreadState_Restart);
        invokeIRQHandler_SetIRQHandler(irq, aepCap, slot);
        return EXCEPTION_NONE;
    }

    case IRQClearIRQHandler:
        setThreadState(ksCurThread, ThreadState_Restart);
        invokeIRQHandler_ClearIRQHandler(irq);
        return EXCEPTION_NONE;
    case IRQSetMode: {
        bool_t trig, pol;

        if (length < 2) {
            userError("IRQSetMode: Not enough arguments", length);
            current_syscall_error.type = seL4_TruncatedMessage;
            return EXCEPTION_SYSCALL_ERROR;
        }
        trig = getSyscallArg(0, buffer);
        pol = getSyscallArg(1, buffer);

        setThreadState(ksCurThread, ThreadState_Restart);
        invokeIRQHandler_SetMode(irq, !!trig, !!pol);
        return EXCEPTION_NONE;
    }

    default:
        userError("IRQHandler: Illegal operation.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }
}

void
invokeIRQHandler_AckIRQ(irq_t irq)
{
    maskInterrupt(false, irq);
}

void invokeIRQHandler_SetMode(irq_t irq, bool_t levelTrigger, bool_t polarityLow)
{
    setInterruptMode(irq, levelTrigger, polarityLow);
}

void
invokeIRQHandler_SetIRQHandler(irq_t irq, cap_t cap, cte_t *slot)
{
    cte_t *irqSlot;

    irqSlot = intStateIRQNode + irq;
    /** GHOSTUPD: "(True, gs_set_assn cteDeleteOne_'proc (-1))" */
    cteDeleteOne(irqSlot);
    cteInsert(cap, slot, irqSlot);
}

void
invokeIRQHandler_ClearIRQHandler(irq_t irq)
{
    cte_t *irqSlot;

    irqSlot = intStateIRQNode + irq;
    /** GHOSTUPD: "(True, gs_set_assn cteDeleteOne_'proc (-1))" */
    cteDeleteOne(irqSlot);
}

void
deletingIRQHandler(irq_t irq)
{
    cte_t *slot;

    userError("IRQ %d", irq);
    slot = intStateIRQNode + irq;
    /** GHOSTUPD: "(True, gs_set_assn cteDeleteOne_'proc (ucast cap_async_endpoint_cap))" */
    cteDeleteOne(slot);
}

void
deletedIRQHandler(irq_t irq)
{
    setIRQState(IRQInactive, irq);
}



void
handleInterrupt(irq_t irq)
{
    switch (intStateIRQTable[irq]) {
    case IRQNotifyAEP: {
        cap_t cap;

        cap = intStateIRQNode[irq].cap;

        if (cap_get_capType(cap) == cap_async_endpoint_cap &&
                cap_async_endpoint_cap_get_capAEPCanSend(cap)) {
            sendAsyncIPC(AEP_PTR(cap_async_endpoint_cap_get_capAEPPtr(cap)),
                         cap_async_endpoint_cap_get_capAEPBadge(cap));
        } else {
#ifdef CONFIG_IRQ_REPORTING
            printf("Undelivered IRQ: %d\n", (int)irq);
#endif
        }
        maskInterrupt(true, irq);
        break;
    }

    case IRQTimer:
        timerTick();
        resetTimer();
        break;

    case IRQReserved:
        handleReservedIRQ(irq);
        break;

    case IRQInactive:
        /*
         * This case shouldn't happen anyway unless the hardware or
         * platform code is broken. Hopefully masking it again should make
         * the interrupt go away.
         */
        maskInterrupt(true, irq);
#ifdef CONFIG_IRQ_REPORTING
        printf("Received disabled IRQ: %d\n", (int)irq);
#endif
        break;

    default:
        /* No corresponding haskell error */
        fail("Invalid IRQ state");
    }

    ackInterrupt(irq);
}

bool_t
isIRQActive(irq_t irq)
{
    return intStateIRQTable[irq] != IRQInactive;
}

void
setIRQState(irq_state_t irqState, irq_t irq)
{
    intStateIRQTable[irq] = irqState;
    maskInterrupt(irqState == IRQInactive, irq);
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/object/objecttype.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <assert.h>
#include <config.h>
#include <types.h>
#include <api/failures.h>
#include <api/syscall.h>
#include <arch/object/objecttype.h>
#include <machine/io.h>
#include <object/objecttype.h>
#include <object/structures.h>
#include <object/asyncendpoint.h>
#include <object/endpoint.h>
#include <object/cnode.h>
#include <object/interrupt.h>
#include <object/tcb.h>
#include <object/untyped.h>
#include <model/statedata.h>
#include <kernel/thread.h>
#include <kernel/vspace.h>
#include <machine.h>
#include <util.h>
#include <string.h>

word_t getObjectSize(word_t t, word_t userObjSize)
{
    if (t >= seL4_NonArchObjectTypeCount) {
        return Arch_getObjectSize(t);
    } else {
        switch (t) {
        case seL4_TCBObject:
            return TCB_BLOCK_SIZE_BITS;
        case seL4_EndpointObject:
            return EP_SIZE_BITS;
        case seL4_AsyncEndpointObject:
            return AEP_SIZE_BITS;
        case seL4_CapTableObject:
            return CTE_SIZE_BITS + userObjSize;
        case seL4_UntypedObject:
            return userObjSize;
        default:
            fail("Invalid object type");
            return 0;
        }
    }
}

deriveCap_ret_t
deriveCap(cte_t *slot, cap_t cap)
{
    deriveCap_ret_t ret;

    if (isArchCap(cap)) {
        return Arch_deriveCap(slot, cap);
    }

    switch (cap_get_capType(cap)) {
    case cap_zombie_cap:
        ret.status = EXCEPTION_NONE;
        ret.cap = cap_null_cap_new();
        break;

    case cap_irq_control_cap:
        ret.status = EXCEPTION_NONE;
        ret.cap = cap_null_cap_new();
        break;

    case cap_reply_cap:
        ret.status = EXCEPTION_NONE;
        ret.cap = cap_null_cap_new();
        break;

    default:
        ret.status = EXCEPTION_NONE;
        ret.cap = cap;
    }

    return ret;
}

finaliseCap_ret_t
finaliseCap(cap_t cap, bool_t final, bool_t exposed)
{
    finaliseCap_ret_t fc_ret;

    if (isArchCap(cap)) {
        fc_ret.remainder = Arch_finaliseCap(cap, final);
        fc_ret.irq = irqInvalid;
        return fc_ret;
    }

    switch (cap_get_capType(cap)) {
    case cap_endpoint_cap:
        if (final) {
            epCancelAll(EP_PTR(cap_endpoint_cap_get_capEPPtr(cap)));
        }

        fc_ret.remainder = cap_null_cap_new();
        fc_ret.irq = irqInvalid;
        return fc_ret;

    case cap_async_endpoint_cap:
        if (final) {
            async_endpoint_t *aep = AEP_PTR(cap_async_endpoint_cap_get_capAEPPtr(cap));
            tcb_t *boundTCB = (tcb_t*)async_endpoint_ptr_get_aepBoundTCB(aep);;

            if (boundTCB) {
                unbindAsyncEndpoint(boundTCB);
            }

            aepCancelAll(aep);
        }
        fc_ret.remainder = cap_null_cap_new();
        fc_ret.irq = irqInvalid;
        return fc_ret;

    case cap_reply_cap: {
        tcb_t *callee;
        cte_t *replySlot;
        callee = TCB_PTR(cap_reply_cap_get_capTCBPtr(cap));
        replySlot = TCB_PTR_CTE_PTR(callee, tcbReply);
        /* Remove the reference to us */
        cap_reply_cap_ptr_set_capCallerSlot(&replySlot->cap, CTE_REF(NULL));
        fc_ret.remainder = cap_null_cap_new();
        fc_ret.irq = irqInvalid;
        return fc_ret;
    }
    case cap_null_cap:
    case cap_domain_cap:
        fc_ret.remainder = cap_null_cap_new();
        fc_ret.irq = irqInvalid;
        return fc_ret;
    }

    if (exposed) {
        fail("finaliseCap: failed to finalise immediately.");
    }

    switch (cap_get_capType(cap)) {
    case cap_cnode_cap: {
        if (final) {
            fc_ret.remainder =
                Zombie_new(
                    1 << cap_cnode_cap_get_capCNodeRadix(cap),
                    cap_cnode_cap_get_capCNodeRadix(cap),
                    cap_cnode_cap_get_capCNodePtr(cap)
                );
            fc_ret.irq = irqInvalid;
            return fc_ret;
        }
        break;
    }

    case cap_thread_cap: {
        if (final) {
            tcb_t *tcb;
            cte_t *cte_ptr;
            cte_t *replySlot;

            tcb = TCB_PTR(cap_thread_cap_get_capTCBPtr(cap));
            cte_ptr = TCB_PTR_CTE_PTR(tcb, tcbCTable);
            unbindAsyncEndpoint(tcb);
            suspend(tcb);
            replySlot = TCB_PTR_CTE_PTR(tcb, tcbReply);
            if (cap_get_capType(replySlot->cap) == cap_reply_cap) {
                assert(cap_reply_cap_get_capTCBPtr(replySlot->cap) == 0);
                replySlot->cap = cap_null_cap_new();
            }
            Arch_prepareThreadDelete(tcb);
            fc_ret.remainder =
                Zombie_new(
                    tcbArchCNodeEntries,
                    ZombieType_ZombieTCB,
                    CTE_REF(cte_ptr)
                );
            fc_ret.irq = irqInvalid;
            return fc_ret;
        }
        break;
    }

    case cap_zombie_cap:
        fc_ret.remainder = cap;
        fc_ret.irq = irqInvalid;
        return fc_ret;

    case cap_irq_handler_cap:
        if (final) {
            irq_t irq = cap_irq_handler_cap_get_capIRQ(cap);

            deletingIRQHandler(irq);

            fc_ret.remainder = cap_null_cap_new();
            fc_ret.irq = irq;
            return fc_ret;
        }
        break;
    }

    fc_ret.remainder = cap_null_cap_new();
    fc_ret.irq = irqInvalid;
    return fc_ret;
}

cap_t
recycleCap(bool_t is_final, cap_t cap)
{
    if (isArchCap(cap)) {
        return Arch_recycleCap(is_final, cap);
    }

    switch (cap_get_capType(cap)) {
    case cap_null_cap:
        fail("recycleCap: can't reconstruct Null");
        break;
    case cap_domain_cap:
        return cap;
    case cap_cnode_cap:
        return cap;
    case cap_thread_cap:
        return cap;
    case cap_zombie_cap: {
        word_t type;

        type = cap_zombie_cap_get_capZombieType(cap);
        if (type == ZombieType_ZombieTCB) {
            tcb_t *tcb;
            _thread_state_t ts UNUSED;

            tcb = TCB_PTR(cap_zombie_cap_get_capZombiePtr(cap)
                          + TCB_OFFSET);
            ts = thread_state_get_tsType(tcb->tcbState);
            /* Haskell error:
             * "Zombie cap should point at inactive thread" */
            assert(ts == ThreadState_Inactive ||
                   ts != ThreadState_IdleThreadState);
            /* Haskell error:
             * "Zombie cap should not point at queued thread" */
            assert(!thread_state_get_tcbQueued(tcb->tcbState));
            /* Haskell error:
             * "Zombie cap should not point at bound thread" */
            assert(tcb->boundAsyncEndpoint == NULL);

            /* makeObject doesn't exist in C, objects are initialised by
             * zeroing. The effect of recycle in Haskell is to reinitialise
             * the TCB, with the exception of the TCB CTEs.  I achieve this
             * here by zeroing the TCB part of the structure, while leaving
             * the CNode alone. */
            memzero(tcb, sizeof (tcb_t));
            Arch_initContext(&tcb->tcbArch.tcbContext);
            tcb->tcbTimeSlice = CONFIG_TIME_SLICE;
            tcb->tcbDomain = ksCurDomain;

            return cap_thread_cap_new(TCB_REF(tcb));
        } else {
            return cap_cnode_cap_new(type, 0, 0,
                                     cap_zombie_cap_get_capZombiePtr(cap));
        }
    }
    case cap_endpoint_cap: {
        word_t badge = cap_endpoint_cap_get_capEPBadge(cap);
        if (badge) {
            endpoint_t* ep = (endpoint_t*)
                             cap_endpoint_cap_get_capEPPtr(cap);
            epCancelBadgedSends(ep, badge);
        }
        return cap;
    }
    default:
        return cap;
    }
}

bool_t CONST
hasRecycleRights(cap_t cap)
{
    switch (cap_get_capType(cap)) {
    case cap_null_cap:
    case cap_domain_cap:
        return false;

    case cap_endpoint_cap:
        return cap_endpoint_cap_get_capCanSend(cap) &&
               cap_endpoint_cap_get_capCanReceive(cap) &&
               cap_endpoint_cap_get_capCanGrant(cap);

    case cap_async_endpoint_cap:
        return cap_async_endpoint_cap_get_capAEPCanSend(cap) &&
               cap_async_endpoint_cap_get_capAEPCanReceive(cap);

    default:
        if (isArchCap(cap)) {
            return Arch_hasRecycleRights(cap);
        } else {
            return true;
        }
    }
}

bool_t CONST
sameRegionAs(cap_t cap_a, cap_t cap_b)
{
    switch (cap_get_capType(cap_a)) {
    case cap_untyped_cap: {
        word_t aBase, bBase, aTop, bTop;

        aBase = (word_t)WORD_PTR(cap_untyped_cap_get_capPtr(cap_a));
        bBase = (word_t)cap_get_capPtr(cap_b);

        aTop = aBase + MASK(cap_untyped_cap_get_capBlockSize(cap_a));
        bTop = bBase + MASK(cap_get_capSizeBits(cap_b));

        return ((bBase != 0) && (aBase <= bBase) &&
                (bTop <= aTop) && (bBase <= bTop));
    }

    case cap_endpoint_cap:
        if (cap_get_capType(cap_b) == cap_endpoint_cap) {
            return cap_endpoint_cap_get_capEPPtr(cap_a) ==
                   cap_endpoint_cap_get_capEPPtr(cap_b);
        }
        break;

    case cap_async_endpoint_cap:
        if (cap_get_capType(cap_b) == cap_async_endpoint_cap) {
            return cap_async_endpoint_cap_get_capAEPPtr(cap_a) ==
                   cap_async_endpoint_cap_get_capAEPPtr(cap_b);
        }
        break;

    case cap_cnode_cap:
        if (cap_get_capType(cap_b) == cap_cnode_cap) {
            return (cap_cnode_cap_get_capCNodePtr(cap_a) ==
                    cap_cnode_cap_get_capCNodePtr(cap_b)) &&
                   (cap_cnode_cap_get_capCNodeRadix(cap_a) ==
                    cap_cnode_cap_get_capCNodeRadix(cap_b));
        }
        break;

    case cap_thread_cap:
        if (cap_get_capType(cap_b) == cap_thread_cap) {
            return cap_thread_cap_get_capTCBPtr(cap_a) ==
                   cap_thread_cap_get_capTCBPtr(cap_b);
        }
        break;

    case cap_reply_cap:
        if (cap_get_capType(cap_b) == cap_reply_cap) {
            return cap_reply_cap_get_capTCBPtr(cap_a) ==
                   cap_reply_cap_get_capTCBPtr(cap_b);
        }
        break;

    case cap_domain_cap:
        if (cap_get_capType(cap_b) == cap_domain_cap) {
            return true;
        }
        break;

    case cap_irq_control_cap:
        if (cap_get_capType(cap_b) == cap_irq_control_cap ||
                cap_get_capType(cap_b) == cap_irq_handler_cap) {
            return true;
        }
        break;

    case cap_irq_handler_cap:
        if (cap_get_capType(cap_b) == cap_irq_handler_cap) {
            return (irq_t)cap_irq_handler_cap_get_capIRQ(cap_a) ==
                   (irq_t)cap_irq_handler_cap_get_capIRQ(cap_b);
        }
        break;

    default:
        if (isArchCap(cap_a) &&
                isArchCap(cap_b)) {
            return Arch_sameRegionAs(cap_a, cap_b);
        }
        break;
    }

    return false;
}

bool_t CONST
sameObjectAs(cap_t cap_a, cap_t cap_b)
{
    if (cap_get_capType(cap_a) == cap_untyped_cap) {
        return false;
    }
    if (cap_get_capType(cap_a) == cap_irq_control_cap &&
            cap_get_capType(cap_b) == cap_irq_handler_cap) {
        return false;
    }
    if (isArchCap(cap_a) && isArchCap(cap_b)) {
        return Arch_sameObjectAs(cap_a, cap_b);
    }
    return sameRegionAs(cap_a, cap_b);
}

cap_t CONST
updateCapData(bool_t preserve, word_t newData, cap_t cap)
{
    if (isArchCap(cap)) {
        return Arch_updateCapData(preserve, newData, cap);
    }

    switch (cap_get_capType(cap)) {
    case cap_endpoint_cap:
        if (!preserve && cap_endpoint_cap_get_capEPBadge(cap) == 0) {
            return cap_endpoint_cap_set_capEPBadge(cap, newData);
        } else {
            return cap_null_cap_new();
        }

    case cap_async_endpoint_cap:
        if (!preserve && cap_async_endpoint_cap_get_capAEPBadge(cap) == 0) {
            return cap_async_endpoint_cap_set_capAEPBadge(cap, newData);
        } else {
            return cap_null_cap_new();
        }

    case cap_cnode_cap: {
        word_t guard, guardSize;
        cnode_capdata_t w = { .words = { newData } };

        guardSize = cnode_capdata_get_guardSize(w);

        if (guardSize + cap_cnode_cap_get_capCNodeRadix(cap) > wordBits) {
            return cap_null_cap_new();
        } else {
            cap_t new_cap;

            guard = cnode_capdata_get_guard(w) & MASK(guardSize);
            new_cap = cap_cnode_cap_set_capCNodeGuard(cap, guard);
            new_cap = cap_cnode_cap_set_capCNodeGuardSize(new_cap,
                                                          guardSize);

            return new_cap;
        }
    }

    default:
        return cap;
    }
}

cap_t CONST
maskCapRights(cap_rights_t cap_rights, cap_t cap)
{
    if (isArchCap(cap)) {
        return Arch_maskCapRights(cap_rights, cap);
    }

    switch (cap_get_capType(cap)) {
    case cap_null_cap:
    case cap_domain_cap:
    case cap_cnode_cap:
    case cap_untyped_cap:
    case cap_reply_cap:
    case cap_irq_control_cap:
    case cap_irq_handler_cap:
    case cap_zombie_cap:
    case cap_thread_cap:
        return cap;

    case cap_endpoint_cap: {
        cap_t new_cap;

        new_cap = cap_endpoint_cap_set_capCanSend(
                      cap, cap_endpoint_cap_get_capCanSend(cap) &
                      cap_rights_get_capAllowWrite(cap_rights));
        new_cap = cap_endpoint_cap_set_capCanReceive(
                      new_cap, cap_endpoint_cap_get_capCanReceive(cap) &
                      cap_rights_get_capAllowRead(cap_rights));
        new_cap = cap_endpoint_cap_set_capCanGrant(
                      new_cap, cap_endpoint_cap_get_capCanGrant(cap) &
                      cap_rights_get_capAllowGrant(cap_rights));

        return new_cap;
    }

    case cap_async_endpoint_cap: {
        cap_t new_cap;

        new_cap = cap_async_endpoint_cap_set_capAEPCanSend(
                      cap, cap_async_endpoint_cap_get_capAEPCanSend(cap) &
                      cap_rights_get_capAllowWrite(cap_rights));
        new_cap = cap_async_endpoint_cap_set_capAEPCanReceive(new_cap,
                                                              cap_async_endpoint_cap_get_capAEPCanReceive(cap) &
                                                              cap_rights_get_capAllowRead(cap_rights));

        return new_cap;
    }

    default:
        fail("Invalid cap type"); /* Sentinel for invalid enums */
    }
}

cap_t
createObject(object_t t, void *regionBase, int userSize, bool_t deviceMemory)
{
    /* Handle architecture-specific objects. */
    if (t >= (object_t) seL4_NonArchObjectTypeCount) {
        return Arch_createObject(t, regionBase, userSize, deviceMemory);
    }

    /* Create objects. */
    switch ((api_object_t)t) {
    case seL4_TCBObject: {
        tcb_t *tcb;
        memzero(regionBase, 1UL << TCB_BLOCK_SIZE_BITS);
        tcb = TCB_PTR((word_t)regionBase + TCB_OFFSET);
        /** AUXUPD: "(True, ptr_retyps 5
          (Ptr ((ptr_val \<acute>tcb) - 0x100) :: cte_C ptr)
            o (ptr_retyp \<acute>tcb))" */

        /* Setup non-zero parts of the TCB. */

        Arch_initContext(&tcb->tcbArch.tcbContext);
        tcb->tcbTimeSlice = CONFIG_TIME_SLICE;
        tcb->tcbDomain = ksCurDomain;

#ifdef DEBUG
        strlcpy(tcb->tcbName, "child of: '", TCB_NAME_LENGTH);
        strlcat(tcb->tcbName, ksCurThread->tcbName, TCB_NAME_LENGTH);
        strlcat(tcb->tcbName, "'", TCB_NAME_LENGTH);
#endif

        return cap_thread_cap_new(TCB_REF(tcb));
    }

    case seL4_EndpointObject:
        memzero(regionBase, 1UL << EP_SIZE_BITS);
        /** AUXUPD: "(True, ptr_retyp
          (Ptr (ptr_val \<acute>regionBase) :: endpoint_C ptr))" */
        return cap_endpoint_cap_new(0, true, true, true,
                                    EP_REF(regionBase));

    case seL4_AsyncEndpointObject:
        memzero(regionBase, 1UL << AEP_SIZE_BITS);
        /** AUXUPD: "(True, ptr_retyp
              (Ptr (ptr_val \<acute>regionBase) :: async_endpoint_C ptr))" */
        return cap_async_endpoint_cap_new(0, true, true,
                                          AEP_REF(regionBase));

    case seL4_CapTableObject:
        memzero(regionBase, 1UL << (CTE_SIZE_BITS + userSize));
        /** AUXUPD: "(True, ptr_retyps (2 ^ (unat \<acute>userSize))
          (Ptr (ptr_val \<acute>regionBase) :: cte_C ptr))" */
        /** GHOSTUPD: "(True, gs_new_cnodes (unat \<acute>userSize)
                                (ptr_val \<acute>regionBase)
                                (4 + unat \<acute>userSize))" */
        return cap_cnode_cap_new(userSize, 0, 0, CTE_REF(regionBase));

    case seL4_UntypedObject:
        /*
         * No objects need to be created; instead, just insert caps into
         * the destination slots.
         */
        return cap_untyped_cap_new(deviceMemory, userSize, WORD_REF(regionBase));

    default:
        fail("Invalid object type");
    }
}

void
createNewObjects(object_t t, cte_t *parent, slot_range_t slots,
                 void *regionBase, unsigned int userSize, bool_t deviceMemory)
{
    word_t objectSize;
    void *nextFreeArea;
    unsigned int i;
    word_t totalObjectSize UNUSED;

    /* ghost check that we're visiting less bytes than the max object size */
    objectSize = getObjectSize(t, userSize);
    totalObjectSize = slots.length << objectSize;
    /** GHOSTUPD: "(gs_get_assn cap_get_capSizeBits_'proc \<acute>ghost'state = 0
        \<or> \<acute>totalObjectSize <= gs_get_assn cap_get_capSizeBits_'proc \<acute>ghost'state, id)" */

    /* Create the objects. */
    nextFreeArea = regionBase;
    for (i = 0; i < slots.length; i++) {
        /* Create the object. */
        /** AUXUPD: "(True, typ_clear_region (ptr_val \<acute> nextFreeArea + ((\<acute> i) << unat (\<acute> objectSize))) (unat (\<acute> objectSize)))" */
        cap_t cap = createObject(t, (void *)((word_t)nextFreeArea + (i << objectSize)), userSize, deviceMemory);

        /* Insert the cap into the user's cspace. */
        insertNewCap(parent, &slots.cnode[slots.offset + i], cap);

        /* Move along to the next region of memory. been merged into a formula of i */
    }
}

exception_t
decodeInvocation(word_t label, unsigned int length,
                 cptr_t capIndex, cte_t *slot, cap_t cap,
                 extra_caps_t extraCaps, bool_t block, bool_t call,
                 word_t *buffer)
{
    if (isArchCap(cap)) {
        return Arch_decodeInvocation(label, length, capIndex,
                                     slot, cap, extraCaps, buffer);
    }

    switch (cap_get_capType(cap)) {
    case cap_null_cap:
        userError("Attempted to invoke a null cap #%u.", capIndex);
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;
        return EXCEPTION_SYSCALL_ERROR;

    case cap_zombie_cap:
        userError("Attempted to invoke a zombie cap #%u.", capIndex);
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;
        return EXCEPTION_SYSCALL_ERROR;

    case cap_endpoint_cap:
        if (unlikely(!cap_endpoint_cap_get_capCanSend(cap))) {
            userError("Attempted to invoke a read-only endpoint cap #%u.",
                      capIndex);
            current_syscall_error.type = seL4_InvalidCapability;
            current_syscall_error.invalidCapNumber = 0;
            return EXCEPTION_SYSCALL_ERROR;
        }

        setThreadState(ksCurThread, ThreadState_Restart);
        return performInvocation_Endpoint(
                   EP_PTR(cap_endpoint_cap_get_capEPPtr(cap)),
                   cap_endpoint_cap_get_capEPBadge(cap),
                   cap_endpoint_cap_get_capCanGrant(cap), block, call);

    case cap_async_endpoint_cap: {
        if (unlikely(!cap_async_endpoint_cap_get_capAEPCanSend(cap))) {
            userError("Attempted to invoke a read-only async-endpoint cap #%u.",
                      capIndex);
            current_syscall_error.type = seL4_InvalidCapability;
            current_syscall_error.invalidCapNumber = 0;
            return EXCEPTION_SYSCALL_ERROR;
        }

        setThreadState(ksCurThread, ThreadState_Restart);
        return performInvocation_AsyncEndpoint(
                   AEP_PTR(cap_async_endpoint_cap_get_capAEPPtr(cap)),
                   cap_async_endpoint_cap_get_capAEPBadge(cap));
    }

    case cap_reply_cap:
        if (unlikely(cap_reply_cap_get_capReplyMaster(cap))) {
            userError("Attempted to invoke an invalid reply cap #%u.",
                      capIndex);
            current_syscall_error.type = seL4_InvalidCapability;
            current_syscall_error.invalidCapNumber = 0;
            return EXCEPTION_SYSCALL_ERROR;
        }

        setThreadState(ksCurThread, ThreadState_Restart);
        return performInvocation_Reply(
                   TCB_PTR(cap_reply_cap_get_capTCBPtr(cap)), slot);

    case cap_thread_cap:
        return decodeTCBInvocation(label, length, cap,
                                   slot, extraCaps, call, buffer);

    case cap_domain_cap:
        return decodeDomainInvocation(label, length, extraCaps, buffer);

    case cap_cnode_cap:
        return decodeCNodeInvocation(label, length, cap, extraCaps, buffer);

    case cap_untyped_cap:
        return decodeUntypedInvocation(label, length, slot, cap, extraCaps,
                                       call, buffer);

    case cap_irq_control_cap:
        return decodeIRQControlInvocation(label, length, slot,
                                          extraCaps, buffer);

    case cap_irq_handler_cap:
        return decodeIRQHandlerInvocation(label, length,
                                          cap_irq_handler_cap_get_capIRQ(cap), extraCaps, buffer);

    default:
        fail("Invalid cap type");
    }
}

exception_t
performInvocation_Endpoint(endpoint_t *ep, word_t badge,
                           bool_t canGrant, bool_t block,
                           bool_t call)
{
    sendIPC(block, call, badge, canGrant, ksCurThread, ep);

    return EXCEPTION_NONE;
}

exception_t
performInvocation_AsyncEndpoint(async_endpoint_t *aep, word_t badge)
{
    sendAsyncIPC(aep, badge);

    return EXCEPTION_NONE;
}

exception_t
performInvocation_Reply(tcb_t *thread, cte_t *slot)
{
    doReplyTransfer(ksCurThread, thread, slot);
    return EXCEPTION_NONE;
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/object/tcb.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <types.h>
#include <api/failures.h>
#include <api/invocation.h>
#include <api/syscall.h>
#include <machine/io.h>
#include <object/structures.h>
#include <object/objecttype.h>
#include <object/cnode.h>
#include <object/tcb.h>
#include <kernel/cspace.h>
#include <kernel/thread.h>
#include <kernel/vspace.h>
#include <model/statedata.h>
#include <util.h>
#include <string.h>

static inline void
addToBitmap(word_t dom, word_t prio)
{
    uint32_t l1index;

    l1index = prio_to_l1index(prio);
    ksReadyQueuesL1Bitmap[dom] |= BIT(l1index);
    ksReadyQueuesL2Bitmap[dom][l1index] |= BIT(prio & MASK(5));
}

static inline void
removeFromBitmap(word_t dom, word_t prio)
{
    uint32_t l1index;

    l1index = prio_to_l1index(prio);
    ksReadyQueuesL2Bitmap[dom][l1index] &= ~BIT(prio & MASK(5));
    if (unlikely(!ksReadyQueuesL2Bitmap[dom][l1index])) {
        ksReadyQueuesL1Bitmap[dom] &= ~BIT(l1index);
    }
}

/* Add TCB to the head of a scheduler queue */
void
tcbSchedEnqueue(tcb_t *tcb)
{
    if (!thread_state_get_tcbQueued(tcb->tcbState)) {
        tcb_queue_t queue;
        UNUSED dom_t dom;
        prio_t prio;
        unsigned int idx;

        dom = tcb->tcbDomain;
        prio = tcb->tcbPriority;
        idx = ready_queues_index(dom, prio);
        queue = ksReadyQueues[idx];

        if (!queue.end) { /* Empty list */
            queue.end = tcb;
            addToBitmap(dom, prio);
        } else {
            queue.head->tcbSchedPrev = tcb;
        }
        tcb->tcbSchedPrev = NULL;
        tcb->tcbSchedNext = queue.head;
        queue.head = tcb;

        ksReadyQueues[idx] = queue;

        thread_state_ptr_set_tcbQueued(&tcb->tcbState, true);
    }
}

/* Add TCB to the end of a scheduler queue */
void
tcbSchedAppend(tcb_t *tcb)
{
    if (!thread_state_get_tcbQueued(tcb->tcbState)) {
        tcb_queue_t queue;
        UNUSED dom_t dom;
        prio_t prio;
        unsigned int idx;

        dom = tcb->tcbDomain;
        prio = tcb->tcbPriority;
        idx = ready_queues_index(dom, prio);
        queue = ksReadyQueues[idx];

        if (!queue.head) { /* Empty list */
            queue.head = tcb;
            addToBitmap(dom, prio);
        } else {
            queue.end->tcbSchedNext = tcb;
        }
        tcb->tcbSchedPrev = queue.end;
        tcb->tcbSchedNext = NULL;
        queue.end = tcb;

        ksReadyQueues[idx] = queue;

        thread_state_ptr_set_tcbQueued(&tcb->tcbState, true);
    }
}

/* Remove TCB from a scheduler queue */
void
tcbSchedDequeue(tcb_t *tcb)
{
    if (thread_state_get_tcbQueued(tcb->tcbState)) {
        tcb_queue_t queue;
        UNUSED dom_t dom;
        prio_t prio;
        unsigned int idx;

        dom = tcb->tcbDomain;
        prio = tcb->tcbPriority;
        idx = ready_queues_index(dom, prio);
        queue = ksReadyQueues[idx];

        if (tcb->tcbSchedPrev) {
            tcb->tcbSchedPrev->tcbSchedNext = tcb->tcbSchedNext;
        } else {
            queue.head = tcb->tcbSchedNext;
            if (likely(!tcb->tcbSchedNext)) {
                removeFromBitmap(dom, prio);
            }
        }

        if (tcb->tcbSchedNext) {
            tcb->tcbSchedNext->tcbSchedPrev = tcb->tcbSchedPrev;
        } else {
            queue.end = tcb->tcbSchedPrev;
        }

        ksReadyQueues[idx] = queue;

        thread_state_ptr_set_tcbQueued(&tcb->tcbState, false);
    }
}

/* Add TCB to the end of an endpoint queue */
tcb_queue_t
tcbEPAppend(tcb_t *tcb, tcb_queue_t queue)
{
    if (!queue.head) { /* Empty list */
        queue.head = tcb;
    } else {
        queue.end->tcbEPNext = tcb;
    }
    tcb->tcbEPPrev = queue.end;
    tcb->tcbEPNext = NULL;
    queue.end = tcb;

    return queue;
}

/* Remove TCB from an endpoint queue */
tcb_queue_t
tcbEPDequeue(tcb_t *tcb, tcb_queue_t queue)
{
    if (tcb->tcbEPPrev) {
        tcb->tcbEPPrev->tcbEPNext = tcb->tcbEPNext;
    } else {
        queue.head = tcb->tcbEPNext;
    }

    if (tcb->tcbEPNext) {
        tcb->tcbEPNext->tcbEPPrev = tcb->tcbEPPrev;
    } else {
        queue.end = tcb->tcbEPPrev;
    }

    return queue;
}

cptr_t PURE
getExtraCPtr(word_t *bufferPtr, unsigned int i)
{
    return (cptr_t)bufferPtr[seL4_MsgMaxLength + 2 + i];
}

void
setExtraBadge(word_t *bufferPtr, word_t badge,
              unsigned int i)
{
    bufferPtr[seL4_MsgMaxLength + 2 + i] = badge;
}

void
setupCallerCap(tcb_t *sender, tcb_t *receiver)
{
    cte_t *replySlot, *callerSlot;
    cap_t masterCap UNUSED, callerCap UNUSED;

    setThreadState(sender, ThreadState_BlockedOnReply);
    replySlot = TCB_PTR_CTE_PTR(sender, tcbReply);
    callerSlot = TCB_PTR_CTE_PTR(receiver, tcbCaller);
    masterCap = replySlot->cap;
    /* Haskell error: "Sender must have a valid master reply cap" */
    assert(cap_get_capType(masterCap) == cap_reply_cap);
    assert(cap_reply_cap_get_capReplyMaster(masterCap));
    assert(TCB_PTR(cap_reply_cap_get_capTCBPtr(masterCap)) == NULL);
    cap_reply_cap_ptr_set_capCallerSlot(&replySlot->cap, CTE_REF(callerSlot));
    callerCap = callerSlot->cap;
    /* Haskell error: "Caller cap must not already exist" */
    assert(cap_get_capType(callerCap) == cap_null_cap);
    callerSlot->cap = cap_reply_cap_new(CTE_REF(NULL), false, TCB_REF(sender));
}

void
deleteCallerCap(tcb_t *receiver)
{
    cte_t *callerSlot;

    callerSlot = TCB_PTR_CTE_PTR(receiver, tcbCaller);
    if (cap_get_capType(callerSlot->cap) == cap_reply_cap) {
        finaliseCap(callerSlot->cap, true, true);
        callerSlot->cap = cap_null_cap_new();
    }
}

extra_caps_t current_extra_caps;

exception_t
lookupExtraCaps(tcb_t* thread, word_t *bufferPtr, message_info_t info)
{
    lookupSlot_raw_ret_t lu_ret;
    cptr_t cptr;
    unsigned int i, length;

    if (!bufferPtr) {
        current_extra_caps.excaprefs[0] = NULL;
        return EXCEPTION_NONE;
    }

    length = message_info_get_msgExtraCaps(info);

    for (i = 0; i < length; i++) {
        cptr = getExtraCPtr(bufferPtr, i);

        lu_ret = lookupSlot(thread, cptr);
        if (lu_ret.status != EXCEPTION_NONE) {
            current_fault = fault_cap_fault_new(cptr, false);
            return lu_ret.status;
        }

        current_extra_caps.excaprefs[i] = lu_ret.slot;
    }
    if (i < seL4_MsgMaxExtraCaps) {
        current_extra_caps.excaprefs[i] = NULL;
    }

    return EXCEPTION_NONE;
}

/* Copy IPC MRs from one thread to another */
unsigned int
copyMRs(tcb_t *sender, word_t *sendBuf, tcb_t *receiver,
        word_t *recvBuf, unsigned int n)
{
    unsigned int i;

    /* Copy inline words */
    for (i = 0; i < n && i < n_msgRegisters; i++) {
        setRegister(receiver, msgRegisters[i],
                    getRegister(sender, msgRegisters[i]));
    }

    if (!recvBuf || !sendBuf) {
        return i;
    }

    /* Copy out-of-line words */
    for (; i < n; i++) {
        recvBuf[i + 1] = sendBuf[i + 1];
    }

    return i;
}

/* The following functions sit in the syscall error monad, but include the
 * exception cases for the preemptible bottom end, as they call the invoke
 * functions directly.  This is a significant deviation from the Haskell
 * spec. */
exception_t
decodeTCBInvocation(word_t label, unsigned int length, cap_t cap,
                    cte_t* slot, extra_caps_t extraCaps, bool_t call,
                    word_t *buffer)
{
    switch (label) {
    case TCBReadRegisters:
        /* Second level of decoding */
        return decodeReadRegisters(cap, length, call, buffer);

    case TCBWriteRegisters:
        return decodeWriteRegisters(cap, length, buffer);

    case TCBCopyRegisters:
        return decodeCopyRegisters(cap, length, extraCaps, buffer);

    case TCBSuspend:
        /* Jump straight to the invoke */
        setThreadState(ksCurThread, ThreadState_Restart);
        return invokeTCB_Suspend(
                   TCB_PTR(cap_thread_cap_get_capTCBPtr(cap)));

    case TCBResume:
        setThreadState(ksCurThread, ThreadState_Restart);
        return invokeTCB_Resume(
                   TCB_PTR(cap_thread_cap_get_capTCBPtr(cap)));

    case TCBConfigure:
        return decodeTCBConfigure(cap, length, slot, extraCaps, buffer);

    case TCBSetPriority:
        return decodeSetPriority(cap, length, buffer);

    case TCBSetIPCBuffer:
        return decodeSetIPCBuffer(cap, length, slot, extraCaps, buffer);

    case TCBSetSpace:
        return decodeSetSpace(cap, length, slot, extraCaps, buffer);

    case TCBBindAEP:
        return decodeBindAEP(cap, extraCaps);

    case TCBUnbindAEP:
        return decodeUnbindAEP(cap);

        /* This is temporary until arch specific TCB operations are implemented */
#ifdef CONFIG_VTX
    case TCBSetEPTRoot:
        return decodeSetEPTRoot(cap, extraCaps);
#endif

    default:
        /* Haskell: "throw IllegalOperation" */
        userError("TCB: Illegal operation.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }
}

enum CopyRegistersFlags {
    CopyRegisters_suspendSource = 0,
    CopyRegisters_resumeTarget = 1,
    CopyRegisters_transferFrame = 2,
    CopyRegisters_transferInteger = 3
};

exception_t
decodeCopyRegisters(cap_t cap, unsigned int length,
                    extra_caps_t extraCaps, word_t *buffer)
{
    word_t transferArch;
    tcb_t *srcTCB;
    cap_t source_cap;
    word_t flags;

    if (length < 1 || extraCaps.excaprefs[0] == NULL) {
        userError("TCB CopyRegisters: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    flags = getSyscallArg(0, buffer);

    transferArch = Arch_decodeTransfer(flags >> 8);

    source_cap = extraCaps.excaprefs[0]->cap;

    if (cap_get_capType(source_cap) == cap_thread_cap) {
        srcTCB = TCB_PTR(cap_thread_cap_get_capTCBPtr(source_cap));
    } else {
        userError("TCB CopyRegisters: Invalid source TCB.");
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 1;
        return EXCEPTION_SYSCALL_ERROR;
    }

    setThreadState(ksCurThread, ThreadState_Restart);
    return invokeTCB_CopyRegisters(
               TCB_PTR(cap_thread_cap_get_capTCBPtr(cap)), srcTCB,
               flags & BIT(CopyRegisters_suspendSource),
               flags & BIT(CopyRegisters_resumeTarget),
               flags & BIT(CopyRegisters_transferFrame),
               flags & BIT(CopyRegisters_transferInteger),
               transferArch);

}

enum ReadRegistersFlags {
    ReadRegisters_suspend = 0
};

exception_t
decodeReadRegisters(cap_t cap, unsigned int length, bool_t call,
                    word_t *buffer)
{
    word_t transferArch, flags, n;

    if (length < 2) {
        userError("TCB ReadRegisters: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    flags = getSyscallArg(0, buffer);
    n     = getSyscallArg(1, buffer);

    if (n < 1 || n > n_frameRegisters + n_gpRegisters) {
        userError("TCB ReadRegisters: Attempted to read an invalid number of registers (%d).",
                  (int)n);
        current_syscall_error.type = seL4_RangeError;
        current_syscall_error.rangeErrorMin = 1;
        current_syscall_error.rangeErrorMax = n_frameRegisters +
                                              n_gpRegisters;
        return EXCEPTION_SYSCALL_ERROR;
    }

    transferArch = Arch_decodeTransfer(flags >> 8);

    setThreadState(ksCurThread, ThreadState_Restart);
    return invokeTCB_ReadRegisters(
               TCB_PTR(cap_thread_cap_get_capTCBPtr(cap)),
               flags & BIT(ReadRegisters_suspend),
               n, transferArch, call);
}

enum WriteRegistersFlags {
    WriteRegisters_resume = 0
};

exception_t
decodeWriteRegisters(cap_t cap, unsigned int length, word_t *buffer)
{
    word_t flags, w;
    word_t transferArch;
    tcb_t* thread;

    if (length < 2) {
        userError("TCB WriteRegisters: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    flags = getSyscallArg(0, buffer);
    w     = getSyscallArg(1, buffer);

    if (length - 2 < w) {
        userError("TCB WriteRegisters: Message too short for requested write size (%d/%d).",
                  (int)(length - 2), (int)w);
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    transferArch = Arch_decodeTransfer(flags >> 8);

    thread = TCB_PTR(cap_thread_cap_get_capTCBPtr(cap));

    setThreadState(ksCurThread, ThreadState_Restart);
    return invokeTCB_WriteRegisters(thread,
                                    flags & BIT(WriteRegisters_resume),
                                    w, transferArch, buffer);
}

/* SetPriority, SetIPCParams and SetSpace are all
 * specialisations of TCBConfigure. */

exception_t
decodeTCBConfigure(cap_t cap, unsigned int length, cte_t* slot,
                   extra_caps_t rootCaps, word_t *buffer)
{
    cte_t *bufferSlot, *cRootSlot, *vRootSlot;
    cap_t bufferCap, cRootCap, vRootCap;
    deriveCap_ret_t dc_ret;
    cptr_t faultEP;
    unsigned int prio;
    word_t cRootData, vRootData, bufferAddr;

    if (length < 5 || rootCaps.excaprefs[0] == NULL
            || rootCaps.excaprefs[1] == NULL
            || rootCaps.excaprefs[2] == NULL) {
        userError("TCB Configure: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    faultEP    = getSyscallArg(0, buffer);
    prio       = getSyscallArg(1, buffer);
    cRootData  = getSyscallArg(2, buffer);
    vRootData  = getSyscallArg(3, buffer);
    bufferAddr = getSyscallArg(4, buffer);

    cRootSlot  = rootCaps.excaprefs[0];
    cRootCap   = rootCaps.excaprefs[0]->cap;
    vRootSlot  = rootCaps.excaprefs[1];
    vRootCap   = rootCaps.excaprefs[1]->cap;
    bufferSlot = rootCaps.excaprefs[2];
    bufferCap  = rootCaps.excaprefs[2]->cap;

    prio = prio & MASK(8);

    if (prio > ksCurThread->tcbPriority) {
        userError("TCB Configure: Requested priority %d too high (max %d).",
                  (int)prio, (int)(ksCurThread->tcbPriority));
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (bufferAddr == 0) {
        bufferSlot = NULL;
    } else {
        exception_t e;

        dc_ret = deriveCap(bufferSlot, bufferCap);
        if (dc_ret.status != EXCEPTION_NONE) {
            return dc_ret.status;
        }
        bufferCap = dc_ret.cap;
        e = checkValidIPCBuffer(bufferAddr, bufferCap);
        if (e != EXCEPTION_NONE) {
            return e;
        }
    }

    if (slotCapLongRunningDelete(
                TCB_PTR_CTE_PTR(cap_thread_cap_get_capTCBPtr(cap), tcbCTable)) ||
            slotCapLongRunningDelete(
                TCB_PTR_CTE_PTR(cap_thread_cap_get_capTCBPtr(cap), tcbVTable))) {
        userError("TCB Configure: CSpace or VSpace currently being deleted.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (cRootData != 0) {
        cRootCap = updateCapData(false, cRootData, cRootCap);
    }

    dc_ret = deriveCap(cRootSlot, cRootCap);
    if (dc_ret.status != EXCEPTION_NONE) {
        return dc_ret.status;
    }
    cRootCap = dc_ret.cap;

    if (cap_get_capType(cRootCap) != cap_cnode_cap &&
            (!config_set(CONFIG_ALLOW_NULL_CSPACE) ||
             cap_get_capType(cRootCap) != cap_null_cap)) {
        userError("TCB Configure: CSpace cap is invalid.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (vRootData != 0) {
        vRootCap = updateCapData(false, vRootData, vRootCap);
    }

    dc_ret = deriveCap(vRootSlot, vRootCap);
    if (dc_ret.status != EXCEPTION_NONE) {
        return dc_ret.status;
    }
    vRootCap = dc_ret.cap;

    if (!isValidVTableRoot(vRootCap)) {
        userError("TCB Configure: VSpace cap is invalid.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    setThreadState(ksCurThread, ThreadState_Restart);
    return invokeTCB_ThreadControl(
               TCB_PTR(cap_thread_cap_get_capTCBPtr(cap)), slot,
               faultEP, prio,
               cRootCap, cRootSlot,
               vRootCap, vRootSlot,
               bufferAddr, bufferCap,
               bufferSlot, thread_control_update_all);
}

exception_t
decodeSetPriority(cap_t cap, unsigned int length, word_t *buffer)
{
    prio_t newPrio;

    if (length < 1) {
        userError("TCB SetPriority: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    newPrio = getSyscallArg(0, buffer);

    /* assuming here seL4_MaxPrio is of form 2^n - 1 */
    newPrio = newPrio & MASK(8);

    if (newPrio > ksCurThread->tcbPriority) {
        userError("TCB SetPriority: Requested priority %d too high (max %d).",
                  (int)newPrio, (int)ksCurThread->tcbPriority);
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    setThreadState(ksCurThread, ThreadState_Restart);
    return invokeTCB_ThreadControl(
               TCB_PTR(cap_thread_cap_get_capTCBPtr(cap)), NULL,
               0, newPrio,
               cap_null_cap_new(), NULL,
               cap_null_cap_new(), NULL,
               0, cap_null_cap_new(),
               NULL, thread_control_update_priority);
}

exception_t
decodeSetIPCBuffer(cap_t cap, unsigned int length, cte_t* slot,
                   extra_caps_t extraCaps, word_t *buffer)
{
    cptr_t cptr_bufferPtr;
    cap_t bufferCap;
    cte_t *bufferSlot;

    if (length < 1 || extraCaps.excaprefs[0] == NULL) {
        userError("TCB SetIPCBuffer: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    cptr_bufferPtr  = getSyscallArg(0, buffer);
    bufferSlot = extraCaps.excaprefs[0];
    bufferCap  = extraCaps.excaprefs[0]->cap;

    if (cptr_bufferPtr == 0) {
        bufferSlot = NULL;
    } else {
        exception_t e;
        deriveCap_ret_t dc_ret;

        dc_ret = deriveCap(bufferSlot, bufferCap);
        if (dc_ret.status != EXCEPTION_NONE) {
            return dc_ret.status;
        }
        bufferCap = dc_ret.cap;
        e = checkValidIPCBuffer(cptr_bufferPtr, bufferCap);
        if (e != EXCEPTION_NONE) {
            return e;
        }
    }

    setThreadState(ksCurThread, ThreadState_Restart);
    return invokeTCB_ThreadControl(
               TCB_PTR(cap_thread_cap_get_capTCBPtr(cap)), slot,
               0,
               0, /* used to be prioInvalid, but it doesn't matter */
               cap_null_cap_new(), NULL,
               cap_null_cap_new(), NULL,
               cptr_bufferPtr, bufferCap,
               bufferSlot, thread_control_update_ipc_buffer);
}

exception_t
decodeSetSpace(cap_t cap, unsigned int length, cte_t* slot,
               extra_caps_t extraCaps, word_t *buffer)
{
    cptr_t faultEP;
    word_t cRootData, vRootData;
    cte_t *cRootSlot, *vRootSlot;
    cap_t cRootCap, vRootCap;
    deriveCap_ret_t dc_ret;

    if (length < 3 || extraCaps.excaprefs[0] == NULL
            || extraCaps.excaprefs[1] == NULL) {
        userError("TCB SetSpace: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    faultEP   = getSyscallArg(0, buffer);
    cRootData = getSyscallArg(1, buffer);
    vRootData = getSyscallArg(2, buffer);

    cRootSlot  = extraCaps.excaprefs[0];
    cRootCap   = extraCaps.excaprefs[0]->cap;
    vRootSlot  = extraCaps.excaprefs[1];
    vRootCap   = extraCaps.excaprefs[1]->cap;

    if (slotCapLongRunningDelete(
                TCB_PTR_CTE_PTR(cap_thread_cap_get_capTCBPtr(cap), tcbCTable)) ||
            slotCapLongRunningDelete(
                TCB_PTR_CTE_PTR(cap_thread_cap_get_capTCBPtr(cap), tcbVTable))) {
        userError("TCB SetSpace: CSpace or VSpace currently being deleted.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (cRootData != 0) {
        cRootCap = updateCapData(false, cRootData, cRootCap);
    }

    dc_ret = deriveCap(cRootSlot, cRootCap);
    if (dc_ret.status != EXCEPTION_NONE) {
        return dc_ret.status;
    }
    cRootCap = dc_ret.cap;

    if (cap_get_capType(cRootCap) != cap_cnode_cap &&
            (!config_set(CONFIG_ALLOW_NULL_CSPACE) ||
             cap_get_capType(cRootCap) != cap_null_cap)) {
        userError("TCB SetSpace: Invalid CNode cap.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (vRootData != 0) {
        vRootCap = updateCapData(false, vRootData, vRootCap);
    }

    dc_ret = deriveCap(vRootSlot, vRootCap);
    if (dc_ret.status != EXCEPTION_NONE) {
        return dc_ret.status;
    }
    vRootCap = dc_ret.cap;

    if (!isValidVTableRoot(vRootCap)) {
        userError("TCB SetSpace: Invalid VSpace cap.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    setThreadState(ksCurThread, ThreadState_Restart);
    return invokeTCB_ThreadControl(
               TCB_PTR(cap_thread_cap_get_capTCBPtr(cap)), slot,
               faultEP,
               0, /* used to be prioInvalid, but it doesn't matter */
               cRootCap, cRootSlot,
               vRootCap, vRootSlot,
               0, cap_null_cap_new(), NULL, thread_control_update_space);
}

exception_t
decodeDomainInvocation(word_t label, unsigned int length, extra_caps_t extraCaps, word_t *buffer)
{
    word_t domain;
    cap_t tcap;

    if (unlikely(label != DomainSetSet)) {
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (unlikely(length == 0)) {
        userError("Domain Configure: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    } else {
        domain = getSyscallArg(0, buffer);
        if (domain >= CONFIG_NUM_DOMAINS) {
            userError("Domain Configure: invalid domain (%u >= %u).",
                      domain, CONFIG_NUM_DOMAINS);
            current_syscall_error.type = seL4_InvalidArgument;
            current_syscall_error.invalidArgumentNumber = 0;
            return EXCEPTION_SYSCALL_ERROR;
        }
    }

    if (unlikely(extraCaps.excaprefs[0] == NULL)) {
        userError("Domain Configure: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    tcap = extraCaps.excaprefs[0]->cap;
    if (unlikely(cap_get_capType(tcap) != cap_thread_cap)) {
        userError("Domain Configure: thread cap required.");
        current_syscall_error.type = seL4_InvalidArgument;
        current_syscall_error.invalidArgumentNumber = 1;
        return EXCEPTION_SYSCALL_ERROR;
    }

    setThreadState(ksCurThread, ThreadState_Restart);
    setDomain(TCB_PTR(cap_thread_cap_get_capTCBPtr(tcap)), domain);
    return EXCEPTION_NONE;
}

exception_t decodeBindAEP(cap_t cap, extra_caps_t extraCaps)
{
    async_endpoint_t *aepptr;
    tcb_t *tcb;

    if (extraCaps.excaprefs[0] == NULL) {
        userError("TCB BindAEP: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (cap_get_capType(extraCaps.excaprefs[0]->cap) != cap_async_endpoint_cap) {
        userError("TCB BindAEP: Async endpoint is invalid.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    tcb = TCB_PTR(cap_thread_cap_get_capTCBPtr(cap));

    if (tcb->boundAsyncEndpoint) {
        userError("TCB BindAEP: TCB already has AEP.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    aepptr = AEP_PTR(cap_async_endpoint_cap_get_capAEPPtr(extraCaps.excaprefs[0]->cap));
    if ((tcb_t*)async_endpoint_ptr_get_aepQueue_head(aepptr)) {
        userError("TCB BindAEP: AEP cannot be bound.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    setThreadState(ksCurThread, ThreadState_Restart);
    return invokeTCB_AEPControl(tcb, aepptr);
}

exception_t decodeUnbindAEP(cap_t cap)
{
    tcb_t *tcb;

    tcb = TCB_PTR(cap_thread_cap_get_capTCBPtr(cap));

    if (!tcb->boundAsyncEndpoint) {
        userError("TCB UnbindAEP: TCB already has no bound AEP.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    setThreadState(ksCurThread, ThreadState_Restart);
    return invokeTCB_AEPControl(tcb, NULL);
}

/* The following functions sit in the preemption monad and implement the
 * preemptible, non-faulting bottom end of a TCB invocation. */
exception_t
invokeTCB_Suspend(tcb_t *thread)
{
    suspend(thread);
    return EXCEPTION_NONE;
}

exception_t
invokeTCB_Resume(tcb_t *thread)
{
    restart(thread);
    return EXCEPTION_NONE;
}

exception_t
invokeTCB_ThreadControl(tcb_t *target, cte_t* slot,
                        cptr_t faultep, prio_t priority,
                        cap_t cRoot_newCap, cte_t *cRoot_srcSlot,
                        cap_t vRoot_newCap, cte_t *vRoot_srcSlot,
                        word_t bufferAddr, cap_t bufferCap,
                        cte_t *bufferSrcSlot,
                        thread_control_flag_t updateFlags)
{
    exception_t e;
    cap_t tCap = cap_thread_cap_new((word_t)target);

    if (updateFlags & thread_control_update_space) {
        target->tcbFaultHandler = faultep;
    }

    if (updateFlags & thread_control_update_priority) {
        setPriority(target, priority);
    }

    if (updateFlags & thread_control_update_space) {
        cte_t *rootSlot;

        rootSlot = TCB_PTR_CTE_PTR(target, tcbCTable);
        e = cteDelete(rootSlot, true);
        if (e != EXCEPTION_NONE) {
            return e;
        }
        if (sameObjectAs(cRoot_newCap, cRoot_srcSlot->cap) &&
                sameObjectAs(tCap, slot->cap)) {
            cteInsert(cRoot_newCap, cRoot_srcSlot, rootSlot);
        }
    }

    if (updateFlags & thread_control_update_space) {
        cte_t *rootSlot;

        rootSlot = TCB_PTR_CTE_PTR(target, tcbVTable);
        e = cteDelete(rootSlot, true);
        if (e != EXCEPTION_NONE) {
            return e;
        }
        if (sameObjectAs(vRoot_newCap, vRoot_srcSlot->cap) &&
                sameObjectAs(tCap, slot->cap)) {
            cteInsert(vRoot_newCap, vRoot_srcSlot, rootSlot);
        }
    }

    if (updateFlags & thread_control_update_ipc_buffer) {
        cte_t *bufferSlot;

        bufferSlot = TCB_PTR_CTE_PTR(target, tcbBuffer);
        e = cteDelete(bufferSlot, true);
        if (e != EXCEPTION_NONE) {
            return e;
        }
        target->tcbIPCBuffer = bufferAddr;
        if (bufferSrcSlot && sameObjectAs(bufferCap, bufferSrcSlot->cap) &&
                sameObjectAs(tCap, slot->cap)) {
            cteInsert(bufferCap, bufferSrcSlot, bufferSlot);
        }
    }

    return EXCEPTION_NONE;
}

exception_t
invokeTCB_CopyRegisters(tcb_t *dest, tcb_t *tcb_src,
                        bool_t suspendSource, bool_t resumeTarget,
                        bool_t transferFrame, bool_t transferInteger,
                        word_t transferArch)
{
    if (suspendSource) {
        suspend(tcb_src);
    }

    if (resumeTarget) {
        restart(dest);
    }

    if (transferFrame) {
        unsigned int i;
        word_t v;
        word_t pc;

        for (i = 0; i < n_frameRegisters; i++) {
            v = getRegister(tcb_src, frameRegisters[i]);
            setRegister(dest, frameRegisters[i], v);
        }

        pc = getRestartPC(dest);
        setNextPC(dest, pc);
    }

    if (transferInteger) {
        unsigned int i;
        word_t v;

        for (i = 0; i < n_gpRegisters; i++) {
            v = getRegister(tcb_src, gpRegisters[i]);
            setRegister(dest, gpRegisters[i], v);
        }
    }

    return Arch_performTransfer(transferArch, tcb_src, dest);
}

/* ReadRegisters is a special case: replyFromKernel & setMRs are
 * unfolded here, in order to avoid passing the large reply message up
 * to the top level in a global (and double-copying). We prevent the
 * top-level replyFromKernel_success_empty() from running by setting the
 * thread state. Retype does this too.
 */
exception_t
invokeTCB_ReadRegisters(tcb_t *tcb_src, bool_t suspendSource,
                        unsigned int n, word_t arch, bool_t call)
{
    unsigned int i, j;
    exception_t e;
    tcb_t *thread;

    thread = ksCurThread;

    if (suspendSource) {
        suspend(tcb_src);
    }

    e = Arch_performTransfer(arch, tcb_src, ksCurThread);
    if (e != EXCEPTION_NONE) {
        return e;
    }

    if (call) {
        word_t *ipcBuffer;

        ipcBuffer = lookupIPCBuffer(true, thread);

        setRegister(thread, badgeRegister, 0);

        for (i = 0; i < n && i < n_frameRegisters && i < n_msgRegisters; i++) {
            setRegister(thread, msgRegisters[i],
                        getRegister(tcb_src, frameRegisters[i]));
        }

        if (ipcBuffer != NULL && i < n && i < n_frameRegisters) {
            for (; i < n && i < n_frameRegisters; i++) {
                ipcBuffer[i + 1] = getRegister(tcb_src, frameRegisters[i]);
            }
        }

        j = i;

        for (i = 0; i < n_gpRegisters && i + n_frameRegisters < n
                && i + n_frameRegisters < n_msgRegisters; i++) {
            setRegister(thread, msgRegisters[i + n_frameRegisters],
                        getRegister(tcb_src, gpRegisters[i]));
        }

        if (ipcBuffer != NULL && i < n_gpRegisters
                && i + n_frameRegisters < n) {
            for (; i < n_gpRegisters && i + n_frameRegisters < n; i++) {
                ipcBuffer[i + n_frameRegisters + 1] =
                    getRegister(tcb_src, gpRegisters[i]);
            }
        }

        setRegister(thread, msgInfoRegister, wordFromMessageInfo(
                        message_info_new(0, 0, 0, i + j)));
    }
    setThreadState(thread, ThreadState_Running);

    return EXCEPTION_NONE;
}

exception_t
invokeTCB_WriteRegisters(tcb_t *dest, bool_t resumeTarget,
                         unsigned int n, word_t arch, word_t *buffer)
{
    unsigned int i;
    word_t pc;
    exception_t e;

    e = Arch_performTransfer(arch, ksCurThread, dest);
    if (e != EXCEPTION_NONE) {
        return e;
    }

    if (n > n_frameRegisters + n_gpRegisters) {
        n = n_frameRegisters + n_gpRegisters;
    }

    for (i = 0; i < n_frameRegisters && i < n; i++) {
        /* Offset of 2 to get past the initial syscall arguments */
        setRegister(dest, frameRegisters[i],
                    sanitiseRegister(frameRegisters[i],
                                     getSyscallArg(i + 2, buffer)));
    }

    for (i = 0; i < n_gpRegisters && i + n_frameRegisters < n; i++) {
        setRegister(dest, gpRegisters[i],
                    sanitiseRegister(gpRegisters[i],
                                     getSyscallArg(i + n_frameRegisters + 2,
                                                   buffer)));
    }

    pc = getRestartPC(dest);
    setNextPC(dest, pc);

    if (resumeTarget) {
        restart(dest);
    }

    return EXCEPTION_NONE;
}

exception_t
invokeTCB_AEPControl(tcb_t *tcb, async_endpoint_t *aepptr)
{
    if (aepptr) {
        bindAsyncEndpoint(tcb, aepptr);
    } else {
        unbindAsyncEndpoint(tcb);
    }

    return EXCEPTION_NONE;
}

#ifdef DEBUG
void
setThreadName(tcb_t *tcb, const char *name)
{
    strlcpy(tcb->tcbName, name, TCB_NAME_LENGTH);
}
#endif
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/object/untyped.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>
#include <types.h>
#include <api/failures.h>
#include <api/syscall.h>
#include <api/invocation.h>
#include <machine/io.h>
#include <object/structures.h>
#include <object/untyped.h>
#include <object/objecttype.h>
#include <object/cnode.h>
#include <kernel/cspace.h>
#include <kernel/thread.h>
#include <util.h>

static word_t
alignUp(word_t baseValue, unsigned int alignment)
{
    return (baseValue + (BIT(alignment) - 1)) & ~MASK(alignment);
}

exception_t
decodeUntypedInvocation(word_t label, unsigned int length, cte_t *slot,
                        cap_t cap, extra_caps_t extraCaps,
                        bool_t call, word_t *buffer)
{
    word_t newType, userObjSize, nodeIndex;
    word_t nodeDepth, nodeOffset, nodeWindow;
    cte_t *rootSlot UNUSED;
    exception_t status;
    cap_t nodeCap;
    lookupSlot_ret_t lu_ret;
    word_t nodeSize;
    unsigned int i;
    slot_range_t slots;
    word_t freeRef, objectSize, untypedSize;
    word_t freeIndex, alignedFreeIndex;

    /* Ensure operation is valid. */
    if (label != UntypedRetype) {
        userError("Untyped cap: Illegal operation attempted.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    /* Ensure message length valid. */
    if (length < 7 || extraCaps.excaprefs[0] == NULL) {
        userError("Untyped invocation: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    /* Fetch arguments. */
    newType     = getSyscallArg(0, buffer);
    freeIndex   = getSyscallArg(1, buffer);
    userObjSize = getSyscallArg(2, buffer);
    nodeIndex   = getSyscallArg(3, buffer);
    nodeDepth   = getSyscallArg(4, buffer);
    nodeOffset  = getSyscallArg(5, buffer);
    nodeWindow  = getSyscallArg(6, buffer);

    rootSlot = extraCaps.excaprefs[0];

    /*
     * Okay to retype if at least one of the following criteria hold:
     *   - the original untyped sits in the kernel window.
     *   - we are retyping to a frame (small, big, whatever).
     *   - we are retyping to an untyped
     */
    if (!(inKernelWindow((void *)cap_untyped_cap_get_capPtr(cap)) ||
            Arch_isFrameType(newType) ||
            newType == seL4_UntypedObject)) {
        userError("Untyped Retype: Untyped outside kernel window (%p).", (void*)newType);
        current_syscall_error.type = seL4_InvalidArgument;
        current_syscall_error.invalidArgumentNumber = 0;
        return EXCEPTION_SYSCALL_ERROR;
    }

    /* Is the requested object type valid? */
    if (newType >= seL4_ObjectTypeCount) {
        userError("Untyped Retype: Invalid object type.");
        current_syscall_error.type = seL4_InvalidArgument;
        current_syscall_error.invalidArgumentNumber = 0;
        return EXCEPTION_SYSCALL_ERROR;
    }

    /* Is the requested object size valid? */
    if (userObjSize >= (wordBits - 1)) {
        userError("Untyped Retype: Invalid object size.");
        current_syscall_error.type = seL4_RangeError;
        current_syscall_error.rangeErrorMin = 0;
        current_syscall_error.rangeErrorMax = wordBits - 2;
        return EXCEPTION_SYSCALL_ERROR;
    }

    /* If the target object is a CNode, is it at least size 1? */
    if (newType == seL4_CapTableObject && userObjSize == 0) {
        userError("Untyped Retype: Requested CapTable size too small.");
        current_syscall_error.type = seL4_InvalidArgument;
        current_syscall_error.invalidArgumentNumber = 1;
        return EXCEPTION_SYSCALL_ERROR;
    }

    /* If the target object is a Untyped, is it at least size 4? */
    if (newType == seL4_UntypedObject && userObjSize < 4) {
        userError("Untyped Retype: Requested UntypedItem size too small.");
        current_syscall_error.type = seL4_InvalidArgument;
        current_syscall_error.invalidArgumentNumber = 1;
        return EXCEPTION_SYSCALL_ERROR;
    }

    /* Lookup the destination CNode (where our caps will be placed in). */
    if (nodeDepth == 0) {
        nodeCap = extraCaps.excaprefs[0]->cap;
    } else {
        cap_t rootCap = extraCaps.excaprefs[0]->cap;
        lu_ret = lookupTargetSlot(rootCap, nodeIndex, nodeDepth);
        if (lu_ret.status != EXCEPTION_NONE) {
            userError("Untyped Retype: Invalid destination address.");
            return lu_ret.status;
        }
        nodeCap = lu_ret.slot->cap;
    }

    /* Is the destination actually a CNode? */
    if (cap_get_capType(nodeCap) != cap_cnode_cap) {
        userError("Untyped Retype: Destination cap invalid or read-only.");
        current_syscall_error.type = seL4_FailedLookup;
        current_syscall_error.failedLookupWasSource = 0;
        current_lookup_fault = lookup_fault_missing_capability_new(nodeDepth);
        return EXCEPTION_SYSCALL_ERROR;
    }

    /* Is the region where the user wants to put the caps valid? */
    nodeSize = 1 << cap_cnode_cap_get_capCNodeRadix(nodeCap);
    if (nodeOffset > nodeSize - 1) {
        userError("Untyped Retype: Destination node offset #%d too large.",
                  (int)nodeOffset);
        current_syscall_error.type = seL4_RangeError;
        current_syscall_error.rangeErrorMin = 0;
        current_syscall_error.rangeErrorMax = nodeSize - 1;
        return EXCEPTION_SYSCALL_ERROR;
    }
    if (nodeWindow < 1 || nodeWindow > CONFIG_RETYPE_FAN_OUT_LIMIT) {
        userError("Untyped Retype: Number of requested objects (%d) too small or large.",
                  (int)nodeWindow);
        current_syscall_error.type = seL4_RangeError;
        current_syscall_error.rangeErrorMin = 1;
        current_syscall_error.rangeErrorMax = CONFIG_RETYPE_FAN_OUT_LIMIT;
        return EXCEPTION_SYSCALL_ERROR;
    }
    if (nodeWindow > nodeSize - nodeOffset) {
        userError("Untyped Retype: Requested destination window overruns size of node.");
        current_syscall_error.type = seL4_RangeError;
        current_syscall_error.rangeErrorMin = 1;
        current_syscall_error.rangeErrorMax = nodeSize - nodeOffset;
        return EXCEPTION_SYSCALL_ERROR;
    }

    /* Ensure that the destination slots are all empty. */
    slots.cnode = CTE_PTR(cap_cnode_cap_get_capCNodePtr(nodeCap));
    slots.offset = nodeOffset;
    slots.length = nodeWindow;
    for (i = nodeOffset; i < nodeOffset + nodeWindow; i++) {
        status = ensureEmptySlot(slots.cnode + i);
        if (status != EXCEPTION_NONE) {
            userError("Untyped Retype: Slot #%d in destination window non-empty.",
                      (int)i);
            return status;
        }
    }

    objectSize = getObjectSize(newType, userObjSize);

    /* Align up the free region so that it is aligned to the target object's
     * size. */
    alignedFreeIndex = alignUp(freeIndex, objectSize);

    freeRef = GET_FREE_REF(cap_untyped_cap_get_capPtr(cap), alignedFreeIndex);

    /* Check that this object will be within the bounds of the untyped */
    untypedSize = BIT(cap_untyped_cap_get_capBlockSize(cap));
    if (objectSize >= wordBits || alignedFreeIndex + BIT(objectSize) > untypedSize) {
        userError("Untyped Retype: Insufficient memory or offset outside untyped");
        current_syscall_error.type = seL4_NotEnoughMemory;
        current_syscall_error.memoryLeft = untypedSize;
        return EXCEPTION_SYSCALL_ERROR;
    }

    /* Check to see if this retype will collide with an existing child. */
    if (newType != seL4_UntypedObject && !cap_untyped_cap_get_capDeviceMemory(cap)) {
        cte_t *child = cdtFindTypedInRange(freeRef, nodeWindow * objectSize);
        if (child) {
            userError("Untyped Retype: collision with existing child");
            current_syscall_error.type = seL4_RevokeFirst;
            return EXCEPTION_SYSCALL_ERROR;
        }
    }

    /* Check we do not create non frames in frame only untypeds */
    if ( (cap_untyped_cap_get_capDeviceMemory(cap) && !Arch_isFrameType(newType))
            && newType != seL4_UntypedObject) {
        userError("Untyped Retype: Creating kernel objects with frame only untyped");
        current_syscall_error.type = seL4_InvalidArgument;
        current_syscall_error.invalidArgumentNumber = 1;
        return EXCEPTION_SYSCALL_ERROR;
    }

    /* Perform the retype. */
    setThreadState(ksCurThread, ThreadState_Restart);
    return invokeUntyped_Retype(
               slot, WORD_PTR(cap_untyped_cap_get_capPtr(cap)),
               (void*)freeRef, newType, userObjSize, slots, call, cap_untyped_cap_get_capDeviceMemory(cap));
}

exception_t
invokeUntyped_Retype(cte_t *srcSlot, void* regionBase,
                     void* freeRegionBase,
                     object_t newType, unsigned int userSize,
                     slot_range_t destSlots, bool_t call, bool_t deviceMemory)
{
    word_t size_ign UNUSED;

    /*
     * If this is the first object we are creating in this untyped region, we
     * need to detype the old memory. At the concrete C level, this doesn't
     * have any effect, but updating this shadow state is important for the
     * verification process.
     */
    size_ign = cap_untyped_cap_ptr_get_capBlockSize(&(srcSlot->cap));
    /** AUXUPD: "(True,
        if (\<acute>freeRegionBase = \<acute>regionBase) then
          (typ_region_bytes (ptr_val \<acute>regionBase) (unat \<acute>size_ign))
        else
          id)" */
    /** GHOSTUPD: "(True,
        if (\<acute>freeRegionBase = \<acute>regionBase) then
          (gs_clear_region (ptr_val \<acute>regionBase) (unat \<acute>size_ign))
        else
          id)" */

    /* Create new objects and caps. */
    createNewObjects(newType, srcSlot, destSlots, freeRegionBase, userSize, deviceMemory);

    return EXCEPTION_NONE;
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/plat/pc99/machine/acpi.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>
#include <util.h>
#include <assert.h>
#include <machine/io.h>
#include <arch/linker.h>
#include <plat/machine.h>
#include <plat/machine/acpi.h>
#include <plat/machine/devices.h>

#define get_dev_id(bus, dev, fun) (((bus) << 8) | ((dev) << 3) | (fun))

enum acpi_type {
    ACPI_RSDP,
    ACPI_RSDT
};

/* Root System Descriptor Pointer */
typedef struct acpi_rsdp {
    char         signature[8];
    uint8_t      checksum;
    char         oem_id[6];
    uint8_t      revision;
    uint32_t     rsdt_address;
    uint32_t     length;
    uint32_t     xsdt_address[2];
    uint8_t      extended_checksum;
    char         reserved[3];
} acpi_rsdp_t;
compile_assert(acpi_rsdp_packed, sizeof(acpi_rsdp_t) == 36)

/* DMA Remapping Reporting Table */
typedef struct acpi_dmar {
    acpi_header_t header;
    uint8_t       host_addr_width;
    uint8_t       flags;
    uint8_t       reserved[10];
} acpi_dmar_t;
compile_assert(acpi_dmar_packed,
               sizeof(acpi_dmar_t) == sizeof(acpi_header_t) + 12)

/* DMA Remapping Structure Header */
typedef struct acpi_dmar_header {
    uint16_t type;
    uint16_t length;
} acpi_dmar_header_t;
compile_assert(acpi_dmar_header_packed, sizeof(acpi_dmar_header_t) == 4)

/* DMA Remapping Structure Types */
enum acpi_table_dmar_struct_type {
    DMAR_DRHD = 0,
    DMAR_RMRR = 1,
    DMAR_ATSR = 2,
};

/* DMA Remapping Hardware unit Definition */
typedef struct acpi_dmar_drhd {
    acpi_dmar_header_t header;
    uint8_t            flags;
    uint8_t            reserved;
    uint16_t           segment;
    uint32_t           reg_base[2];
} acpi_dmar_drhd_t;
compile_assert(acpi_dmar_drhd_packed,
               sizeof(acpi_dmar_drhd_t) == sizeof(acpi_dmar_header_t) + 12)

/* Reserved Memory Region Reporting structure Definition */
typedef struct acpi_dmar_devscope {
    uint8_t  type;
    uint8_t  length;
    uint16_t reserved;
    uint8_t  enum_id;
    uint8_t  start_bus;
    struct {
        uint8_t dev;
        uint8_t fun;
    } path_0;
} acpi_dmar_devscope_t;
compile_assert(acpi_dmar_devscope_packed, sizeof(acpi_dmar_devscope_t) == 8)

/* Reserved Memory Region Reporting structure Definition */
typedef struct acpi_dmar_rmrr {
    acpi_dmar_header_t   header;
    uint16_t             reserved;
    uint16_t             segment;
    uint32_t             reg_base[2];
    uint32_t             reg_limit[2];
    acpi_dmar_devscope_t devscope_0;
} acpi_dmar_rmrr_t;
compile_assert(acpi_dmar_rmrr_packed, sizeof(acpi_dmar_rmrr_t) ==
               sizeof(acpi_dmar_header_t) + 20 + sizeof(acpi_dmar_devscope_t))

/* Multiple APIC Description Table (MADT) */
typedef struct acpi_madt {
    acpi_header_t header;
    uint32_t      apic_addr;
    uint32_t      flags;
} acpi_madt_t;
compile_assert(acpi_madt_packed,
               sizeof(acpi_madt_t) == sizeof(acpi_header_t) + 8)

typedef struct acpi_madt_header {
    uint8_t type;
    uint8_t length;
} acpi_madt_header_t;
compile_assert(acpi_madt_header_packed, sizeof(acpi_madt_header_t) == 2)

enum acpi_table_madt_struct_type {
    MADT_APIC   = 0,
    MADT_IOAPIC = 1,
    MADT_ISO    = 2,
};

typedef struct acpi_madt_apic {
    acpi_madt_header_t header;
    uint8_t            cpu_id;
    uint8_t            apic_id;
    uint32_t           flags;
} acpi_madt_apic_t;
compile_assert(acpi_madt_apic_packed,
               sizeof(acpi_madt_apic_t) == sizeof(acpi_madt_header_t) + 6)

typedef struct acpi_madt_ioapic {
    acpi_madt_header_t header;
    uint8_t            ioapic_id;
    uint8_t            reserved[1];
    uint32_t           ioapic_addr;
    uint32_t           gsib;
} acpi_madt_ioapic_t;
compile_assert(acpi_madt_ioapic_packed,
               sizeof(acpi_madt_ioapic_t) == sizeof(acpi_madt_header_t) + 10)

typedef struct acpi_madt_iso {
    acpi_madt_header_t header;
    uint8_t            bus; /* always 0 (ISA) */
    uint8_t            source;
    uint32_t           gsi;
    uint16_t           flags;
} acpi_madt_iso_t;
/* We can't assert on the sizeof acpi_madt_iso because it contains trailing
 * padding.
 */
compile_assert(acpi_madt_iso_packed,
               OFFSETOF(acpi_madt_iso_t, flags) == sizeof(acpi_madt_header_t) + 6)

/* workaround because string literals are not supported by C parser */
const char acpi_str_rsd[]  = {'R', 'S', 'D', ' ', 'P', 'T', 'R', ' ', 0};
const char acpi_str_apic[] = {'A', 'P', 'I', 'C', 0};

BOOT_CODE static uint8_t
acpi_calc_checksum(char* start, uint32_t length)
{
    uint8_t checksum = 0;

    while (length > 0) {
        checksum += *start;
        start++;
        length--;
    }
    return checksum;
}

BOOT_CODE static acpi_rsdp_t*
acpi_get_rsdp(void)
{
    char* addr;

    for (addr = (char*)BIOS_PADDR_START; addr < (char*)BIOS_PADDR_END; addr += 16) {
        if (strncmp(addr, acpi_str_rsd, 8) == 0) {
            if (acpi_calc_checksum(addr, 20) == 0) {
                return (acpi_rsdp_t*)addr;
            }
        }
    }
    return NULL;
}

void* acpi_table_init(void* entry, enum acpi_type table_type);
BOOT_CODE void*
acpi_table_init(void* entry, enum acpi_type table_type)
{
    void* acpi_table;
    unsigned int pages_for_table;
    unsigned int pages_for_header = 1;

    /* if we need to map another page to read header */
    uint32_t offset_in_page = (uint32_t)entry & MASK(LARGE_PAGE_BITS);
    if (MASK(LARGE_PAGE_BITS) - offset_in_page < sizeof(acpi_rsdp_t)) {
        pages_for_header++;
    }

    /* map in table's header */
    acpi_table = map_temp_boot_page(entry, pages_for_header);

    switch (table_type) {
    case ACPI_RSDP: {
        acpi_rsdp_t *rsdp_entry = (acpi_rsdp_t*)entry;
        pages_for_table = (rsdp_entry->length + offset_in_page) / MASK(LARGE_PAGE_BITS) + 1;
        break;
    }
    case ACPI_RSDT: { // RSDT, MADT, DMAR etc.
        acpi_rsdt_t *rsdt_entry = (acpi_rsdt_t*)entry;
        pages_for_table = (rsdt_entry->header.length + offset_in_page) / MASK(LARGE_PAGE_BITS) + 1;
        break;
    }
    default:
        printf("Error: Mapping unknown ACPI table type\n");
        assert(false);
        return NULL;
    }

    /* map in full table */
    acpi_table = map_temp_boot_page(entry, pages_for_table);

    return acpi_table;
}

BOOT_CODE acpi_rsdt_t*
acpi_init(void)
{
    acpi_rsdp_t* acpi_rsdp = acpi_get_rsdp();
    acpi_rsdt_t* acpi_rsdt;
    acpi_rsdt_t* acpi_rsdt_mapped;

    if (acpi_rsdp == NULL) {
        printf("BIOS: No ACPI support detected\n");
        return NULL;
    }
    printf("ACPI: RSDP paddr=0x%x\n", (unsigned int)acpi_rsdp);
    acpi_rsdp = acpi_table_init(acpi_rsdp, ACPI_RSDP);
    printf("ACPI: RSDP vaddr=0x%x\n", (unsigned int)acpi_rsdp);

    acpi_rsdt = (acpi_rsdt_t*)acpi_rsdp->rsdt_address;
    printf("ACPI: RSDT paddr=0x%x\n", (unsigned int)acpi_rsdt);
    acpi_rsdt_mapped = (acpi_rsdt_t*)acpi_table_init(acpi_rsdt, ACPI_RSDT);
    printf("ACPI: RSDT vaddr=0x%x\n", (unsigned int)acpi_rsdt_mapped);

    assert(acpi_rsdt_mapped->header.length > 0);
    if (acpi_calc_checksum((char*)acpi_rsdt_mapped, acpi_rsdt_mapped->header.length) != 0) {
        printf("ACPI: RSDT checksum failure\n");
        return NULL;
    }

    return acpi_rsdt;
}

BOOT_CODE uint32_t
acpi_madt_scan(
    acpi_rsdt_t* acpi_rsdt,
    cpu_id_t*    cpu_list,
    uint32_t     max_list_len,
    uint32_t*    num_ioapic,
    paddr_t*     ioapic_paddrs
)
{
    unsigned int entries;
    uint32_t            num_cpu;
    uint32_t            count;
    acpi_madt_t*        acpi_madt;
    acpi_madt_header_t* acpi_madt_header;

    acpi_rsdt_t* acpi_rsdt_mapped;
    acpi_madt_t* acpi_madt_mapped;
    acpi_rsdt_mapped = (acpi_rsdt_t*)acpi_table_init(acpi_rsdt, ACPI_RSDT);

    num_cpu = 0;
    *num_ioapic = 0;

    assert(acpi_rsdt_mapped->header.length >= sizeof(acpi_header_t));
    entries = (acpi_rsdt_mapped->header.length - sizeof(acpi_header_t)) / sizeof(acpi_header_t*);
    for (count = 0; count < entries; count++) {
        acpi_madt = (acpi_madt_t*)acpi_rsdt_mapped->entry[count];
        acpi_madt_mapped = (acpi_madt_t*)acpi_table_init(acpi_madt, ACPI_RSDT);

        if (strncmp(acpi_str_apic, acpi_madt_mapped->header.signature, 4) == 0) {
            printf("ACPI: MADT paddr=0x%x\n", (unsigned int)acpi_madt);
            printf("ACPI: MADT vaddr=0x%x\n", (unsigned int)acpi_madt_mapped);
            printf("ACPI: MADT apic_addr=0x%x\n", acpi_madt_mapped->apic_addr);
            printf("ACPI: MADT flags=0x%x\n", acpi_madt_mapped->flags);

            acpi_madt_header = (acpi_madt_header_t*)(acpi_madt_mapped + 1);

            while ((char*)acpi_madt_header < (char*)acpi_madt_mapped + acpi_madt_mapped->header.length) {
                switch (acpi_madt_header->type) {
                case MADT_APIC: {
                    /* what Intel calls apic_id is what is called cpu_id in seL4! */
                    uint8_t  cpu_id = ((acpi_madt_apic_t*)acpi_madt_header)->apic_id;
                    uint32_t flags  = ((acpi_madt_apic_t*)acpi_madt_header)->flags;
                    if (flags == 1) {
                        printf("ACPI: MADT_APIC apic_id=0x%x\n", cpu_id);
                        if (num_cpu < max_list_len) {
                            cpu_list[num_cpu] = cpu_id;
                        }
                        num_cpu++;
                    }
                    break;
                }
                case MADT_IOAPIC:
                    printf(
                        "ACPI: MADT_IOAPIC ioapic_id=%d ioapic_addr=0x%x gsib=%d\n",
                        ((acpi_madt_ioapic_t*)acpi_madt_header)->ioapic_id,
                        ((acpi_madt_ioapic_t*)acpi_madt_header)->ioapic_addr,
                        ((acpi_madt_ioapic_t*)acpi_madt_header)->gsib
                    );
                    if (*num_ioapic == CONFIG_MAX_NUM_IOAPIC) {
                        printf("ACPI: Not recording this IOAPIC, only support %d\n", CONFIG_MAX_NUM_IOAPIC);
                    } else {
                        ioapic_paddrs[*num_ioapic] = ((acpi_madt_ioapic_t*)acpi_madt_header)->ioapic_addr;
                        (*num_ioapic)++;
                    }
                    break;
                case MADT_ISO:
                    printf("ACIP: MADT_ISO bus=%d source=%d gsi=%d flags=0x%x\n",
                           ((acpi_madt_iso_t*)acpi_madt_header)->bus,
                           ((acpi_madt_iso_t*)acpi_madt_header)->source,
                           ((acpi_madt_iso_t*)acpi_madt_header)->gsi,
                           ((acpi_madt_iso_t*)acpi_madt_header)->flags);
                    break;
                default:
                    break;
                }
                acpi_madt_header = (acpi_madt_header_t*)((char*)acpi_madt_header + acpi_madt_header->length);
            }
        }
    }

    printf("ACPI: %d CPU(s) detected\n", num_cpu);

    return num_cpu;
}

#ifdef CONFIG_IOMMU

BOOT_CODE void
acpi_dmar_scan(
    acpi_rsdt_t* acpi_rsdt,
    paddr_t*     drhu_list,
    uint32_t*    num_drhu,
    uint32_t     max_drhu_list_len,
    acpi_rmrr_list_t *rmrr_list
)
{
    unsigned int i;
    unsigned int entries;
    uint32_t count;
    uint32_t reg_basel, reg_baseh;
    int rmrr_count;
    dev_id_t dev_id;

    acpi_dmar_t*          acpi_dmar;
    acpi_dmar_header_t*   acpi_dmar_header;
    acpi_dmar_rmrr_t*     acpi_dmar_rmrr;
    acpi_dmar_devscope_t* acpi_dmar_devscope;

    acpi_rsdt_t* acpi_rsdt_mapped;
    acpi_dmar_t* acpi_dmar_mapped;

    acpi_rsdt_mapped = (acpi_rsdt_t*)acpi_table_init(acpi_rsdt, ACPI_RSDT);

    *num_drhu = 0;
    rmrr_count = 0;

    assert(acpi_rsdt_mapped->header.length >= sizeof(acpi_header_t));
    entries = (acpi_rsdt_mapped->header.length - sizeof(acpi_header_t)) / sizeof(acpi_header_t*);
    for (count = 0; count < entries; count++) {
        acpi_dmar = (acpi_dmar_t*)acpi_rsdt_mapped->entry[count];
        acpi_dmar_mapped = (acpi_dmar_t*)acpi_table_init(acpi_dmar, ACPI_RSDT);

        if (strncmp("DMAR", acpi_dmar_mapped->header.signature, 4) == 0) {
            printf("ACPI: DMAR paddr=0x%x\n", (unsigned int)acpi_dmar);
            printf("ACPI: DMAR vaddr=0x%x\n", (unsigned int)acpi_dmar_mapped);
            printf("ACPI: IOMMU host address width: %d\n", acpi_dmar_mapped->host_addr_width + 1);
            acpi_dmar_header = (acpi_dmar_header_t*)(acpi_dmar_mapped + 1);

            while ((char*)acpi_dmar_header < (char*)acpi_dmar_mapped + acpi_dmar_mapped->header.length) {
                switch (acpi_dmar_header->type) {

                case DMAR_DRHD:
                    if (*num_drhu == max_drhu_list_len) {
                        printf("ACPI: too many IOMMUs, disabling IOMMU support\n");
                        /* try to increase MAX_NUM_DRHU in config.h */
                        *num_drhu = 0; /* report zero IOMMUs */
                        return;
                    }
                    reg_basel = ((acpi_dmar_drhd_t*)acpi_dmar_header)->reg_base[0];
                    reg_baseh = ((acpi_dmar_drhd_t*)acpi_dmar_header)->reg_base[1];
                    /* check if value fits into uint32_t */
                    if (reg_baseh != 0) {
                        printf("ACPI: DMAR_DRHD reg_base exceeds 32 bit, disabling IOMMU support\n");
                        /* try to make BIOS map it below 4G */
                        *num_drhu = 0; /* report zero IOMMUs */
                        return;
                    }
                    drhu_list[*num_drhu] = (paddr_t)reg_basel;
                    (*num_drhu)++;
                    break;

                case DMAR_RMRR:
                    /* loop through all device scopes of this RMRR */
                    acpi_dmar_rmrr = (acpi_dmar_rmrr_t*)acpi_dmar_header;
                    if (acpi_dmar_rmrr->reg_base[1] != 0 ||
                            acpi_dmar_rmrr->reg_limit[1] != 0) {
                        printf("ACPI: RMRR device above 4GiB, disabling IOMMU support\n");
                        *num_drhu = 0;
                        return ;
                    }

                    printf("ACPI: RMRR providing region 0x%x-0x%x\n", acpi_dmar_rmrr->reg_base[0], acpi_dmar_rmrr->reg_limit[0]);

                    for (i = 0; i <= (acpi_dmar_header->length - sizeof(acpi_dmar_rmrr_t)) / sizeof(acpi_dmar_devscope_t); i++) {
                        acpi_dmar_devscope = &acpi_dmar_rmrr->devscope_0 + i;

                        if (acpi_dmar_devscope->type != 1) {
                            /* FIXME - bugzilla bug 170 */
                            printf("ACPI: RMRR device scope: non-PCI-Endpoint-Devices not supported yet, disabling IOMMU support\n");
                            *num_drhu = 0; /* report zero IOMMUs */
                            return;
                        }

                        if (acpi_dmar_devscope->length > sizeof(acpi_dmar_devscope_t)) {
                            /* FIXME - bugzilla bug 170 */
                            printf("ACPI: RMRR device scope: devices behind bridges not supported yet, disabling IOMMU support\n");
                            *num_drhu = 0; /* report zero IOMMUs */
                            return;
                        }

                        dev_id =
                            get_dev_id(
                                acpi_dmar_devscope->start_bus,
                                acpi_dmar_devscope->path_0.dev,
                                acpi_dmar_devscope->path_0.fun
                            );

                        if (rmrr_count == CONFIG_MAX_RMRR_ENTRIES) {
                            printf("ACPI: Too many RMRR entries, disabling IOMMU support\n");
                            *num_drhu = 0;
                            return;
                        }
                        printf("\tACPI: registering RMRR entry for region for device: bus=0x%x dev=0x%x fun=0x%x\n",
                               acpi_dmar_devscope->start_bus,
                               acpi_dmar_devscope->path_0.dev,
                               acpi_dmar_devscope->path_0.fun
                              );

                        rmrr_list->entries[rmrr_count].device = dev_id;
                        rmrr_list->entries[rmrr_count].base = acpi_dmar_rmrr->reg_base[0];
                        rmrr_list->entries[rmrr_count].limit = acpi_dmar_rmrr->reg_limit[0];
                        rmrr_count++;
                    }
                    break;

                case DMAR_ATSR:
                    /* not implemented yet */
                    break;

                default:
                    printf("ACPI: Unknown DMA remapping structure type: %x\n", acpi_dmar_header->type);
                }
                acpi_dmar_header = (acpi_dmar_header_t*)((char*)acpi_dmar_header + acpi_dmar_header->length);
            }
        }
    }
    rmrr_list->num = rmrr_count;
    printf("ACPI: %d IOMMUs detected\n", *num_drhu);
}

#endif /* IOMMU */
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/plat/pc99/machine/debug_helpers.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#if defined DEBUG || defined RELEASE_PRINTF

#include <arch/model/statedata.h>
#include <plat/machine/debug_helpers.h>
#include <plat/machine/io.h>

#define DEBUG_PORT ia32KSdebugPort

unsigned char getDebugChar(void)
{
    while ((in8(DEBUG_PORT + 5) & 1) == 0);
    return in8(DEBUG_PORT);
}

void putDebugChar(unsigned char a)
{
    while ((in8(DEBUG_PORT + 5) & 0x20) == 0);
    out8(DEBUG_PORT, a);
}

#endif
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/plat/pc99/machine/hardware.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>
#include <machine/io.h>
#include <arch/kernel/apic.h>
#include <arch/model/statedata.h>
#include <arch/linker.h>
#include <plat/machine/pic.h>
#include <plat/machine/ioapic.h>
#include <plat/machine.h>

#ifdef CONFIG_IOMMU
#include <plat/machine/intel-vtd.h>
#endif

/* ============================== interrupts/IRQs ============================== */

/* Enable or disable irq according to the 'mask' flag. */
void maskInterrupt(bool_t mask, irq_t irq)
{
    assert(irq >= irq_controller_min);
    assert(irq <= maxIRQ);

    if (irq <= irq_controller_max) {
#ifdef CONFIG_IRQ_IOAPIC
        ioapic_mask_irq(mask, irq);
#else
        pic_mask_irq(mask, irq);
#endif
    } else {
        /* we can't mask/unmask specific APIC vectors (e.g. MSIs/IPIs) */
    }
}

/* Set mode of an irq */
void setInterruptMode(irq_t irq, bool_t levelTrigger, bool_t polarityLow)
{
#ifdef CONFIG_IRQ_IOAPIC
    assert(irq >= irq_ioapic_min);
    assert(irq <= maxIRQ);

    if (irq <= irq_ioapic_max) {
        ioapic_set_mode(irq, levelTrigger, polarityLow);
    } else {
        /* No mode setting for specific APIC vectors */
    }
#endif
}

/* Handle a platform-reserved IRQ. */
void handleReservedIRQ(irq_t irq)
{
#ifdef CONFIG_IOMMU
    if (irq == irq_iommu) {
        vtd_handle_fault();
        return;
    }
#endif
    printf("Received reserved IRQ: %d\n", (int)irq);
}

/* Get the IRQ number currently working on. */
irq_t getActiveIRQ(void)
{
    if (ia32KScurInterrupt == int_invalid) {
        return irqInvalid;
    } else {
        return ia32KScurInterrupt - IRQ_INT_OFFSET;
    }
}

/* Checks for pending IRQ */
bool_t isIRQPending(void)
{
    if (apic_is_interrupt_pending()) {
        return true;
    }
#ifdef CONFIG_IRQ_PIC
    if (pic_is_irq_pending()) {
        return true;
    }
#endif
    return false;
}

void ackInterrupt(irq_t irq)
{
#ifdef CONFIG_IRQ_PIC
    if (irq <= irq_isa_max) {
        pic_ack_active_irq();
    } else
#endif
    {
        apic_ack_active_interrupt();
    }
}

void handleSpuriousIRQ(void)
{
    /* Do nothing */
}

/* ============================== timer ============================== */

void resetTimer(void)
{
    /* not necessary */
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/plat/pc99/machine/intel-vtd.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>

#ifdef CONFIG_IOMMU

#include <kernel/boot.h>
#include <machine.h>
#include <machine/io.h>
#include <arch/kernel/apic.h>
#include <arch/model/statedata.h>
#include <arch/linker.h>
#include <plat/machine/acpi.h>
#include <plat/machine/intel-vtd.h>
#include <util.h>

#define RTADDR_REG  0x20
#define GCMD_REG    0x18
#define GSTS_REG    0x1C
#define CCMD_REG    0x28
#define ECAP_REG    0x10
#define IOTLB_REG   0x08
#define FSTS_REG    0x34
#define FECTL_REG   0x38
#define FEDATA_REG  0x3C
#define FEADDR_REG  0x40
#define FEUADDR_REG 0x44
#define CAP_REG     0x08

/* Bit Positions within Registers */
#define SRTP        30  /* Set Root Table Pointer */
#define RTPS        30  /* Root Table Pointer Status */
#define TE          31  /* Translation Enable */
#define TES         31  /* Translation Enable Status */

/* ICC is 63rd bit in CCMD_REG, but since we will be
 * accessing this register as 4 byte word, ICC becomes
 * 31st bit in the upper 32bit word.
 */
#define ICC         31  /* Invalidate Context Cache */
#define CIRG        29  /* Context Invalidation Request Granularity */
#define CAIG        27  /* Context Actual Invalidation Granularity */
#define CAIG_MASK   0x3
#define IVO_MASK    0x3FF
#define IVT         31  /* Invalidate IOTLB */
#define IIRG        28  /* IOTLB Invalidation Request Granularity */
#define IAIG        25  /* IOTLB Actual Invalidation Granularity */
#define IAIG_MASK   0x7
#define IP          30  /* Interrupt Pending */
#define FRI         0x8 /* Fault Recording Index */
#define FRI_MASK    0xFF
#define FRO         24
#define FRO_MASK    0xFF
#define FI          12
#define SID_MASK    0xFFFF
#define FR_MASK     0xFF
#define FAULT_TYPE  30
#define FAULT       31
#define NFR         8   /* high word of CAP_REG */
#define NFR_MASK    0xff
#define PPF         1
#define PPF_MASK    1
#define PRESENT     1
#define WBF         27
#define WBFS        27
#define DID         8
#define RW          0x3

#define SAGAW         8
#define SAGAW_2_LEVEL 0x01
#define SAGAW_3_LEVEL 0x02
#define SAGAW_4_LEVEL 0x04
#define SAGAW_5_LEVEL 0x08
#define SAGAW_6_LEVEL 0x10

#define CONTEXT_GLOBAL_INVALIDATE 0x1
#define IOTLB_GLOBAL_INVALIDATE   0x1

#define DMA_TLB_READ_DRAIN  (1 << 17)
#define DMA_TLB_WRITE_DRAIN (1 << 16)

typedef uint32_t drhu_id_t;

static inline uint32_t vtd_read32(drhu_id_t drhu_id, uint32_t offset)
{
    return *(volatile uint32_t*)(PPTR_DRHU_START + (drhu_id << PAGE_BITS) + offset);
}

static inline void vtd_write32(drhu_id_t drhu_id, uint32_t offset, uint32_t value)
{
    *(volatile uint32_t*)(PPTR_DRHU_START + (drhu_id << PAGE_BITS) + offset) = value;
}

static inline uint32_t get_ivo(drhu_id_t drhu_id)
{
    return ((vtd_read32(drhu_id, ECAP_REG) >> 8) & IVO_MASK) * 16;
}

static inline int supports_passthrough(drhu_id_t drhu_id)
{
    return (vtd_read32(drhu_id, ECAP_REG) >> 6) & 1;
}

static uint32_t get_fro_offset(drhu_id_t drhu_id)
{
    uint32_t fro_offset;

    /* Get bits 31 to 24 from lower Capability Register */
    fro_offset = (vtd_read32(drhu_id, CAP_REG) >> FRO) & FRO_MASK;

    /* Get bits 33 to 32 from higher Capability Register */
    fro_offset |= (vtd_read32(drhu_id, CAP_REG + 4) & 0x3) << 8;

    return fro_offset << 4;
}

void invalidate_context_cache(void)
{
    /* FIXME - bugzilla bug 172
     * 1. Instead of assuming global invalidation, this function should
     *    accept a parameter to control the granularity of invalidation
     *    request.
     * 2. Instead of doing invalidation for all the IOMMUs, it should
     *    only do it for the IOMMU responsible for the requesting PCI
     *    device.
     */

    uint8_t   invalidate_command = CONTEXT_GLOBAL_INVALIDATE;
    uint32_t  ccmd_reg_upper;
    drhu_id_t i;

    for (i = 0; i < ia32KSnumDrhu; i++) {
        /* Wait till ICC bit is clear */
        while ((vtd_read32(i, CCMD_REG + 4) >> ICC) & 1);

        /* Program CIRG for Global Invalidation by setting bit 61 which
         * will be bit 29 in upper 32 bits of CCMD_REG
         */
        ccmd_reg_upper = invalidate_command << CIRG;

        /* Invalidate Context Cache */
        ccmd_reg_upper |= (1U << ICC);
        vtd_write32(i, CCMD_REG, 0);
        vtd_write32(i, CCMD_REG + 4, ccmd_reg_upper);

        /* Wait for the invalidation to complete */
        while ((vtd_read32(i, CCMD_REG + 4) >> ICC) & 1);
    }
}

void invalidate_iotlb(void)
{
    /* FIXME - bugzilla bug 172
     * 1. Instead of assuming global invalidation, this function should
     *    accept a parameter to control the granularity of invalidation
     *    request.
     * 2. Instead of doing invalidation for all the IOMMUs, it should
     *    only do it for the IOMMU responsible for the requesting PCI
     *    device.
     */

    uint8_t   invalidate_command = IOTLB_GLOBAL_INVALIDATE;
    uint32_t  iotlb_reg_upper;
    uint32_t  ivo_offset;
    drhu_id_t i;

    for (i = 0; i < ia32KSnumDrhu; i++) {
        ivo_offset = get_ivo(i);

        /* Wait till IVT bit is clear */
        while ((vtd_read32(i, ivo_offset + IOTLB_REG + 4) >> IVT) & 1);

        /* Program IIRG for Global Invalidation by setting bit 60 which
         * will be bit 28 in upper 32 bits of IOTLB_REG
         */
        iotlb_reg_upper = invalidate_command << IIRG;

        /* Invalidate IOTLB */
        iotlb_reg_upper |= (1U << IVT);
        iotlb_reg_upper |= DMA_TLB_READ_DRAIN | DMA_TLB_WRITE_DRAIN;

        vtd_write32(i, ivo_offset + IOTLB_REG, 0);
        vtd_write32(i, ivo_offset + IOTLB_REG + 4, iotlb_reg_upper);

        /* Wait for the invalidation to complete */
        while ((vtd_read32(i, ivo_offset + IOTLB_REG + 4) >> IVT) & 1);
    }
}

static void vtd_clear_fault(drhu_id_t i, word_t fr_reg)
{
    /* Clear the 'F' (Fault) bit to indicate that this fault is processed */
    vtd_write32(i, fr_reg + 12, BIT(FAULT));
}

static void vtd_process_faults(drhu_id_t i)
{
    /* Fault Recording register offset relative to the base register */
    uint32_t fro_offset;
    uint32_t source_id UNUSED;
    uint32_t fault_type UNUSED;
    uint32_t address[2] UNUSED;
    uint32_t reason UNUSED;
    uint32_t num_fault_regs;
    uint32_t fr_reg;
    uint32_t fault_status;
    uint32_t fault_record_index;

    /* Retrieves FRO by looking into Capability register bits 33 to 24 */
    fro_offset = get_fro_offset(i);
    fault_status = (vtd_read32(i, FSTS_REG) >> PPF) & PPF_MASK;

    if (fault_status) {
        num_fault_regs = ((vtd_read32(i, CAP_REG + 4) >> NFR) & NFR_MASK) + 1;
        fault_record_index = (vtd_read32(i, FSTS_REG) >> FRI) & FRI_MASK;
        fr_reg = fro_offset + 16 * fault_record_index;

        /* Traverse the fault register ring buffer */
        do {
            source_id = vtd_read32(i, fr_reg + 8) & SID_MASK;

            fault_type = (vtd_read32(i, fr_reg + 12) >> FAULT_TYPE) & 1;
            address[1] = vtd_read32(i, fr_reg + 4);
            address[0] = vtd_read32(i, fr_reg);
            reason = vtd_read32(i, fr_reg + 12) & FR_MASK;

            printf("IOMMU: DMA %s page fault ", fault_type ? "read" : "write");
            printf("from bus/dev/fun 0x%x ", source_id);
            printf("on address 0x%x:%x ", address[1], address[0]);
            printf("with reason code 0x%x\n", reason);

            vtd_clear_fault(i, fr_reg);

            fault_record_index = (fault_record_index + 1) % num_fault_regs;
            fr_reg = fro_offset + 16 * fault_record_index;
        } while ((vtd_read32(i, fr_reg + 12) >> FAULT) & 1);

        /* Check for Primary Fault Overflow */
        if (vtd_read32(i, FSTS_REG) & 1) {
            /* Clear PFO bit, so new faults will be generated again ! */
            vtd_write32(i, FSTS_REG, 1);
        }
    }
}

void vtd_handle_fault(void)
{
    drhu_id_t i;

    for (i = 0; i < ia32KSnumDrhu; i++) {
        vtd_process_faults(i);
    }
}

BOOT_CODE static void
vtd_create_root_table(void)
{
    ia32KSvtdRootTable = (void*)alloc_region(VTD_RT_SIZE_BITS);
    memzero((void*)ia32KSvtdRootTable, 1 << VTD_RT_SIZE_BITS);
}

/* This function is a simplistic duplication of some of the logic
 * in iospace.c
 */
BOOT_CODE static void
vtd_map_reserved_page(vtd_cte_t *vtd_context_table, int context_index, paddr_t addr)
{
    int i;
    vtd_pte_t *iopt;
    vtd_pte_t *vtd_pte_slot;
    /* first check for the first page table */
    vtd_cte_t *vtd_context_slot = vtd_context_table + context_index;
    if (!vtd_cte_ptr_get_present(vtd_context_slot)) {
        iopt = (vtd_pte_t*)alloc_region(VTD_PT_SIZE_BITS);
        if (!iopt) {
            fail("Failed to allocate IO page table");
        }
        memzero(iopt, 1 << VTD_PT_SIZE_BITS);
        flushCacheRange(iopt, VTD_PT_SIZE_BITS);

        vtd_cte_ptr_new(
            vtd_context_slot,
            ia32KSFirstValidIODomain, /* Domain ID                              */
            0,                        /* CTE Depth. Ignored as reserved mapping */
            true,                     /* RMRR Mapping                           */
            ia32KSnumIOPTLevels - 2,  /* Address Width                          */
            pptr_to_paddr(iopt),      /* Address Space Root                     */
            0,                        /* Translation Type                       */
            true);                    /* Present                                */
        ia32KSFirstValidIODomain++;
        flushCacheRange(vtd_context_slot, VTD_CTE_SIZE_BITS);
    } else {
        iopt = (vtd_pte_t*)paddr_to_pptr(vtd_cte_ptr_get_asr(vtd_context_slot));
    }
    /* now recursively find and map page tables */
    for (i = ia32KSnumIOPTLevels - 1; i >= 0; i--) {
        uint32_t iopt_index;
        /* If we are still looking up bits beyond the 32bit of physical
         * that we support then we select entry 0 in the current PT */
        if (VTD_PT_BITS * i >= 32) {
            iopt_index = 0;
        } else {
            iopt_index = ( (addr >> IA32_4K_bits) >> (VTD_PT_BITS * i)) & MASK(VTD_PT_BITS);
        }
        vtd_pte_slot = iopt + iopt_index;
        if (i == 0) {
            /* Now put the mapping in */
            vtd_pte_ptr_new(vtd_pte_slot, addr, 0, 1, 1);
            flushCacheRange(vtd_pte_slot, VTD_PTE_SIZE_BITS);
        } else {
            if (!vtd_pte_ptr_get_write(vtd_pte_slot)) {
                iopt = (vtd_pte_t*)alloc_region(VTD_PT_SIZE_BITS);
                if (!iopt) {
                    fail("Failed to allocate IO page table");
                }
                memzero(iopt, 1 << VTD_PT_SIZE_BITS);
                flushCacheRange(iopt, VTD_PT_SIZE_BITS);

                vtd_pte_ptr_new(vtd_pte_slot, pptr_to_paddr(iopt), 0, 1, 1);
                flushCacheRange(vtd_pte_slot, VTD_PTE_SIZE_BITS);
            } else {
                iopt = (vtd_pte_t*)paddr_to_pptr(vtd_pte_ptr_get_addr(vtd_pte_slot));
            }
        }
    }
}

BOOT_CODE static void
vtd_create_context_table(
    uint8_t   bus,
    uint32_t  max_num_iopt_levels,
    acpi_rmrr_list_t *rmrr_list
)
{
    unsigned int i;
    vtd_cte_t* vtd_context_table = (vtd_cte_t*)alloc_region(VTD_CT_SIZE_BITS);
    if (!vtd_context_table) {
        fail("Failed to allocate context table");
    }

    printf("IOMMU: Create VTD context table for PCI bus 0x%x (pptr=0x%x)\n", bus, (uint32_t)vtd_context_table);
    memzero(vtd_context_table, 1 << VTD_CT_SIZE_BITS);
    flushCacheRange(vtd_context_table, VTD_CT_SIZE_BITS);

    ia32KSvtdRootTable[bus] =
        vtd_rte_new(
            pptr_to_paddr(vtd_context_table), /* Context Table Pointer */
            true                              /* Present               */
        );

    /* map in any RMRR regions */
    for (i = 0; i < rmrr_list->num; i++) {
        if (vtd_get_root_index(rmrr_list->entries[i].device) == bus) {
            uint32_t addr;
            for (addr = rmrr_list->entries[i].base; addr < rmrr_list->entries[i].limit; addr += BIT(IA32_4K_bits)) {
                (void)vtd_map_reserved_page;
                vtd_map_reserved_page(vtd_context_table, vtd_get_context_index(rmrr_list->entries[i].device), addr);
            }
        }
    }
}

BOOT_CODE static bool_t
vtd_enable(cpu_id_t cpu_id)
{
    drhu_id_t i;

    for (i = 0; i < ia32KSnumDrhu; i++) {
        /* Set the Root Table Register */
        vtd_write32(i, RTADDR_REG, pptr_to_paddr((void*)ia32KSvtdRootTable));
        vtd_write32(i, RTADDR_REG + 4, 0);

        /* Set SRTP bit in GCMD_REG */
        vtd_write32(i, GCMD_REG, (1 << SRTP));

        /* Wait for SRTP operation to complete by polling
         * RTPS bit from GSTS_REG
         */
        while (!((vtd_read32(i, GSTS_REG) >> RTPS) & 1));
    }

    /* Globally invalidate context cache of all IOMMUs */
    invalidate_context_cache();

    /* Globally invalidate IOTLB of all IOMMUs */
    invalidate_iotlb();

    for (i = 0; i < ia32KSnumDrhu; i++) {
        uint32_t data, addr;

        data = int_iommu;
        addr = apic_get_base_paddr();
        if (!addr) {
            return false;
        }
        addr |= (cpu_id << 12);

        vtd_process_faults(i);
        vtd_write32(i, FECTL_REG, 0);
        vtd_write32(i, FEDATA_REG, data);
        vtd_write32(i, FEADDR_REG, addr);
        vtd_write32(i, FEUADDR_REG, 0);

        /*flush IOMMU write buffer */
        vtd_write32(i, GCMD_REG, BIT(WBF));
        while (((vtd_read32(i, GSTS_REG) >> WBFS) & 1));

        printf("IOMMU 0x%x: enabling...", i);

        /* Enable the DMA translation by setting TE bit in GCMD_REG */
        vtd_write32(i, GCMD_REG, (1U << TE));

        /* Wait for Translation Enable operation to complete by polling
         * TES bit from GSTS_REG
         */
        while (!((vtd_read32(i, GSTS_REG) >> TES) & 1));

        printf(" enabled\n");
    }
    return true;
}

BOOT_CODE bool_t
vtd_init(
    cpu_id_t  cpu_id,
    uint32_t  num_drhu,
    acpi_rmrr_list_t *rmrr_list
)
{
    drhu_id_t i;
    uint32_t  bus;
    uint32_t  aw_bitmask = 0xffffffff;
    uint32_t  max_num_iopt_levels;
    /* Start the number of domains at 16 bits */
    uint32_t  num_domain_id_bits = 16;

    ia32KSnumDrhu = num_drhu;
    ia32KSFirstValidIODomain = 0;

    if (ia32KSnumDrhu == 0) {
        return true;
    }

    for (i = 0; i < ia32KSnumDrhu; i++) {
        uint32_t bits_supported = 4 + 2 * (vtd_read32(i, CAP_REG) & 7);
        aw_bitmask &= vtd_read32(i, CAP_REG) >> SAGAW;
        printf("IOMMU 0x%x: %d-bit domain IDs supported\n", i, bits_supported);
        if (bits_supported < num_domain_id_bits) {
            num_domain_id_bits = bits_supported;
        }
    }

    ia32KSnumIODomainIDBits = num_domain_id_bits;

    if (aw_bitmask & SAGAW_6_LEVEL) {
        max_num_iopt_levels = 6;
    } else if (aw_bitmask & SAGAW_5_LEVEL) {
        max_num_iopt_levels = 5;
    } else if (aw_bitmask & SAGAW_4_LEVEL) {
        max_num_iopt_levels = 4;
    } else if (aw_bitmask & SAGAW_3_LEVEL) {
        max_num_iopt_levels = 3;
    } else if (aw_bitmask & SAGAW_2_LEVEL) {
        max_num_iopt_levels = 2;
    } else {
        printf("IOMMU: mismatch of supported number of PT levels between IOMMUs\n");
        return false;
    }

    if (aw_bitmask & SAGAW_3_LEVEL) {
        ia32KSnumIOPTLevels = 3;
    } else if (aw_bitmask & SAGAW_4_LEVEL) {
        ia32KSnumIOPTLevels = 4;
    } else if (aw_bitmask & SAGAW_5_LEVEL) {
        ia32KSnumIOPTLevels = 5;
    } else if (aw_bitmask & SAGAW_6_LEVEL) {
        ia32KSnumIOPTLevels = 6;
    } else if (aw_bitmask & SAGAW_2_LEVEL) {
        ia32KSnumIOPTLevels = 2;
    } else {
        printf("IOMMU: mismatch of supported number of PT levels between IOMMUs\n");
        return false;
    }

    printf("IOMMU: Using %d page-table levels (max. supported: %d)\n", ia32KSnumIOPTLevels, max_num_iopt_levels);

    vtd_create_root_table();

    for (bus = 0; bus < 256; bus++) {
        vtd_create_context_table(
            bus,
            max_num_iopt_levels,
            rmrr_list
        );
    }

    flushCacheRange(ia32KSvtdRootTable, VTD_RT_SIZE_BITS);

    if (!vtd_enable(cpu_id)) {
        return false;
    }
    return true;
}

#endif
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/plat/pc99/machine/io.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <arch/kernel/boot_sys.h>
#include <arch/kernel/lock.h>
#include <arch/model/statedata.h>
#include <plat/machine/io.h>

#if defined DEBUG || defined RELEASE_PRINTF

static uint16_t get_console_port(void)
{
    if (in_boot_phase()) {
        return console_port_of_node(node_of_cpu(cur_cpu_id()));
    } else {
        return ia32KSconsolePort;
    }
}

void serial_init(uint16_t port)
{
    while (!(in8(port + 5) & 0x60)); /* wait until not busy */

    out8(port + 1, 0x00); /* disable generating interrupts */
    out8(port + 3, 0x80); /* line control register: command: set divisor */
    out8(port,     0x01); /* set low byte of divisor to 0x01 = 115200 baud */
    out8(port + 1, 0x00); /* set high byte of divisor to 0x00 */
    out8(port + 3, 0x03); /* line control register: set 8 bit, no parity, 1 stop bit */
    out8(port + 4, 0x0b); /* modem control register: set DTR/RTS/OUT2 */

    in8(port);     /* clear recevier port */
    in8(port + 5); /* clear line status port */
    in8(port + 6); /* clear modem status port */
}

void console_putchar(char c)
{
    uint16_t port = get_console_port();

    lock_acquire(&lock_debug);

    if (port > 0) {
        while (!(in8(port + 5) & 0x60));
        out8(port, c);
        if (c == '\n') {
            while (!(in8(port + 5) & 0x60));
            out8(port, '\r');
        }
    }

    lock_release(&lock_debug);
}

#endif
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/plat/pc99/machine/ioapic.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>

#ifdef CONFIG_IRQ_IOAPIC

#include <arch/linker.h>
#include <plat/machine/io.h>
#include <plat/machine/hardware.h>
#include <plat/machine/ioapic.h>

#define IOAPIC_REGSEL 0x00
#define IOAPIC_WINDOW 0x10

#define IOAPIC_REG_IOAPICID 0x00
#define IOAPIC_REG_IOREDTBL 0x10

#define IOREDTBL_LOW(reg) (IOAPIC_REG_IOREDTBL + (reg) * 2)
#define IOREDTBL_HIGH(reg) (IOREDTBL_LOW(reg) + 1)

#define IOREDTBL_LOW_INTERRUPT_MASK BIT(16)
#define IOREDTBL_LOW_TRIGGER_MODE_LEVEL BIT(15)
#define IOREDTBL_LOW_POLARITY_LOW BIT(13)
#define IOREDTBL_LOW_DEST_MODE_LOGCIAL BIT(11)

#define IOAPICID_ID_BITS 4
#define IOAPICID_ID_OFFSET 24

#define IOREDTBL_HIGH_RESERVED_BITS 24

/* Cache what we believe is in the low word of the IOREDTBL. This
 * has all the state of trigger modes etc etc */
static uint32_t ioredtbl_state[IOAPIC_IRQ_LINES * CONFIG_MAX_NUM_IOAPIC];

/* Number of IOAPICs in the system */
static uint32_t num_ioapics = 0;

/* In debug mode we track whether an unmasked vector has
 * had its mode set. This is to catch bad user level code */
#if defined DEBUG || defined RELEASE_PRINTF
static bool_t done_set_mode[IOAPIC_IRQ_LINES * CONFIG_MAX_NUM_IOAPIC] = { 0 };
#endif

static void ioapic_write(uint32_t ioapic, uint32_t reg, uint32_t value)
{
    *(volatile uint32_t*)((uint32_t)(PPTR_IOAPIC_START + ioapic * BIT(PAGE_BITS)) + reg) = value;
}

static uint32_t ioapic_read(uint32_t ioapic, uint32_t reg)
{
    return *(volatile uint32_t*)((uint32_t)(PPTR_IOAPIC_START + ioapic * BIT(PAGE_BITS)) + reg);
}

static bool_t in_list(uint32_t size, cpu_id_t *list, cpu_id_t target)
{
    uint32_t i;
    for (i = 0; i < size; i++) {
        if (list[i] == target) {
            return true;
        }
    }
    return false;
}

static void single_ioapic_init(uint32_t ioapic, cpu_id_t ioapic_id, cpu_id_t delivery_cpu)
{
    uint32_t id_reg;
    uint32_t i;
    /* Write the ID to the ioapic */
    ioapic_write(ioapic, IOAPIC_REGSEL, IOAPIC_REG_IOAPICID);
    id_reg = ioapic_read(ioapic, IOAPIC_WINDOW);
    /* perform mask to preserve the reserved bits */
    id_reg &= ~(MASK(IOAPICID_ID_BITS) << IOAPICID_ID_OFFSET);
    id_reg |= ioapic_id << IOAPICID_ID_OFFSET;
    /* Mask all the IRQs and set default delivery details.
     * attempt to deliberately set a trigger mode and level
     * setting that is LEAST likely to be correct. This is
     * to ensure user code sets it correctly and cannot get
     * away with it happening to be correct */
    for (i = 0; i < IOAPIC_IRQ_LINES; i++) {
        /* Send to desired cpu */
        ioapic_write(ioapic, IOAPIC_REGSEL, IOREDTBL_HIGH(i));
        ioapic_write(ioapic, IOAPIC_WINDOW, (ioapic_read(ioapic, IOAPIC_WINDOW) & MASK(IOREDTBL_HIGH_RESERVED_BITS)) | (delivery_cpu << IOREDTBL_HIGH_RESERVED_BITS));
        /* Mask and set to level trigger high polarity and make the delivery vector */
        ioredtbl_state[i] = IOREDTBL_LOW_INTERRUPT_MASK |
                            IOREDTBL_LOW_TRIGGER_MODE_LEVEL |
                            (i + IRQ_INT_OFFSET);
        ioapic_write(ioapic, IOAPIC_REGSEL, IOREDTBL_LOW(i));
        /* The upper 16 bits are reserved, so we make sure to preserve them */
        ioredtbl_state[i] |= ioapic_read(ioapic, IOAPIC_WINDOW) & ~MASK(16);
        ioapic_write(ioapic, IOAPIC_WINDOW, ioredtbl_state[i]);
    }
}

/* To guarantee we will be able to find enough free apic ids there needs to be less than
 * 2^4 cpus + ioapics in the system */
compile_assert(ioapic_id_will_not_overflow, CONFIG_MAX_NUM_NODES + CONFIG_MAX_NUM_IOAPIC < 16);

void ioapic_init(uint32_t num_nodes, cpu_id_t *cpu_list, uint32_t num_ioapic)
{
    uint32_t ioapic;
    cpu_id_t ioapic_id = 0;
    num_ioapics = num_ioapic;
    for (ioapic = 0; ioapic < num_ioapic; ioapic++) {
        /* Determine the next free apic ID */
        while (in_list(num_nodes, cpu_list, ioapic_id)) {
            ioapic_id++;
        }
        /* ioapic id field is 4 bits. this assert passing should be
         * guaranteed by the compile assert above this function, hence
         * this does not need to be a run time check */
        assert(ioapic_id < BIT(4));
        /* Init this ioapic */
        single_ioapic_init(ioapic, ioapic_id, cpu_list[0]);
        /* Increment the id */
        ioapic_id++;
    }
}

void ioapic_mask_irq(bool_t mask, irq_t irq)
{
    uint32_t ioapic = irq / IOAPIC_IRQ_LINES;
    uint32_t index = irq % IOAPIC_IRQ_LINES;
    if (ioapic >= num_ioapics) {
        /* silently ignore requests to non existent parts of the interrupt space */
        return;
    }
    if (mask) {
        ioredtbl_state[irq] |= IOREDTBL_LOW_INTERRUPT_MASK;
    } else {
        ioredtbl_state[irq] &= ~IOREDTBL_LOW_INTERRUPT_MASK;
#if defined DEBUG || defined RELEASE_PRINTF
        if (!done_set_mode[irq]) {
            printf("Unmasking IOAPIC source %d on ioapic %d without ever setting its mode!\n", index, ioapic);
            /* Set the flag so we don't repeatedly warn */
            done_set_mode[irq] = 1;
        }
#endif
    }
    ioapic_write(ioapic, IOAPIC_REGSEL, IOREDTBL_LOW(index));
    ioapic_write(ioapic, IOAPIC_WINDOW, ioredtbl_state[irq]);
}

void ioapic_set_mode(irq_t irq, bool_t levelTrigger, bool_t polarityLow)
{
    uint32_t ioapic = irq / IOAPIC_IRQ_LINES;
    uint32_t index = irq % IOAPIC_IRQ_LINES;
    if (ioapic >= num_ioapics) {
        /* silently ignore requests to non existent parts of the interrupt space */
        return;
    }
    if (levelTrigger) {
        ioredtbl_state[irq] |= IOREDTBL_LOW_TRIGGER_MODE_LEVEL;
    } else {
        ioredtbl_state[irq] &= ~IOREDTBL_LOW_TRIGGER_MODE_LEVEL;
    }
    if (polarityLow) {
        ioredtbl_state[irq] |= IOREDTBL_LOW_POLARITY_LOW;
    } else {
        ioredtbl_state[irq] &= ~IOREDTBL_LOW_POLARITY_LOW;
    }
#if defined DEBUG || defined RELEASE_PRINTF
    done_set_mode[irq] = 1;
#endif
    ioapic_write(ioapic, IOAPIC_REGSEL, IOREDTBL_LOW(index));
    ioapic_write(ioapic, IOAPIC_WINDOW, ioredtbl_state[irq]);
}

#endif /* CONFIG_IOAPIC */
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/plat/pc99/machine/pic.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <arch/linker.h>
#include <plat/machine/io.h>
#include <plat/machine/hardware.h>
#include <plat/machine/pic.h>

/* PIC (i8259) base registers */
#define PIC1_BASE 0x20
#define PIC2_BASE 0xa0

/* Program PIC (i8259) to remap IRQs 0-15 to interrupt vectors starting at 'interrupt' */
BOOT_CODE void
pic_remap_irqs(interrupt_t interrupt)
{
    out8(PIC1_BASE, 0x11);
    out8(PIC2_BASE, 0x11);
    out8(PIC1_BASE + 1, interrupt);
    out8(PIC2_BASE + 1, interrupt + 8);
    out8(PIC1_BASE + 1, 0x04);
    out8(PIC2_BASE + 1, 0x02);
    out8(PIC1_BASE + 1, 0x01);
    out8(PIC2_BASE + 1, 0x01);
    out8(PIC1_BASE + 1, 0x0);
    out8(PIC2_BASE + 1, 0x0);
}

BOOT_CODE void pic_disable(void)
{
    /* We assume that pic_remap_irqs has already been called and
     * just mask all the irqs */
    out8(PIC1_BASE + 1, 0xff);
    out8(PIC2_BASE + 1, 0xff);
}

#ifdef CONFIG_IRQ_PIC

void pic_mask_irq(bool_t mask, irq_t irq)
{
    uint8_t  bit_mask;
    uint16_t pic_port;

    assert(irq >= irq_isa_min);
    assert(irq <= irq_isa_max);

    if (irq < 8) {
        bit_mask = BIT(irq);
        pic_port = PIC1_BASE + 1;
    } else {
        bit_mask = BIT(irq - 8);
        pic_port = PIC2_BASE + 1;
    }

    if (mask) {
        /* Disables the interrupt */
        out8(pic_port, (in8(pic_port) | bit_mask));
    } else {
        /* Enables the interrupt */
        out8(pic_port, (in8(pic_port) & ~bit_mask));
    }
}

bool_t pic_is_irq_pending(void)
{
    /* Interrupt Request Register (IRR) - holds pending IRQs */
    uint8_t irr;

    /* Send to PIC1's OCW3, in order to read IRR from next inb instruction */
    out8(PIC1_BASE, 0x0a);

    /* Read IRR */
    irr = in8(PIC1_BASE);

    /* Since slave PIC is connected to IRQ2 of master PIC,
     * there is no need to check IRR of slave PIC.
     */
    return irr != 0;
}

static uint16_t pic_get_isr(void)
{
    out8(PIC1_BASE, 0x0b);
    out8(PIC2_BASE, 0x0b);
    return (((uint16_t)in8(PIC2_BASE)) << 8) | in8(PIC1_BASE);
}

void pic_ack_active_irq(void)
{
    irq_t irq = getActiveIRQ();
    if (irq >= irq_isa_min + 8) {
        /* ack slave PIC, unless we got a spurious irq 15
         * It is spurious if the bit is not set in the ISR
         * Even if it was spurious we will still need to
         * acknowledge the master PIC */
        if (irq != irq_isa_min + 15 || (pic_get_isr() & BIT(15))) {
            out8(PIC2_BASE, 0x20);
        }
    }
    /* ack master PIC, unless we got a spurious IRQ 7
     * It is spurious if the bit is not set in the ISR */
    if (irq != irq_isa_min + 7 || (pic_get_isr() & BIT(7))) {
        out8(PIC1_BASE, 0x20);
    }
}

#endif
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/plat/pc99/machine/pit.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <arch/linker.h>
#include <plat/machine/io.h>
#include <plat/machine/pit.h>

/* PIT (i8253) registers */
#define PIT_MODE 0x43
#define PIT_CH0  0x40

/* Count frequency in Hz */
#define PIT_HZ 1193180

PHYS_CODE void
pit_init(void)
{
    uint16_t divisor = (PIT_HZ * PIT_WRAPAROUND_MS) / 1000;

    out8_phys(PIT_MODE, 0x34);          /* Set mode 2 and wait for divisor bytes */
    out8_phys(PIT_CH0, divisor & 0xff); /* Set low byte of divisor */
    out8_phys(PIT_CH0, divisor >> 8);   /* Set high byte of divisor */
}

PHYS_CODE void
pit_wait_wraparound(void)
{
    uint16_t count;
    uint16_t count_old;

    out8_phys(PIT_MODE, 0x00);
    count = in8_phys(PIT_CH0);
    count |= (in8_phys(PIT_CH0) << 8);
    count_old = count;

    while (count <= count_old) {
        count_old = count;
        out8_phys(PIT_MODE, 0x00);
        count = in8_phys(PIT_CH0);
        count |= (in8_phys(PIT_CH0) << 8);
    }
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/string.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <assert.h>
#include <string.h>

#ifdef DEBUG

unsigned int strnlen(const char *s, unsigned int maxlen)
{
    unsigned int len;
    for (len = 0; len < maxlen && s[len]; len++);
    return len;
}

unsigned int strlcpy(char *dest, const char *src, unsigned int size)
{
    unsigned int len;
    for (len = 0; len + 1 < size && src[len]; len++) {
        dest[len] = src[len];
    }
    dest[len] = '\0';
    return len;
}

unsigned int strlcat(char *dest, const char *src, unsigned int size)
{
    unsigned int len;
    /* get to the end of dest */
    for (len = 0; len < size && dest[len]; len++);
    /* check that dest was at least 'size' length to prevent inserting
     * a null byte when we shouldn't */
    if (len < size) {
        for (; len + 1 < size && *src; len++, src++) {
            dest[len] = *src;
        }
        dest[len] = '\0';
    }
    return len;
}

#endif
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/util.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <assert.h>
#include <stdint.h>
#include <util.h>

/*
 * Zero 'n' bytes of memory starting from 's'.
 *
 * 'n' and 's' must be word aligned.
 */
void
memzero(void *s, unsigned int n)
{
    uint8_t *p = s;

    /* Ensure alignment constraints are met. */
    assert((unsigned int)s % 4 == 0);
    assert(n % 4 == 0);

    /* We will never memzero an area larger than the largest current
       live object */
    /** GHOSTUPD: "(gs_get_assn cap_get_capSizeBits_'proc \<acute>ghost'state = 0
        \<or> \<acute>n <= gs_get_assn cap_get_capSizeBits_'proc \<acute>ghost'state, id)" */

    /* Write out words. */
    while (n != 0) {
        *(uint32_t *)p = 0;
        p += 4;
        n -= 4;
    }
}

void*
memset(void *s, unsigned int c, unsigned int n)
{
    uint8_t *p;

    /*
     * If we are only writing zeros and we are word aligned, we can
     * use the optimized 'memzero' function.
     */
    if (likely(c == 0 && ((uint32_t)s % 4) == 0 && (n % 4) == 0)) {
        memzero(s, n);
    } else {
        /* Otherwise, we use a slower, simple memset. */
        for (p = (uint8_t *)s; n > 0; n--, p++) {
            *p = (uint8_t)c;
        }
    }

    return s;
}

void*
memcpy(void* ptr_dst, const void* ptr_src, unsigned int n)
{
    uint8_t *p;
    const uint8_t *q;

    for (p = (uint8_t *)ptr_dst, q = (const uint8_t *)ptr_src; n; n--, p++, q++) {
        *p = *q;
    }

    return ptr_dst;
}

int
strncmp(const char* s1, const char* s2, int n)
{
    unsigned int i;
    int diff;

    for (i = 0; i < n; i++) {
        diff = ((unsigned char*)s1)[i] - ((unsigned char*)s2)[i];
        if (diff != 0 || s1[i] == '\0') {
            return diff;
        }
    }

    return 0;
}

int CONST
char_to_int(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    } else if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

int PURE
str_to_int(const char* str)
{
    unsigned int base;
    int res;
    int val = 0;
    char c;

    /*check for "0x" */
    if (*str == '0' && (*(str + 1) == 'x' || *(str + 1) == 'X')) {
        base = 16;
        str += 2;
    } else {
        base = 10;
    }

    if (!*str) {
        return -1;
    }

    c = *str;
    while (c != '\0') {
        res = char_to_int(c);
        if (res == -1 || res >= base) {
            return -1;
        }
        val = val * base + res;
        str++;
        c = *str;
    }

    return val;
}
#line 1 "/home/kq/Sources/RefOS_x86/kernel/src/config/default_domain.c"
/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <object/structures.h>
#include <model/statedata.h>

/* Default schedule. */
const dschedule_t ksDomSchedule[] = {
    { .domain = 0, .length = 1 },
};

const unsigned int ksDomScheduleLength = sizeof(ksDomSchedule) / sizeof(dschedule_t);

