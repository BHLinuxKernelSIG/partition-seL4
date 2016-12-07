#ifndef _APEX_COMMON_H
#define _APEX_COMMON_H

#define SYSTEM_LIMIT_NUMBER_OF_PARTITIONS     32   /* module scope */
#define SYSTEM_LIMIT_NUMBER_OF_MESSAGES       512  /* module scope */
#define SYSTEM_LIMIT_MESSAGE_SIZE             8192 /* module scope */
#define SYSTEM_LIMIT_NUMBER_OF_PROCESSES      128  /* partition scope */
#define SYSTEM_LIMIT_NUMBER_OF_SAMPLING_PORTS 512  /* partition scope */
#define SYSTEM_LIMIT_NUMBER_OF_QUEUING_PORTS  512  /* partition scope */
#define SYSTEM_LIMIT_NUMBER_OF_BUFFERS        256  /* partition scope */
#define SYSTEM_LIMIT_NUMBER_OF_BLACKBOARDS    256  /* partition scope */
#define SYSTEM_LIMIT_NUMBER_OF_SEMAPHORES     256  /* partition scope */
#define SYSTEM_LIMIT_NUMBER_OF_EVENTS         256  /* partition scope */

typedef enum {
	SYS_SET_PARTITION_MODE = 0xf000
} syscall_num_t;

/*----------------------*/
/* Base APEX types        */
/*----------------------*/
/*  The actual size of these base types is system specific and the          */
/*  sizes must match the sizes used by the implementation of the            */
/*  underlying Operating System.                                            */
typedef   unsigned char    APEX_BYTE;               /* 8-bit unsigned */
typedef   long             APEX_INTEGER;            /* 32-bit signed */
typedef   unsigned long    APEX_UNSIGNED;           /* 32-bit unsigned */
typedef   long long        APEX_LONG_INTEGER;       /* 64-bit signed */
/*----------------------*/
/* General APEX types     */
/*----------------------*/
typedef
enum {
   NO_ERROR        =  0,    /*  request valid and operation performed     */
   NO_ACTION       =  1,    /*  status of system unaffected by request    */
   NOT_AVAILABLE   =  2,    /*  resource required by request unavailable  */
   INVALID_PARAM   =  3,    /*  invalid parameter specified in request    */
   INVALID_CONFIG  =  4,    /*  parameter incompatible with configuration */
   INVALID_MODE    =  5,    /*  request incompatible with current mode    */
   TIMED_OUT       =  6     /*  time-out tied up with request has expired */
} RETURN_CODE_TYPE;

#define   MAX_NAME_LENGTH              30
typedef   char             NAME_TYPE[MAX_NAME_LENGTH];
typedef   void             (* SYSTEM_ADDRESS_TYPE);
typedef   APEX_BYTE*       MESSAGE_ADDR_TYPE;
typedef   APEX_INTEGER     MESSAGE_SIZE_TYPE;
typedef   APEX_INTEGER     MESSAGE_RANGE_TYPE;
typedef   enum { SOURCE = 0, DESTINATION = 1 } PORT_DIRECTION_TYPE;
typedef   enum { FIFO = 0, PRIORITY = 1 } QUEUING_DISCIPLINE_TYPE;
typedef   APEX_LONG_INTEGER     SYSTEM_TIME_TYPE; /* 64-bit signed integer with a 1 nanosecond LSB */
#define   INFINITE_TIME_VALUE       -1

typedef enum
{
		POK_ERRNO_OK                    =   0,
		POK_ERRNO_EINVAL                =   1,

		POK_ERRNO_UNAVAILABLE           =   2,
		POK_ERRNO_PARAM									=   3,
		POK_ERRNO_TOOMANY               =   5,
		POK_ERRNO_EPERM                 =   6,
		POK_ERRNO_EXISTS                =   7,

		POK_ERRNO_ERANGE                =   8,
		POK_ERRNO_EDOM                  =   9,
		POK_ERRNO_HUGE_VAL              =  10,

		POK_ERRNO_EFAULT                =  11,

		POK_ERRNO_THREAD                =  49,
		POK_ERRNO_THREADATTR            =  50,

		POK_ERRNO_TIME                 =  100,

		POK_ERRNO_PARTITION_ATTR        = 200,

		POK_ERRNO_PORT                 =  301,
		POK_ERRNO_NOTFOUND             =  302,
		POK_ERRNO_DIRECTION            =  303,
		POK_ERRNO_SIZE                 =  304,
		POK_ERRNO_DISCIPLINE           =  305,
		POK_ERRNO_PORTPART             =  307,
		POK_ERRNO_EMPTY                =  308,
		POK_ERRNO_KIND                 =  309,
		POK_ERRNO_FULL                 =  311,
		POK_ERRNO_READY                =  310,
		POK_ERRNO_TIMEOUT              =  250,
		POK_ERRNO_MODE                 =  251,

		POK_ERRNO_LOCKOBJ_UNAVAILABLE  =  500,
		POK_ERRNO_LOCKOBJ_NOTREADY     =  501,
		POK_ERRNO_LOCKOBJ_KIND         =  502,
		POK_ERRNO_LOCKOBJ_POLICY       =  503,

		POK_ERRNO_PARTITION_MODE       =  601,

		POK_ERRNO_PARTITION            =  401
} pok_ret_t;

#endif