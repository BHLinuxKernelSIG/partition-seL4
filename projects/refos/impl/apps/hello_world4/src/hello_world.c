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

int main()
{
    refos_initialise();

    while(1)
    seL4_DebugPrintf("EEEEEEEEEEEEEEEEEEEE, %d\n", GET);        
}


