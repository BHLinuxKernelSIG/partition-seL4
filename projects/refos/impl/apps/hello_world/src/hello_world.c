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

#include <assert.h>

void thread1()
{
    char *mess = "hello china";
    while(1)
    {
        seL4_DebugPrintf("[PART 0] hello world from part 0 thread 1 \n");
        RETURN_CODE_TYPE ret;
        WRITE_SAMPLING_MESSAGE(0, mess, 11, &ret);
        giveup_period();
    }
}

int main(void)
{
    refos_initialise();
    static char clone_stack[1][4096];

    seL4_DebugPrintf("[PART 0] hello world from part 0 thread 0.\n");

    int threadID = proc_clone(thread1, &clone_stack[0][4096], 0, 0);

    RETURN_CODE_TYPE ret;
    SAMPLING_PORT_ID_TYPE port_id;
    char *name = "dataout";
    CREATE_SAMPLING_PORT (name,
                          100,
                          SOURCE,
                          1000, // not implemented
                          &port_id,
                          &ret);
    assert(ret == 0);
    assert(port_id == 0);

    seL4_DebugPrintf("[PART 0] port id %d created.\n", port_id);

    seL4_DebugPrintf("[PART 0] Init finish, will set to normal.\n");

    OPERATING_MODE_TYPE mode = NORMAL;
    SET_PARTITION_MODE(mode, &ret);

	return 0;
}


















