#include "port.h"

pok_port_t  pok_ports[POK_CONFIG_NB_PORTS];
const char  pok_queue_data[1024];

seL4_CPtr cap_array[POK_CONFIG_NB_PORTS];

typedef unsigned char uint8_t;

uint8_t pok_ports_nb_ports_by_partition[POK_CONFIG_NB_PARTITIONS] = {1,1};

uint8_t pok_pr1_ports[1] = {dataout};

uint8_t pok_pr2_ports[1] = {datain};

uint8_t*  pok_ports_by_partition[POK_CONFIG_NB_PARTITIONS] = {pok_pr1_ports, pok_pr2_ports};

char*    pok_ports_names[POK_CONFIG_NB_PORTS] = {"dataout","datain"};

uint8_t pok_ports_identifiers[POK_CONFIG_NB_PORTS] = {dataout, datain};

uint8_t pok_ports_nb_destinations[POK_CONFIG_NB_PORTS] = {1,0};

uint8_t pok_ports_dataout_destinations[1] = {datain};

uint8_t pok_ports_datain_destinations[0] = {};

uint8_t* pok_ports_destinations[POK_CONFIG_NB_PORTS] = {pok_ports_dataout_destinations, NULL};

uint8_t  pok_ports_kind[POK_CONFIG_NB_PORTS] = {POK_PORT_KIND_QUEUEING,POK_PORT_KIND_QUEUEING};


void port_init()
{
	return;
}