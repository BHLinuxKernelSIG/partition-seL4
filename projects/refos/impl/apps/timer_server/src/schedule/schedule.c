#include "schedule.h"

void schedule(int i)
{
    if(i%2 == 0)
    {
        seL4_DebugPrintf("\n    will RUN partition 1\n");
        seL4_TCB_Suspend(C_cptr);
        seL4_TCB_Suspend(D_cptr);
        seL4_TCB_Suspend(E_cptr);
        seL4_TCB_Resume(A_cptr);
        seL4_TCB_Resume(B_cptr);
    }
    else
    {
        seL4_DebugPrintf("\n    will RUN partition 2 \n");
        seL4_TCB_Suspend(A_cptr);
        seL4_TCB_Suspend(B_cptr);
        seL4_TCB_Resume(C_cptr);
        seL4_TCB_Resume(D_cptr);
        seL4_TCB_Resume(E_cptr);
    }
}

void schedule_entry(void)
{
    struct timeserv_state *s = &timeServ;
    srv_msg_t msg;
    int counter = 0;
    int i = 0;
    
    while (1) {
        msg.message = seL4_Wait(s->commonState.anonEP, &msg.badge);

        if(counter % 1000 == 0)
        {
            seL4_DebugPrintf("WILL schedule parititions\n");
        }

        timer_server_handle_message(s, &msg);

        if(counter % 1000 == 0)
        {
            schedule(i);
            i ++;
        }
        counter ++;
    }
}