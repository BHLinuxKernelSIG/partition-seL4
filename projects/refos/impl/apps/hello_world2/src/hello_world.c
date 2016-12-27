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

void thread3()
{
    while(1)
    {
        seL4_DebugPrintf("[PART 1] hello world from part 1 thread 3 \n");
        giveup_period();
    }
}

void thread4()
{
    while(1)
    {
        seL4_DebugPrintf("[PART 1] hello world from part 1 thread 4 \n");
        char tmp[100];
        MESSAGE_SIZE_TYPE size;
        VALIDITY_TYPE valid;
        RETURN_CODE_TYPE ret;
        READ_SAMPLING_MESSAGE(1, tmp, &size, &valid, &ret);

        giveup_period();
    }
}

int main(void)
{
    static char clone_stack1[2][4096];
    static char clone_stack2[2][4096];
    
    refos_initialise();

    seL4_DebugPrintf("[PART 1] hello world from part 1 thread 2\n");

    proc_clone(thread3, &clone_stack1[0][4096], 0, 0);
    proc_clone(thread4, &clone_stack2[1][4096], 0, 0);

    RETURN_CODE_TYPE ret;
    SAMPLING_PORT_ID_TYPE port_id;
    char *name = "datain";
    CREATE_SAMPLING_PORT (name,
                          100,
                          DESTINATION,
                          1000, // not implemented
                          &port_id,
                          &ret);
    assert(ret == 0);
    assert(port_id == 1);

    seL4_DebugPrintf("[PART 1] Port id %d created.\n", port_id);
    seL4_DebugPrintf("[PART 1] Init finish, will set to normal.\n");

    OPERATING_MODE_TYPE mode = NORMAL;
    SET_PARTITION_MODE(mode, &ret);

    return 0;
}

















