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

#include <test_apex.h>
#include <apex_proc.h>
#include <apex_part.h>
#include <apex_sampling.h>

int main()
{
    refos_initialise();

    while(1)
    {
    	seL4_DebugPrintf("[PART 1] hello world from part 1 thread 3\n");
    	
    	
        seL4_MessageInfo_t tag = seL4_MessageInfo_new(0,0,0,1);
        seL4_SetMR(0, 0xabcd);
        seL4_NBSend(REFOS_PROCSERV_EP, tag);
        
        
    }
}


















