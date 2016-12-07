qemu-system-arm -nographic -M kzm \
		-kernel images/refos-image

ELF-loader started on CPU: ARM Ltd. ARMv6 Part: 0xb36 r1p3
  paddr=[82000000..82c6001f]
ELF-loading image 'kernel'
  paddr=[80000000..8003cfff]
  vaddr=[f0000000..f003cfff]
  virt_entry=f0000000
ELF-loading image 'process_server'
  paddr=[8003d000..80e5dfff]
  vaddr=[8000..e28fff]
  virt_entry=8548
Enabling MMU and paging
Jumping to kernel-image entry point...

Bootstrapping kernel
[00.0] [;1;32mPROCSERV | [0m initialise_welcome_message(): ================= RefOS Version 2.0 =================
[00.1] [;1;32mPROCSERV | [0m initialise_welcome_message():   Built on Nov  3 2016 18:02:07.
[00.2] [;1;32mPROCSERV | [0m initialise_welcome_message():   © Copyright 2014, NICTA
[00.3] [;1;32mPROCSERV | [0m initialise_welcome_message(): =====================================================
[00.4] [;1;32mPROCSERV | [0m initialise_welcome_message(): Node ID: 0 (of 1)
[00.5] [;1;32mPROCSERV | [0m initialise_welcome_message(): initThreadCNode size: 65536 slots
[00.6] [;1;32mPROCSERV | [0m initialise_welcome_message(): initMemPool size: 196608 bytes (48 pages)
[00.7] [;1;32mPROCSERV | [0m initialise_welcome_message(): 
[00.8] [;1;32mPROCSERV | [0m initialise_welcome_message(): --- Capability Details ---
[00.9] [;1;32mPROCSERV | [0m initialise_welcome_message(): Type              Start      End
[00.10] [;1;32mPROCSERV | [0m initialise_welcome_message(): Empty             0x00000e7c 0x00010000
[00.11] [;1;32mPROCSERV | [0m initialise_welcome_message(): Shared frames     0x00000000 0x00000000
[00.12] [;1;32mPROCSERV | [0m initialise_welcome_message(): User image frames 0x0000001b 0x00000e3c
[00.13] [;1;32mPROCSERV | [0m initialise_welcome_message(): User image PTs    0x0000000c 0x0000001b
[00.14] [;1;32mPROCSERV | [0m initialise_welcome_message(): Untypeds          0x00000e3c 0x00000e51
[00.15] [;1;32mPROCSERV | [0m initialise_welcome_message(): Device Regions    0x00000e51 0x00000e7c
[00.16] [;1;32mPROCSERV | [0m initialise_welcome_message(): 
[00.17] [;1;32mPROCSERV | [0m initialise_welcome_message(): --- Untyped Details ---
[00.18] [;1;32mPROCSERV | [0m initialise_welcome_message(): Untyped Slot       Paddr      Bits
[00.19] [;1;32mPROCSERV | [0m initialise_welcome_message():   0     0x00000e3c 0x80001000 12
[00.20] [;1;32mPROCSERV | [0m initialise_welcome_message():   1     0x00000e3d 0x80002000 13
[00.21] [;1;32mPROCSERV | [0m initialise_welcome_message():   2     0x00000e3e 0x80004000 14
[00.22] [;1;32mPROCSERV | [0m initialise_welcome_message():   3     0x00000e3f 0x80008000 15
[00.23] [;1;32mPROCSERV | [0m initialise_welcome_message():   4     0x00000e40 0x80e5f000 12
[00.24] [;1;32mPROCSERV | [0m initialise_welcome_message():   5     0x00000e41 0x80e60000 17
[00.25] [;1;32mPROCSERV | [0m initialise_welcome_message():   6     0x00000e42 0x80e80000 19
[00.26] [;1;32mPROCSERV | [0m initialise_welcome_message():   7     0x00000e43 0x80f00000 20
[00.27] [;1;32mPROCSERV | [0m initialise_welcome_message():   8     0x00000e44 0x81000000 24
[00.28] [;1;32mPROCSERV | [0m initialise_welcome_message():   9     0x00000e45 0x82000000 25
[00.29] [;1;32mPROCSERV | [0m initialise_welcome_message():   a     0x00000e46 0x84000000 25
[00.30] [;1;32mPROCSERV | [0m initialise_welcome_message():   b     0x00000e47 0x86000000 24
[00.31] [;1;32mPROCSERV | [0m initialise_welcome_message():   c     0x00000e48 0x87000000 23
[00.32] [;1;32mPROCSERV | [0m initialise_welcome_message():   d     0x00000e49 0x87800000 22
[00.33] [;1;32mPROCSERV | [0m initialise_welcome_message():   e     0x00000e4a 0x87c00000 21
[00.34] [;1;32mPROCSERV | [0m initialise_welcome_message():   f     0x00000e4b 0x87e00000 19
[00.35] [;1;32mPROCSERV | [0m initialise_welcome_message():  10     0x00000e4c 0x87e80000 18
[00.36] [;1;32mPROCSERV | [0m initialise_welcome_message():  11     0x00000e4d 0x87ec0000 17
[00.37] [;1;32mPROCSERV | [0m initialise_welcome_message():  12     0x00000e4e 0x87ee0000 16
[00.38] [;1;32mPROCSERV | [0m initialise_welcome_message():  13     0x00000e4f 0x87ef0000 14
[00.39] [;1;32mPROCSERV | [0m initialise_welcome_message():  14     0x00000e50 0x87ef4000 13
[00.40] [;1;32mPROCSERV | [0m initialise_welcome_message(): 
[00.41] [;1;32mPROCSERV | [0m initialise_welcome_message(): --- Device Regions: 43 ---
[00.42] [;1;32mPROCSERV | [0m initialise_welcome_message(): CSlot 	 Device Addr 	 Size
[00.43] [;1;32mPROCSERV | [0m initialise_welcome_message(): e51 	 0x43f80000 	 12
[00.44] [;1;32mPROCSERV | [0m initialise_welcome_message(): e52 	 0x43f84000 	 12
[00.45] [;1;32mPROCSERV | [0m initialise_welcome_message(): e53 	 0x43f88000 	 12
[00.46] [;1;32mPROCSERV | [0m initialise_welcome_message(): e54 	 0x43f8c000 	 12
[00.47] [;1;32mPROCSERV | [0m initialise_welcome_message(): e55 	 0x43f90000 	 12
[00.48] [;1;32mPROCSERV | [0m initialise_welcome_message(): e56 	 0x43f94000 	 12
[00.49] [;1;32mPROCSERV | [0m initialise_welcome_message(): e57 	 0x43f98000 	 12
[00.50] [;1;32mPROCSERV | [0m initialise_welcome_message(): e58 	 0x43f9c000 	 12
[00.51] [;1;32mPROCSERV | [0m initialise_welcome_message(): e59 	 0x43fa0000 	 12
[00.52] [;1;32mPROCSERV | [0m initialise_welcome_message(): e5a 	 0x43fa4000 	 12
[00.53] [;1;32mPROCSERV | [0m initialise_welcome_message(): e5b 	 0x43fa8000 	 12
[00.54] [;1;32mPROCSERV | [0m initialise_welcome_message(): e5c 	 0x43fac000 	 12
[00.55] [;1;32mPROCSERV | [0m initialise_welcome_message(): e5d 	 0x43fb0000 	 12
[00.56] [;1;32mPROCSERV | [0m initialise_welcome_message(): e5e 	 0x43fb4000 	 12
[00.57] [;1;32mPROCSERV | [0m initialise_welcome_message(): e5f 	 0x50004000 	 12
[00.58] [;1;32mPROCSERV | [0m initialise_welcome_message(): e60 	 0x50008000 	 12
[00.59] [;1;32mPROCSERV | [0m initialise_welcome_message(): e61 	 0x5000c000 	 12
[00.60] [;1;32mPROCSERV | [0m initialise_welcome_message(): e62 	 0x50010000 	 12
[00.61] [;1;32mPROCSERV | [0m initialise_welcome_message(): e63 	 0x50014000 	 12
[00.62] [;1;32mPROCSERV | [0m initialise_welcome_message(): e64 	 0x50018000 	 12
[00.63] [;1;32mPROCSERV | [0m initialise_welcome_message(): e65 	 0x5001c000 	 12
[00.64] [;1;32mPROCSERV | [0m initialise_welcome_message(): e66 	 0x50020000 	 12
[00.65] [;1;32mPROCSERV | [0m initialise_welcome_message(): e67 	 0x50024000 	 12
[00.66] [;1;32mPROCSERV | [0m initialise_welcome_message(): e68 	 0x50028000 	 12
[00.67] [;1;32mPROCSERV | [0m initialise_welcome_message(): e69 	 0x5003c000 	 12
[00.68] [;1;32mPROCSERV | [0m initialise_welcome_message(): e6a 	 0x53f80000 	 12
[00.69] [;1;32mPROCSERV | [0m initialise_welcome_message(): e6b 	 0x53f84000 	 12
[00.70] [;1;32mPROCSERV | [0m initialise_welcome_message(): e6c 	 0x53f8c000 	 12
[00.71] [;1;32mPROCSERV | [0m initialise_welcome_message(): e6d 	 0x53f90000 	 12
[00.72] [;1;32mPROCSERV | [0m initialise_welcome_message(): e6e 	 0x53f98000 	 12
[00.73] [;1;32mPROCSERV | [0m initialise_welcome_message(): e6f 	 0x53fa4000 	 14
[00.74] [;1;32mPROCSERV | [0m initialise_welcome_message(): e70 	 0x53fb0000 	 12
[00.75] [;1;32mPROCSERV | [0m initialise_welcome_message(): e71 	 0x53fc4000 	 12
[00.76] [;1;32mPROCSERV | [0m initialise_welcome_message(): e72 	 0x53fcc000 	 14
[00.77] [;1;32mPROCSERV | [0m initialise_welcome_message(): e73 	 0x53fd0000 	 14
[00.78] [;1;32mPROCSERV | [0m initialise_welcome_message(): e74 	 0x53fd8000 	 12
[00.79] [;1;32mPROCSERV | [0m initialise_welcome_message(): e75 	 0x53fe0000 	 12
[00.80] [;1;32mPROCSERV | [0m initialise_welcome_message(): e76 	 0x53fec000 	 12
[00.81] [;1;32mPROCSERV | [0m initialise_welcome_message(): e77 	 0xa0000000 	 26
[00.82] [;1;32mPROCSERV | [0m initialise_welcome_message(): e78 	 0xa8000000 	 25
[00.83] [;1;32mPROCSERV | [0m initialise_welcome_message(): e79 	 0xb4000000 	 25
[00.84] [;1;32mPROCSERV | [0m initialise_welcome_message(): e7a 	 0xb6000000 	 25
[00.85] [;1;32mPROCSERV | [0m initialise_welcome_message(): e7b 	 0xc0000000 	 25
[00.86] [;1;32mPROCSERV | [0m initialise(): Allocating main process server endpoint...
[00.87] [;1;32mPROCSERV | [0m initialise(): Setting recv cslot...
[00.88] [;1;32mPROCSERV | [0m initialise(): Initialising process server modules...
[00.89] [;1;32mPROCSERV | [0m pd_init(): Initialising static Page Directory pool (sized 64)...
[00.90] [;1;32mPROCSERV | [0m pid_init(): Initialising process ID allocation table...
[00.91] [;1;32mPROCSERV | [0m w_init(): Initialising window allocation table (max 8192 windows).
[00.92] [;1;32mPROCSERV | [0m ram_dspace_init(): Initialising RAM dataspace allocation table (max 8192 dspaces).
[00.93] [;1;32mPROCSERV | [0m initialise(): OK.
[00.94] [;1;32mPROCSERV | [0m initialise(): ==========================================

[00.95] [;1;32mPROCSERV | [0m main(): ======== RefOS Process Server ========
=======start part_init ========
[00.96] [;1;32mPROCSERV | [0m proc_load_direct(): Allocating PID and PCB...
[00.97] [;1;32mPROCSERV | [0m proc_load_direct(): Allocated PID 1!...
[00.98] [;1;32mPROCSERV | [0m proc_load_direct(): Configuring process for PID 1!...
INFO :sel4utils_elf_load_record_regions:270:  * Loading segment 00008000-->000eb964
[00.99] [;1;32mPROCSERV | [0m thread_config(): Configuring new thread 0xca2a10..
[00.100] [;1;32mPROCSERV | [0m proc_load_direct(): Allocating PID and PCB...
[00.101] [;1;32mPROCSERV | [0m proc_load_direct(): Allocated PID 2!...
[00.102] [;1;32mPROCSERV | [0m proc_load_direct(): Configuring process for PID 2!...
INFO :sel4utils_elf_load_record_regions:270:  * Loading segment 00008000-->00e4e4d4
QEMU: Terminated
