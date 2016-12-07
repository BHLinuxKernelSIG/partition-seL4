#ifndef _TYPES_H_
#define _TYPES_H_

#define FALSE  0
#define TRUE   1
#define bool_t int
#define pok_bool_t int

/*
typedef unsigned short        uint8_t;
typedef unsigned short        uint16_t;
typedef unsigned int          uint32_t;
typedef unsigned long long    uint64_t;

typedef short                 int8_t;
typedef short                 int16_t;
typedef signed long long      int64_t;
typedef unsigned int          size_t;
*/
/*
typedef unsigned long int     intptr_t;
*/

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