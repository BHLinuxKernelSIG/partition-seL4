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

/*
void part2_user_schedule(int i);

static int i = 0;

seL4_CPtr hello3_cptr;
seL4_CPtr hello4_cptr;

int main(void)
{
    refos_initialise();
    while(1)
    printf("123123123");

    hello3_cptr = proc_get_hello3_cptr();
    hello4_cptr = proc_get_hello4_cptr();

    printf("\nPartition 2 scheduler Init Over\n");
    while(1)
    {
        printf("In partition 2 scheduler\n");
        printf("  will suspend all threads\n");
        seL4_TCB_Suspend(hello3_cptr);
        seL4_TCB_Suspend(hello4_cptr);


        printf("  will schedule new thread\n");
        part2_user_schedule(i++);

        printf("  will yeild myself\n");
        seL4_Yield();
    }
}

void part2_user_schedule(int i)
{
    if(i%2 == 0)
    {
        printf("      will run hello 3\n");
        seL4_TCB_Resume(hello3_cptr);
    }
    else
    {
        printf("      will run hello 4\n");
        seL4_TCB_Resume(hello4_cptr);
    }
}

*/

void print_cycle(int info)
{
    unsigned long long result;
	//asm volatile("rdtsc" : "=A" (result));
    // printf("%d: cycles are %lld\n", info, result);
}

seL4_CPtr hello3_cptr;
seL4_CPtr hello4_cptr;

static int i = 0;

int main(void)
{
    refos_initialise();
    //hello3_cptr = proc_get_hello3_cptr();
    //hello4_cptr = proc_get_hello4_cptr();
    
    while(1)
    {
        //i++;
        seL4_DebugPrintf("CCCCCCCCCCCCCCCC\n");
        //seL4_DebugPutChar('\n');

        //seL4_TCB_Suspend(hello3_cptr);
        //seL4_TCB_Suspend(hello4_cptr);

        //if (i % 2 == 0)
        //{
        //    seL4_TCB_Resume(hello3_cptr);
        //}
        //else
        //{
        //    seL4_TCB_Resume(hello4_cptr);
        //}
        //seL4_Yield();
        //seL4_DebugPutChar('Y');        
        //seL4_DebugPutChar('\n');
    }
    return 0;
}

















