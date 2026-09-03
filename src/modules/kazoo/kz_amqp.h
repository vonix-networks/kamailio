/*
 * $Id$
 *
 * Kazoo module interface
 *
 * Copyright (C) 2010-2014 2600Hz
 *
 * This file is part of Kamailio, a free SIP server.
 *
 * Kamailio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * Kamailio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * History:
 * --------
 * 2014-08  first version (2600hz)
 */

#ifndef KZ_AMQP_H_
#define KZ_AMQP_H_

#include <stdio.h>
#include <stdlib.h>


#include "../../core/lock_ops.h"
#include "../../core/mem/shm_mem.h"
//#include "../../core/sr_module.h"
#include "../../core/str.h"
#include "../../core/locking.h"
#include "../../core/dprint.h"
#include "../../core/fmsg.h"


#include "const.h"
#include "defs.h"
//#include "kz_amqp_mem.h"
//#include "kz_amqp_pipe.h"
#include "kazoo_params.h"



int kz_amqp_add_connection_k(modparam_t type, void* val);

int kz_amqp_publish3(struct sip_msg* msg, char* exchange, char* routing_key, char* payload);
int kz_amqp_publish4(struct sip_msg* msg, char* exchange, char* routing_key, char* payload, char* _pub_flags);
int kz_amqp_publish5(struct sip_msg* msg, char* exchange, char* routing_key, char* payload, char* _pub_flags, char* args);
int kz_amqp_publish_ex(struct sip_msg* msg, char* exchange, char* routing_key, char* payload, char* _pub_flags, char* args);

int kz_amqp_query3(struct sip_msg* msg, char* exchange, char* routing_key, char* payload);
int kz_amqp_query4(struct sip_msg* msg, char* exchange, char* routing_key, char* payload, char* _pub_flags);
int kz_amqp_query5(struct sip_msg* msg, char* exchange, char* routing_key, char* payload, char* _pub_flags, char* args);
int kz_amqp_query_ex(struct sip_msg* msg, char* exchange, char* routing_key, char* payload, char* _pub_flags, char* args);

int kz_amqp_query_async5(struct sip_msg* msg, char* exchange, char* routing_key, char* payload, char* _cb_route, char* _err_route);
int kz_amqp_query_async6(struct sip_msg* msg, char* exchange, char* routing_key, char* payload, char* _cb_route, char* _err_route, char* _pub_flags);
int kz_amqp_query_async7(struct sip_msg* msg, char* _exchange, char* _routing_key, char* _payload, char* _cb_route, char* _err_route, char* _pub_flags, char* args);
int kz_amqp_query_async_ex(struct sip_msg* msg, char* _exchange, char* _routing_key, char* _payload, char* _cb_route, char* _err_route, char* _pub_flags, char* args);

int kz_amqp_relay(struct sip_msg* msg);
//int kz_amqp_stop_relay(sip_msg_t* msg);
int kz_amqp_stop_relay(sr_event_param_t *evp);

int kz_amqp_subscribe_k(struct sip_msg* msg, char* payload);
int kz_amqp_subscribe_simple_k(struct sip_msg* msg, char* exchange, char* exchange_type, char* queue_name, char* routing_key);

int kz_amqp_encode(struct sip_msg* msg, char* unencoded, char* encoded);
int kz_amqp_encode_ex(str* unencoded, pv_value_p dst_val);

int kz_amqp_declare_exchange_k(struct sip_msg* msg, char* payload);
int kz_amqp_declare_exchanges_k(struct sip_msg* msg, char* payload);


void kz_amqp_manager_proc(int cmd_pipes[2]);
int kz_amqp_consumer_worker_proc(int cmd_pipe);
int kz_amqp_start_worker_procs();

int kz_amqp_send_worker_event_error(kz_amqp_cmd_ptr cmd);

int kz_pv_get_event_payload(struct sip_msg *msg, pv_param_t *param,	pv_value_t *res);
int kz_pv_get_event_routing_key(struct sip_msg *msg, pv_param_t *param,	pv_value_t *res);
int kz_pv_get_last_query_result(struct sip_msg *msg, pv_param_t *param,	pv_value_t *res);
int kz_pv_get_call_id(struct sip_msg *msg, pv_param_t *param,	pv_value_t *res);
int kz_pv_get_connection_host(struct sip_msg *msg, pv_param_t *param,	pv_value_t *res);


void kz_amqp_cb_ok(kz_amqp_cmd_ptr cmd);
void kz_amqp_cb_error(kz_amqp_cmd_ptr cmd);

const char * kz_event_payload();

#endif /* KZ_AMQP_H_ */

