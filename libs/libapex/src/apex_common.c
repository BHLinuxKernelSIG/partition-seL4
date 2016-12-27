#include <apex_common.h>
#include <refos-util/dprintf.h>
#include <refos-rpc/proc_client.h>
#include <refos-rpc/proc_client_helper.h>

void giveup_period()
{
        seL4_MessageInfo_t tag = seL4_MessageInfo_new(0,0,0,1);
        seL4_SetMR(0, 0xabcd);
        seL4_NBSend(REFOS_PROCSERV_EP, tag);
}

void RPC_NBSend1(int mess1)
{
        seL4_MessageInfo_t tag = seL4_MessageInfo_new(0,0,0,1);
        seL4_SetMR(0, mess1);
        seL4_NBSend(REFOS_PROCSERV_EP, tag);
}

void RPC_NBSend2(int mess1, int mess2)
{
        seL4_MessageInfo_t tag = seL4_MessageInfo_new(0,0,0,2);
        seL4_SetMR(0, mess1);
        seL4_SetMR(1, mess2);
        seL4_NBSend(REFOS_PROCSERV_EP, tag);
}

void RPC_NBSend3(int mess1, int mess2, int mess3)
{
        seL4_MessageInfo_t tag = seL4_MessageInfo_new(0,0,0,3);
        seL4_SetMR(0, mess1);
        seL4_SetMR(1, mess2);
        seL4_SetMR(2, mess3);
        seL4_NBSend(REFOS_PROCSERV_EP, tag);
}

void RPC_NBSend4(int mess1, int mess2, int mess3, int mess4)
{
        seL4_MessageInfo_t tag = seL4_MessageInfo_new(0,0,0,4);
        seL4_SetMR(0, mess1);
        seL4_SetMR(1, mess2);
        seL4_SetMR(2, mess3);
        seL4_SetMR(3, mess4);
        seL4_NBSend(REFOS_PROCSERV_EP, tag);
}