#include "../process/process.h"
#include "../process/thread.h"
#include "../process/pid.h"

#include "../../common.h"
#include "../../state.h"

#include "shared.h"

#include "partinit.h"
#include <partition.h>

#include <refos-util/dprintf.h>

#define num_of_part 2
#define num_part1_proc 2
#define num_part2_proc 3

void hello_init()
{
    procServ.hello_cptr = get_tcb_cptr_from_pid(4);
    procServ.hello1_cptr = get_tcb_cptr_from_pid(5);
    procServ.hello2_cptr = get_tcb_cptr_from_pid(6);
    procServ.hello3_cptr = get_tcb_cptr_from_pid(7);
    procServ.hello4_cptr = get_tcb_cptr_from_pid(8);
}

void build_struct(struct partition **part, struct process **proc,int num_part)
{
    seL4_DebugPrintf("building structs.....\n");
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
                procServ.hello_cptr);

    //seL4_DebugPrintf("after 1st config_proc\n");

    config_proc(proc[1],
                part[0],
                1,
                100,
                "hello1",
                0,
                procServ.hello2_cptr);

    //seL4_DebugPrintf("after 2nd config_proc\n");


    config_proc(proc[2],
                part[0],
                2,
                100,
                "hello2",
                0,
                procServ.hello2_cptr);

    //seL4_DebugPrintf("after 3rd config_proc\n");

    config_proc(proc[3],
                part[1],
                3,
                100,
                "hello3",
                0,
                procServ.hello3_cptr);

    //seL4_DebugPrintf("after 4th config_proc\n");

    config_proc(proc[4],
                part[1],
                4,
                100,
                "hello4",
                0,
                procServ.hello4_cptr);

    //seL4_DebugPrintf("after 5th config_proc\n");

    part[0]->procs[0] = proc[0];
    part[0]->procs[1] = proc[1];
    part[0]->next->procs[0] = proc[2];
    part[0]->next->procs[1] = proc[3];
    part[0]->next->procs[2] = proc[4];
}

int part_init()
{
    hello_init();

    //int *num = (int *)create_shared_mem(0x1000);
    //*num = num_of_part;

    //struct partition* start = (struct partition *)(num + 1);
    struct partition* part_array[2];
    for(int i = 0; i < 2; i ++)
    {
        part_array[i] = malloc(sizeof(struct partition));
    }

    struct process *proc_array[5];
    for(int i = 0; i < 5;i ++)
    {
        proc_array[i] = malloc(sizeof(struct process));
    }

    // build all stucts
    build_struct(part_array, proc_array, 2);

    //#ifdef CONFIG_PART_DEBUG

    traverse_all_parts(part_array, proc_array, 2);

    //#endif

    return 0;
}