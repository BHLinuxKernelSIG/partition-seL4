#include <apex_common.h>
#include <apex_proc.h>
#include <refos-util/dprintf.h>
#include <string.h>

void GET_PROCESS_ID (PROCESS_NAME_TYPE process_name[MAX_NAME_LENGTH],
										 PROCESS_ID_TYPE   *process_id,
										 RETURN_CODE_TYPE  *return_code )
{
	*process_id = proc_get_pid_from_name((char *)process_name);
	if (*process_id == (PROCESS_ID_TYPE)0)
	{
		*return_code = INVALID_CONFIG;
	}
	else
	{
		*return_code = NO_ERROR;
	}
}

void GET_MY_ID (PROCESS_ID_TYPE   *process_id,
		RETURN_CODE_TYPE  *return_code )
{
	*process_id = proc_getpid();
	*return_code = NO_ERROR;
}

void GET_PROCESS_STATUS (PROCESS_ID_TYPE id,
						 PROCESS_STATUS_TYPE *s,
						 RETURN_CODE_TYPE    *return_code)
{
	s->DEADLINE_TIME = proc_get_deadline_from_pid(id);  
	s->CURRENT_PRIORITY = proc_get_currprio_from_pid(id);
	s->PROCESS_STATE = proc_get_status_from_pid(id);

	s->ATTRIBUTES.PERIOD = proc_get_period_from_pid(id);
	s->ATTRIBUTES.TIME_CAPACITY = proc_get_timecap_from_pid(id);
	s->ATTRIBUTES.ENTRY_POINT = (void *)proc_get_entrypoint_from_pid(id);
	s->ATTRIBUTES.STACK_SIZE = proc_get_stacksize_from_pid(id);
	s->ATTRIBUTES.BASE_PRIORITY = proc_get_baseprio_from_pid(id);
	s->ATTRIBUTES.DEADLINE = proc_get_deadline_from_pid(id);

	// TODO cannot get name from RPC 
	//char *name = proc_get_name_from_pid(id);
	//strcpy(s->ATTRIBUTES.NAME, name);

	*return_code = NO_ERROR;
}

/*not implemented, use proc_clone instead*/
// TODO: implement this API
void CREATE_PROCESS (PROCESS_ATTRIBUTE_TYPE  *attributes,
					 PROCESS_ID_TYPE         *process_id,
					 RETURN_CODE_TYPE        *return_code )
{
	seL4_DebugPrintf("Function %s not implemented yet\n", __FUNCTION__);
}

// TODO
void STOP_SELF()
{
	RPC_NBSend1(SYS_STOP_SELF);
}


// TODO
void SET_PRIORITY (PROCESS_ID_TYPE  process_id,
				   PRIORITY_TYPE    prio,
				   RETURN_CODE_TYPE *return_code )
{
	PROCESS_STATE_TYPE status = proc_get_status_from_pid(process_id);
	if (status < DORMANT || status > WAITING)
	{
		*return_code = INVALID_PARAM;
		return;
	}
	if (prio > MAX_PRIORITY_VALUE || prio < MIN_PRIORITY_VALUE)
	{
		*return_code = INVALID_PARAM;
		return;
	}
	if (status == DORMANT)
	{
		*return_code = INVALID_MODE;
		return;
	}

	RPC_NBSend3(SYS_SET_PRIORITY,process_id, prio);
	*return_code = NO_ERROR;
}

// TODO
void SUSPEND_SELF (SYSTEM_TIME_TYPE time_out,
				   RETURN_CODE_TYPE *return_code )
{
	if (time_out > 0) // not implemented
	{
		*return_code = NOT_AVAILABLE;
	}
	else
	{
		//pok_ret_t error = proc_stop_self();
		RPC_NBSend1(SYS_SUSPEND_SELF);
		*return_code = NO_ERROR;
	}
	return;
}

// TODO
void SUSPEND (PROCESS_ID_TYPE    process_id,
			  RETURN_CODE_TYPE   *return_code )
{
	PROCESS_STATE_TYPE status = proc_get_status_from_pid(process_id);
	if (status == DORMANT)
	{
		*return_code = INVALID_MODE;
		return;
	}
	if (proc_get_period_from_pid(process_id) == INFINITE_TIME_VALUE)
	{
		*return_code = INVALID_MODE;
		return;
	}
	if (status == WAITING)
	{
		*return_code = NO_ACTION;
		return;
	}
	RPC_NBSend2(SYS_SUSPEND, process_id);
	*return_code =  NO_ERROR;
}

// TODO
void RESUME (PROCESS_ID_TYPE     process_id,
			 RETURN_CODE_TYPE    *return_code )
{
	PROCESS_STATE_TYPE status = proc_get_status_from_pid(process_id);
	if (status == DORMANT)
	{
		*return_code = INVALID_MODE;
		return;
	}
	if (proc_get_period_from_pid(process_id) == INFINITE_TIME_VALUE)
	{
		*return_code = INVALID_MODE;
		return;
	}
	if (status != WAITING)
	{
		*return_code = NO_ACTION;
		return;
	}

	pok_ret_t error = RPC_NBSend2(SYS_RESUME, process_id);
	*return_code = NO_ERROR;
}

void START (PROCESS_ID_TYPE   process_id,
			RETURN_CODE_TYPE   *return_code )
{
	RESUME (process_id, return_code);
}

// not implemented
void STOP (PROCESS_ID_TYPE    process_id,
		   RETURN_CODE_TYPE *return_code )
{
}

// not implemented
void DELAYED_START (PROCESS_ID_TYPE   process_id,
				    SYSTEM_TIME_TYPE  delay_time,
				    RETURN_CODE_TYPE *return_code )
{
	*return_code = NOT_AVAILABLE;
}

// not implemented
void LOCK_PREEMPTION (LOCK_LEVEL_TYPE     *lock_level,
					  RETURN_CODE_TYPE    *return_code )
{
	(void) lock_level;
	*return_code = NOT_AVAILABLE;
}

// not implemented
void UNLOCK_PREEMPTION (LOCK_LEVEL_TYPE   *lock_level,
					    RETURN_CODE_TYPE  *return_code )

{
	(void) lock_level;
	*return_code = NOT_AVAILABLE;
}

