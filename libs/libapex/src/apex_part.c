#include <apex_common.h>
#include <apex_part.h>
#include <refos-util/dprintf.h>SYS_STOP_SELF

void GET_PARTITION_STATUS (PARTITION_STATUS_TYPE *status,
                           RETURN_CODE_TYPE      *return_code)
{
    //seL4_DebugPrintf("Function %s not implemented yet\n", __FUNCTION__);

    status->IDENTIFIER = proc_current_partition_get_id();
  	status->PERIOD = proc_current_partition_get_period();
  	status->DURATION = proc_current_partition_get_duration();
  	status->LOCK_LEVEL = proc_current_partition_get_lock_level();
  	status->OPERATING_MODE = proc_current_partition_get_operating_mode();
  	status->START_CONDITION = proc_current_partition_get_start_condition();

  	*return_code = NO_ERROR;
}

void SET_PARTITION_MODE (OPERATING_MODE_TYPE mode,
                         RETURN_CODE_TYPE *ret)
{
    RPC_NBSend2(SYS_SET_PARTITION_MODE, mode);
    *ret = NO_ERROR;
}