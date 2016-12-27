#include "port.h"
#include <refos-util/dprintf.h>
#include <stdio.h>
#include <refos/refos.h>
#include <refos-io/stdio.h>

pok_port_t  pok_ports[POK_CONFIG_NB_PORTS];
const char  pok_queue_data[1024];

//seL4_CPtr cap_array[POK_CONFIG_NB_PORTS];

uint8_t pok_ports_by_partition[POK_CONFIG_NB_PARTITIONS] = 
                          POK_PORTS_BY_PARTITION;

char*   pok_ports_names[POK_CONFIG_NB_PORTS] = POK_PORTS_NAME;
uint8_t pok_ports_identifiers[POK_CONFIG_NB_PORTS] = POK_PORTS_ID;
uint8_t pok_ports_nb_destinations[POK_CONFIG_NB_PORTS] = 
                          POK_PORTS_DESTS; 
uint8_t pok_ports_kind[POK_CONFIG_NB_PORTS] = POK_PORTS_KIND;

void port_init()
{
   for (int i = 0 ; i < POK_CONFIG_NB_PORTS ; i++)
   {
      pok_ports[i].size    = 0;
      pok_ports[i].off_b   = 0;
      pok_ports[i].off_e   = 0;
      pok_ports[i].empty   = TRUE;
      pok_ports[i].full    = FALSE;
      pok_ports[i].index   = 0;
      pok_ports[i].ready   = FALSE;
      pok_ports[i].kind    = POK_PORT_KIND_INVALID;
      pok_ports[i].last_receive = 0;
      pok_ports[i].must_be_flushed = FALSE;
   }
}

int pok_own_port (const uint8_t partition, const uint8_t port)
{
    seL4_DebugPrintf("[own port] part is %d, port is %d\n", 
    	 partition, 
    	 port);
   if (port > POK_CONFIG_NB_PORTS)
   {
      return 0;
   }

   if ((((uint8_t[]) POK_CONFIG_PARTITIONS_PORTS)[port]) == partition)
   {
      return 1;
   }

   return 0;
}


pok_ret_t pok_port_sampling_id (char* name, pok_port_id_t* id)
{
	 uint8_t i;
	 seL4_DebugPrintf("[id], target name is %s\n", name);
	 for (i = 0; i < POK_CONFIG_NB_PORTS ; i++)
	 {
	 	seL4_DebugPrintf("[id] name array %d is %s\n", 
	 		 i,
	 		 pok_ports_names[i]);

			if ( (strcmp (name, pok_ports_names[i]) == 0) 
				 && (pok_ports_kind[i] == POK_PORT_KIND_SAMPLING))
			{
				 if (! pok_own_port (POK_SCHED_CURRENT_PARTITION, i))
				 {
						return POK_ERRNO_PORT;
				 }

				 *id = i;

				 return POK_ERRNO_OK;
			}
	 }
	 return POK_ERRNO_NOTFOUND;
}


pok_ret_t pok_port_create (char* name, 
	                       const pok_port_size_t size,
						   const pok_port_direction_t direction,
						   uint8_t kind, 
						   pok_port_id_t* id)
{
	 uint8_t   gid;
	 pok_ret_t ret;

	 ret = POK_ERRNO_OK;
	 seL4_DebugPrintf("[create] enter pok_port_create function\n");

	 if (size > POK_PORT_MAX_SIZE)
	 {
			return POK_ERRNO_PORT;
	 }

	 if ((direction != POK_PORT_DIRECTION_IN) &&
			 (direction != POK_PORT_DIRECTION_OUT))
	 {
			return POK_ERRNO_PORT;
	 }

	 switch (kind)
	 {
			case POK_PORT_KIND_SAMPLING:
			{
				ret = pok_port_sampling_id (name, &gid);
				break;
      		}
			
			default:
			{
				return POK_ERRNO_EINVAL;
				break;
			}
	 }

	 if (ret != POK_ERRNO_OK)
	 {
			return ret;
	 }

	 if (pok_ports[gid].ready == TRUE)
	 {
			*id = gid;
			return POK_ERRNO_EXISTS;
	 }

	 pok_ports[gid].index      = pok_ports_identifiers[gid];
	 pok_ports[gid].off_b      = 0;
	 pok_ports[gid].off_e      = 0;
	 pok_ports[gid].size       = size;
	 pok_ports[gid].full       = FALSE;
	 pok_ports[gid].partition  = current_partition;
	 pok_ports[gid].direction  = direction;
	 pok_ports[gid].ready      = TRUE;
	 pok_ports[gid].kind       = kind;

	 //pok_queue.available_size  = pok_queue.available_size - size;
	 pok_ports[gid].data       = malloc(size);

	 *id = gid;

	 return POK_ERRNO_OK;
}

pok_ret_t pok_port_get (const uint32_t pid, void *data, 
	                    const pok_port_size_t size)
{
   if (pok_ports[pid].kind == POK_PORT_KIND_SAMPLING)
   {
        if (pok_ports[pid].empty == TRUE)
         {
            return POK_ERRNO_EMPTY;
         }

         if (size > pok_ports[pid].size)
         {
            return POK_ERRNO_SIZE;
         }
         
         /* TODO: do real data transfer now */
         char temp[size + 1];
         memcpy(temp, pok_ports[pid].data, size);
         temp[size+1] = '\0';
         seL4_DebugPrintf("[Part %d], get data %s from port %d\n",
         	               current_partition,
         	               temp,
         	               pid);


         return POK_ERRNO_OK;
    }
    else
    	return POK_ERRNO_EINVAL;
}

pok_ret_t  pok_port_write (const uint8_t pid, const void *data, const pok_port_size_t size)
{
   uint8_t dest = pok_ports_nb_destinations[pid];
   
   if (pok_ports[pid].kind == POK_PORT_KIND_SAMPLING)
   {
         if (size > pok_ports[dest].size)
         {
            return POK_ERRNO_SIZE;
         }

         /* do real data transfer now */
         memcpy(pok_ports[dest].data, data, size);

         pok_ports[dest].empty = FALSE;
         pok_ports[dest].last_receive = POK_GETTICK ();

         return POK_ERRNO_OK;
	}
     
    return POK_ERRNO_EINVAL;
}

