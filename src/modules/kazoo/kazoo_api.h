/*
 * kazoo_api.h
 *
 *  Created on: Sep 11, 2018
 *      Author: root
 */

#ifndef SRC_MODULES_KAZOO3_KAZOO_API_H_
#define SRC_MODULES_KAZOO3_KAZOO_API_H_


int kz_amqp_add_connection(char* url_str);
int kz_amqp_subscribe(char* payload);
int kz_amqp_subscribe_simple(char* str_exchange, char* str_exchange_type, char* str_queue, char* str_routing_key);
int kz_pv_get_node(struct sip_msg *msg, pv_param_t *param,	pv_value_t *res);
int kz_pv_set_ki(struct sip_msg* msg, pv_param_t *param, int op, pv_value_t *val);
int kz_pv_get_gregorian_timestamp(struct sip_msg *msg, pv_param_t *param, pv_value_t *res);
int kz_pv_get_interaction_id(struct sip_msg *msg, pv_param_t *param, pv_value_t *res);
int kz_amqp_declare_exchange(char* payload);



#endif /* SRC_MODULES_KAZOO3_KAZOO_API_H_ */
