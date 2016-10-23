#include "timer_init.h"

static void 
build_struct(struct partition **part, struct process **proc,int num_part);

static void hello_init()
{
	A_cptr = proc_get_hello_cptr();
    B_cptr = proc_get_hello1_cptr();
    C_cptr = proc_get_hello2_cptr();
    D_cptr = proc_get_hello3_cptr();
    E_cptr = proc_get_hello4_cptr();
}

static void suspend_all()
{
    seL4_TCB_Suspend(A_cptr);
    seL4_TCB_Suspend(B_cptr);
    seL4_TCB_Suspend(C_cptr);
    seL4_TCB_Suspend(D_cptr);
    seL4_TCB_Suspend(E_cptr);
}

void init_struct()
{
    for(int i = 0; i < 2; i ++)
    {
        part_array[i] = malloc(sizeof(struct partition));
    }

    for(int i = 0; i < 5;i ++)
    {
        proc_array[i] = malloc(sizeof(struct process));
    }

    build_struct(part_array, proc_array, 2);
}

void part_init()
{
    hello_init();
    suspend_all();
    init_struct();
    traverse_all_parts(part_array, proc_array, 2);
}

// TODO use shared structs

static void 
build_struct(struct partition **part, struct process **proc,int num_part)
{
    seL4_DebugPrintf("IN (timer_server) build_struct\n");
    config_part(part[0], 
                part[1],
                0,
                5,
                5,
                2,
                1000);

    //seL4_DebugPrintf("after 1st config_part\n");

    config_part(part[1], 
                part[0],
                1,
                5,
                5,
                3,
                1000);

    //seL4_DebugPrintf("after 2nd config_part\n");

    config_proc(proc[0],
                part[0],
                0,
                100,
                "hello0",
                0,
                A_cptr);

    //seL4_DebugPrintf("after 1st config_proc\n");

    config_proc(proc[1],
                part[0],
                1,
                100,
                "hello1",
                0,
                B_cptr);

    //seL4_DebugPrintf("after 2nd config_proc\n");


    config_proc(proc[2],
                part[0],
                2,
                100,
                "hello2",
                0,
                C_cptr);

    //seL4_DebugPrintf("after 3rd config_proc\n");

    config_proc(proc[3],
                part[1],
                3,
                100,
                "hello3",
                0,
                D_cptr);

    //seL4_DebugPrintf("after 4th config_proc\n");

    config_proc(proc[4],
                part[1],
                4,
                100,
                "hello4",
                0,
                E_cptr);

    //seL4_DebugPrintf("after 5th config_proc\n");

    part[0]->procs[0] = proc[0];
    part[0]->procs[1] = proc[1];
    part[0]->next->procs[0] = proc[2];
    part[0]->next->procs[1] = proc[3];
    part[0]->next->procs[2] = proc[4];
}
