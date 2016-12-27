#ifndef SYS_APEX_H
#define SYS_APEX_H

typedef enum {
	SYS_APEX_START = 0Xf000,
    SYS_SET_PARTITION_MODE,
    
    SYS_STOP_SELF,
    SYS_SUSPEND_SELF,
    
    SYS_SET_PRIORITY,
    
    SYS_SUSPEND,
    SYS_RESUME,

    SYS_GIVE_UP_PERIOD,

    SYS_APEX_MAX
} syscall_num_t;

#endif