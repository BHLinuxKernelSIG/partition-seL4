#ifndef _APEX_PROC_H
#define _APEX_PROC_H

#include <refos-rpc/proc_client.h>
#include <refos-rpc/proc_client_helper.h>


int GET_PID()
{
	return proc_getpid();
}


#endif