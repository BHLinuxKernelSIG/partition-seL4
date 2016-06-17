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

#define NUM_THREADS 1

static seL4_CPtr threadEP;
static sync_mutex_t threadMutex;
static uint32_t threadCount = 0;

int threads_func(void *arg)
{
    /*sync_acquire(threadMutex);*/
    /*
    threadCount++;
    threadCount++;
    */
    /*sync_release(threadMutex);*/
    
    seL4_DebugPrintf("child process starts!!!\n");
    printf("why not printf?\n");
    /*
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    seL4_Call(threadEP, tag);
    */
    while(1);
   
    return 0;
}

int main()
{
    refos_initialise();
    /*
    int threadID;
    threadEP = 0;
    threadCount = 0;
    
    static char clone_stack[NUM_THREADS][4096];
   
    seL4_DebugPrintf("I am child??? threadCount is %d\n", threadCount);
    refos_initialise();
    
    seL4_CPtr window;
    seL4_Word vaddr = walloc(1, &window);
    printf("new window created, vaddr: %p", vaddr);
    while(1);
    
    printf("creating new ep...\n");
    threadEP = proc_new_endpoint();
    assert(threadEP != 0);
    assert(REFOS_GET_ERRNO() == ESUCCESS);

    printf("creating new mutex\n");
    threadMutex = sync_create_mutex();
    assert(threadMutex);

    for(int i=0; i < NUM_THREADS; i++){
        printf("cloning thread, id: %d\n", i);
        threadID = proc_clone(threads_func, &clone_stack[i][4096], 0, 0);
        printf("new threadID is %d\n", threadID);
        assert(REFOS_GET_ERRNO() == ESUCCESS);
    }

    for(int i=0; i<NUM_THREADS; i++){
        printf("waiting for thread: %d\n", i);
        seL4_Word badge;
        seL4_Wait(threadEP, &badge);
        assert(badge == 0);
    }

    printf("Is the thread count right?\n");
    printf("thread count is: %d\n", threadCount);
    printf("exiting....\n");
    sync_destroy_mutex(threadMutex);
    proc_del_endpoint(threadEP);
    while(1);
    */
    while(1)
    seL4_DebugPrintf("hello world!\n");
}


















