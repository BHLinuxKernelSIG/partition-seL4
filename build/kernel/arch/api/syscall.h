/* @LICENSE(OKL_CORE) */ 

/* This header was generated by kernel/tools/syscall_header_gen.py.
 *
 * To add a system call number, edit kernel/include/api/syscall.xml
 *
 */
#ifndef __ARCH_API_SYSCALL_H
#define __ARCH_API_SYSCALL_H

#ifdef __ASSEMBLER__

/* System Calls */
#define SYSCALL_CALL (-1)
#define SYSCALL_REPLY_WAIT (-2)
#define SYSCALL_SEND (-3)
#define SYSCALL_NB_SEND (-4)
#define SYSCALL_WAIT (-5)
#define SYSCALL_REPLY (-6)
#define SYSCALL_YIELD (-7)
#define SYSCALL_POLL (-8)
#define SYSCALL_VM_ENTER (-9)

#endif

#define SYSCALL_MAX (-1)
#define SYSCALL_MIN (-9)

#ifndef __ASSEMBLER__

enum syscall {
    SysCall = -1,
    SysReplyWait = -2,
    SysSend = -3,
    SysNBSend = -4,
    SysWait = -5,
    SysReply = -6,
    SysYield = -7,
    SysPoll = -8,
#ifdef CONFIG_VTX
    SysVMEnter = -9,
#endif /* CONFIG_VTX */
#ifdef DEBUG
    SysDebugPutChar = -10,
    SysDebugHalt = -11,
    SysDebugCapIdentify = -12,
    SysDebugSnapshot = -13,
    SysDebugNameThread = -14,
#endif /* DEBUG */
#ifdef DANGEROUS_CODE_INJECTION
    SysDebugRun = -15,
#endif /* DANGEROUS_CODE_INJECTION */
#ifdef CONFIG_BENCHMARK
    SysBenchmarkResetLog = -16,
    SysBenchmarkDumpLog = -17,
    SysBenchmarkLogSize = -18,
#endif /* CONFIG_BENCHMARK */
};
typedef uint32_t syscall_t;

#endif

#endif /* __ARCH_API_SYSCALL_H */