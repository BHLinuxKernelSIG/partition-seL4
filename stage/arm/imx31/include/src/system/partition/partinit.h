#ifndef _PARTINIT_H
#define _PARTINIT_H

int part_init();
seL4_CPtr get_tcb_cptr_from_pid(int pid);

//int timer_server_handle_message(struct timeserv_state *s, srv_msg_t *msg);

#endif