#ifndef _PORT_H
#define _PORT_H

#include "deployment.h"
#include "partition.h"

#define POK_PORT_MAX_SIZE 512

//#define NULL   0
#define FALSE  0
#define TRUE   1
#define bool_t int
#define pok_bool_t int

extern void port_init();

typedef enum
{
   POK_PORT_QUEUEING_DISCIPLINE_FIFO      = 1,
   POK_PORT_QUEUEING_DISCIPLINE_PRIORITY  = 2
} pok_port_queueing_disciplines_t;

typedef pok_queueing_discipline_t pok_port_queueing_discipline_t;

typedef enum
{
	 POK_PORT_DIRECTION_IN   = 1,
	 POK_PORT_DIRECTION_OUT  = 2
} pok_port_directions_t;

typedef enum
{
	 POK_PORT_KIND_QUEUEING  = 1,
	 POK_PORT_KIND_SAMPLING  = 2,
	 POK_PORT_KIND_INVALID   = 10
} pok_port_kinds_t;

typedef struct
{
	 pok_port_id_t                    identifier;
	 //pok_partition_id_t               partition;
	 struct partition * partition;
	 pok_port_size_t                  index;
	 bool_t                           full;
	 pok_port_size_t                  size;
	 pok_port_size_t                  off_b; /* Offset of the beginning of the buffer */
	 pok_port_size_t                  off_e; /* Offset of the end of the buffer */
	 pok_port_direction_t             direction;
	 pok_port_queueing_discipline_t   discipline;
	 pok_bool_t                       ready;
	 bool_t                           empty;
	 int                              kind;
	 unsigned long int                refresh;
	 unsigned long int                last_receive;
	 bool_t                           must_be_flushed;
	 char *data;
}pok_port_t;

typedef struct
{
   pok_port_size_t      size;
   pok_port_direction_t direction;
   unsigned long        refresh;
   bool_t               validity;
}pok_port_sampling_status_t;

extern pok_port_t  pok_ports[];
extern const char  pok_queue_data[1024];

extern uint8_t pok_ports_by_partition[];
extern char*   pok_ports_names[];
extern uint8_t pok_ports_identifiers[];
extern uint8_t pok_ports_nb_destinations[]; 
extern uint8_t pok_ports_kind[];

#endif