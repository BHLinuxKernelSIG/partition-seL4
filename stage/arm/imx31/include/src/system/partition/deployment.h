#ifndef __POK_KERNEL_GENERATED_DEPLOYMENT_H_
#define __POK_KERNEL_GENERATED_DEPLOYMENT_H_ 

#define CONFIG_PART_DEBUG

#define NUM_SYS_THREADS     2

#define POK_NEEDS_TIME       1
#define POK_NEEDS_THREADS    1
#define POK_NEEDS_PARTITIONS 1
#define POK_NEEDS_SCHED      1
#define POK_NEEDS_MIDDLEWARE 1
#define POK_NEEDS_PORTS_SAMPLING 1

#define POK_CONFIG_NB_THREADS       5
#define POK_CONFIG_NB_PARTITIONS    2

#define POK_CONFIG_PARTITIONS_SIZE  {120 * 1024,120 * 1024}
#define POK_CONFIG_PARTITIONS_NTHREADS {2,3}
#define POK_CONFIG_THREADS_NANME \
{\
"fileserv/hello_world", \
"fileserv/hello_world1", \
"fileserv/hello_world2", \
"fileserv/hello_world3", \
"fileserv/hello_world4"  \
}

#define CONFIG_MAIN_THREADS_NANE \
{\
"fileserv/hello_world", \
"fileserv/hello_world2" \
}

#define CONFIG_PCB_OFFSET 3

#define POK_CONFIG_THREAD_PRIO \
{100, 100, 100, 100, 100}

#define POK_CONFIG_THREAD_PERIOD \
{1000, 1000, 1000, 1000, 1000}

#define POK_CONFIG_THREAD_TIME \
{1000, 1000, 1000, 1000, 1000}

#define POK_CONFIG_SCHEDULING_SLOTS {1000, 1000}
#define POK_CONFIG_SCHEDULING_MAJOR_FRAME 2000
#define POK_CONFIG_SCHEDULING_SLOTS_ALLOCATION {0,1}
#define POK_CONFIG_SCHEDULING_NBSLOTS 2

#define POK_CONFIG_PARTITIONS_SCHEDULER \
{POK_SCHED_RR,POK_SCHED_RR}

#define POK_CONFIG_NB_PORTS         2

#define POK_CONFIG_NB_ALLPORTS  2

#define POK_NEEDS_DEBUG

#define num_of_part 2
#define num_of_proc 5
#define num_part1_proc 2
#define num_part2_proc 3

#define MIN_PRIORITY_VALUE       1
#define MAX_PRIORITY_VALUE       63


typedef enum
{
   dataout = 0,
   datain  = 1
}pok_ports_identitiers_t;

/*
typedef unsigned short        uint8_t;
typedef unsigned short        uint16_t;
typedef unsigned int          uint32_t;
typedef long int              int32_t;
typedef unsigned long long    uint64_t;

typedef short                 int8_t;
typedef short                 int16_t;
typedef signed long long      int64_t;

typedef unsigned int          size_t;
typedef int		      ssize_t;
typedef unsigned long int     intptr_t;
*/

//typedef unsigned long long    uint64_t;
//typedef unsigned short        uint8_t;

typedef unsigned int pok_port_size_t;
typedef unsigned int pok_port_direction_t;
typedef unsigned int pok_port_kind_t;
typedef unsigned int pok_queueing_discipline_t;
typedef unsigned int pok_port_id_t;
typedef unsigned int pok_size_t;
typedef unsigned int pok_range_t;
typedef unsigned int pok_buffer_id_t;
typedef unsigned int pok_blackboard_id_t;
typedef unsigned int pok_lockobj_id_t;
typedef unsigned int pok_sem_id_t;
typedef unsigned int pok_event_id_t;
typedef unsigned int pok_partition_id_t;
typedef unsigned int pok_sem_value_t;

#endif
