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

seL4_CPtr hello1_cptr;

int main(void)
{
    refos_initialise();
    //hello1_cptr = proc_get_hello1_cptr();
    
	while(1)
    {
        seL4_DebugPrintf("AAAAAAAAAAAAAAAAAA, %d, %d\n", GET, HELLO);
        seL4_DebugPrintf("mypid is %d\n", GET_PID());
        //seL4_DebugPutChar('\n');

        //seL4_TCB_Suspend(hello1_cptr);

        //user schedule function
        //seL4_TCB_Resume(hello1_cptr);

        //seL4_DebugPutChar('Y');        
        //seL4_DebugPutChar('\n');

        //seL4_Yield();
    }
	return 0;
}


















