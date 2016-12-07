#include <stdbool.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>

#include <refos/refos.h>
#include <refos-io/stdio.h>
#include <refos-util/init.h>
#include <refos-util/dprintf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <refos-util/init.h>
#include <refos/sync.h>
#include <syscall_stubs_sel4.h>

#include <refos-util/dprintf.h>
#include <refos-rpc/proc_client.h>
#include <refos-rpc/proc_client_helper.h>
#include <refos/vmlayout.h>
#include <data_struct/cvector.h>

#include <refos-io/morecore.h>
#include <test_apex.h>
#include <apex_proc.h>
#include <apex_part.h>
#include <apex_sampling.h>

void test_ping()
{
        seL4_MessageInfo_t tag = seL4_MessageInfo_new(0,0,0,1);
        seL4_SetMR(0, 0xabcd);
        //printf("[hello0]will call process server\n");
        seL4_NBSend(REFOS_PROCSERV_EP, tag);
}

void thread3()
{
    while(1)
    {
        seL4_DebugPrintf("[PART 1] hello world from part 1 thread 3 \n");
        test_ping();
    }
}

void thread4()
{
    while(1)
    {
        seL4_DebugPrintf("[PART 1] hello world from part 1 thread 4 \n");
        test_ping();
    }
}

int main(void)
{
    static char clone_stack1[2][4096];
    static char clone_stack2[2][4096];
    
    refos_initialise();

    proc_clone(thread3, &clone_stack1[0][4096], 0, 0);
    proc_clone(thread4, &clone_stack2[1][4096], 0, 0);
    
    seL4_DebugPrintf("[PART 1] hello world from part 1 thread 2\n");
    seL4_DebugPrintf("[PART 1] Init finish, will set to normal.\n");

    OPERATING_MODE_TYPE mode = NORMAL;
    RETURN_CODE_TYPE ret;
    SET_PARTITION_MODE(mode, &ret);

    return 0;
}

















