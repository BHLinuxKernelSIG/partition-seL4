/*
 *                               POK header
 * 
 * The following file is a part of the POK project. Any modification should
 * made according to the POK licence. You CANNOT use this file or a part of
 * this file is this part of a file for your own project
 *
 * For more information on the POK licence, please see our LICENCE FILE
 *
 * Please follow the coding guidelines described in doc/CODING_GUIDELINES
 *
 *                                      Copyright (c) 2007-2009 POK team 
 *
 * Created by julien on Thu Jan 15 23:34:13 2009 
 */

#ifndef __POK_LIBPOK_PORTS_H__
#define __POK_LIBPOK_PORTS_H__

#include "pok_types.h"


typedef enum
{
   POK_PORT_QUEUEING_DISCIPLINE_FIFO      = 1,
   POK_PORT_QUEUEING_DISCIPLINE_PRIORITY  = 2
} pok_port_queueing_disciplines_t;

typedef enum
{
   POK_PORT_DIRECTION_IN   = 1,
   POK_PORT_DIRECTION_OUT  = 2
} pok_port_directions_t;

typedef pok_queueing_discipline_t pok_port_queueing_discipline_t;

typedef enum
{
   POK_PORT_KIND_QUEUEING  = 1,
   POK_PORT_KIND_SAMPLING  = 2,
   POK_PORT_KIND_VIRTUAL   = 2,
   POK_PORT_KIND_INVALID   = 10
} pok_port_kinds_t;

typedef struct
{
   pok_port_size_t      size;
   pok_port_direction_t direction;
   uint8_t              nb_messages;
   uint8_t              waiting_processes;
}pok_port_queueing_status_t;

#endif
