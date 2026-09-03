/* Reconstructed (2026-09): prototypes of kz_amqp_pipe.c, the script-facing
 * publish/query/subscribe entry points of the 2600hz kazoo module. */
#ifndef KZ_AMQP_PIPE_H_
#define KZ_AMQP_PIPE_H_

#include <sys/time.h>
#include <json.h>
#include "../../core/str.h"
#include "../../core/parser/msg_parser.h"
#include "../../core/events.h"

#ifndef KZ_JSON_OBJ_PTR_DEFINED
#define KZ_JSON_OBJ_PTR_DEFINED
typedef struct json_object *json_obj_ptr;
#endif

int kz_amqp_pipe_send(str *str_exchange, str *str_routing_key, str *str_payload, int pub_flags, char *args);
int kz_amqp_pipe_send_receive(str *str_exchange, str *str_routing_key, str *str_payload, int pub_flags,
		struct timeval *kz_timeout, json_obj_ptr *json_ret, char *args);

int kz_amqp_publish_ex(struct sip_msg *msg, char *exchange, char *routing_key, char *payload, char *_pub_flags, char *args);
int kz_amqp_publish5(struct sip_msg *msg, char *exchange, char *routing_key, char *payload, char *_pub_flags, char *args);
int kz_amqp_publish4(struct sip_msg *msg, char *exchange, char *routing_key, char *payload, char *_pub_flags);
int kz_amqp_publish3(struct sip_msg *msg, char *exchange, char *routing_key, char *payload);

int kz_amqp_query_ex(struct sip_msg *msg, char *exchange, char *routing_key, char *payload, char *_pub_flags, char *args);
int kz_amqp_query5(struct sip_msg *msg, char *exchange, char *routing_key, char *payload, char *_pub_flags, char *args);
int kz_amqp_query4(struct sip_msg *msg, char *exchange, char *routing_key, char *payload, char *_pub_flags);
int kz_amqp_query3(struct sip_msg *msg, char *exchange, char *routing_key, char *payload);

int kz_amqp_query_async_ex(struct sip_msg *msg, char *_exchange, char *_routing_key, char *_payload,
		char *_cb_route, char *_err_route, char *_pub_flags, char *args);
int kz_amqp_query_async7(struct sip_msg *msg, char *_exchange, char *_routing_key, char *_payload,
		char *_cb_route, char *_err_route, char *_pub_flags, char *args);
int kz_amqp_query_async6(struct sip_msg *msg, char *_exchange, char *_routing_key, char *_payload,
		char *_cb_route, char *_err_route, char *_pub_flags);
int kz_amqp_query_async5(struct sip_msg *msg, char *_exchange, char *_routing_key, char *_payload,
		char *_cb_route, char *_err_route);

int kz_amqp_relay(sip_msg_t *msg);
int kz_amqp_stop_relay(sr_event_param_t *evp);


#endif /* KZ_AMQP_PIPE_H_ */
