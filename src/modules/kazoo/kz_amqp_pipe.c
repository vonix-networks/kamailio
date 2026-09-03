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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/timerfd.h>
#include <uuid/uuid.h>
#include <json.h>
#include "../../core/locking.h"
#include "../../core/mem/mem.h"
#include "../../core/timer_proc.h"
#include "../../core/sr_module.h"
#include "../../core/pvar.h"
#include "../../core/mod_fix.h"
#include "../../core/lvalue.h"
#include "../tm/tm_load.h"
#include "../../core/route.h"
#include "../../core/receive.h"
#include "../../core/action.h"
#include "../../core/script_cb.h"

extern int log_stderr;

#include "kz_amqp_pipe.h"
#include "kz_amqp_mem.h"
#include "kz_amqp.h"
#include "kz_json.h"


extern struct tm_binds kz_tmb;
extern tm_xapi_t kz_xtm;

typedef struct json_object *json_obj_ptr;

extern struct timeval kz_qtimeout_tv;

extern pv_spec_t kz_query_timeout_spec;

extern char* last_payload_result;

int kz_amqp_pipe_send(str *str_exchange, str *str_routing_key, str *str_payload, int pub_flags, char * args)
{
	int ret = -1;
    kz_amqp_cmd_ptr cmd = kz_amqp_alloc_pipe_cmd();

	if(cmd == NULL) {
		LM_ERR("failed to allocate kz_amqp_cmd in process %d\n", getpid());
		goto error;
	}

	cmd->exchange = kz_amqp_str_dup(str_exchange);
	cmd->routing_key = kz_amqp_str_dup(str_routing_key);
	cmd->payload = kz_amqp_str_dup(str_payload);
	if(cmd->payload == NULL || cmd->routing_key == NULL || cmd->exchange == NULL) {
		LM_ERR("failed to allocate kz_amqp_cmd parameters in process %d : '%s' , '%s', '%s' \n", getpid(), cmd->exchange, cmd->routing_key, cmd->payload);
		LM_ERR("failed to allocate kz_amqp_cmd parameters in process %d : '%s' , '%s', '%s' \n", getpid(), str_exchange->s, str_routing_key->s, str_payload->s);
		goto error;
	}
	if(args != NULL) {
		cmd->arguments = kz_amqp_string_dup(args);
	}
	cmd->flags = pub_flags;
	cmd->type = KZ_AMQP_CMD_PUBLISH;
	cmd->consumer = getpid();
	if (write(kz_cmd_pipe, &cmd, sizeof(cmd)) != sizeof(cmd)) {
		LM_ERR("failed to publish message to amqp in process %d, write to command pipe: %s\n", getpid(), strerror(errno));
	} else {
		lock_get(&cmd->lock);
		ret = cmd->return_code;
	}

	error:

	if(cmd)
		kz_amqp_free_pipe_cmd(cmd);

	return ret;
}

int kz_amqp_pipe_send_receive(str *str_exchange, str *str_routing_key, str *str_payload, int pub_flags, struct timeval* kz_timeout, json_obj_ptr* json_ret, char* args )
{
	int ret = 1;
    json_obj_ptr json_obj = NULL;
    kz_amqp_cmd_ptr cmd = NULL;
    json_obj_ptr json_body = NULL;

    /* parse json  and add extra fields */
    json_obj = kz_json_parse(str_payload->s);
    if (json_obj == NULL)
	goto error;

    char *payload = (char *)json_object_to_json_string(json_obj);

	cmd = kz_amqp_alloc_pipe_cmd();
	if(cmd == NULL) {
		LM_ERR("failed to allocate kz_amqp_cmd in process %d\n", getpid());
		goto error;
	}
	cmd->exchange = kz_amqp_str_dup(str_exchange);
	cmd->routing_key = kz_amqp_str_dup(str_routing_key);
	cmd->payload = kz_amqp_string_dup(payload);
	cmd->timeout = *kz_timeout;
	cmd->flags = pub_flags;
	if(args != NULL) {
		cmd->arguments = kz_amqp_string_dup(args);
	}
	if(cmd->payload == NULL || cmd->routing_key == NULL || cmd->exchange == NULL) {
		LM_ERR("failed to allocate kz_amqp_cmd parameters in process %d\n", getpid());
		goto error;
	}
	cmd->type = KZ_AMQP_CMD_CALL;
	cmd->consumer = getpid();
	if (write(kz_cmd_pipe, &cmd, sizeof(cmd)) != sizeof(cmd)) {
		LM_ERR("failed to publish message to amqp in process %d, write to command pipe: %s\n", getpid(), strerror(errno));
	} else {
		lock_get(&cmd->lock);
		switch(cmd->return_code) {
		case 200:
			json_body = kz_json_parse(cmd->return_payload);
		    if (json_body == NULL)
			goto error;
		    *json_ret = json_body;
		    ret = 0;
		    break;

		default:
			if(cmd->return_payload) {
				json_body = kz_json_parse(cmd->return_payload);
			    if (json_body != NULL) {
				    *json_ret = json_body;
			    }
			}
			ret = -1;
			break;
		}
	}

 error:
	if(cmd)
		kz_amqp_free_pipe_cmd(cmd);

    if(json_obj)
    	json_object_put(json_obj);

    return ret;
}

int kz_amqp_publish_ex(struct sip_msg* msg, char* exchange, char* routing_key, char* payload, char* _pub_flags, char* args)
{
	str json_s;
	str exchange_s;
	str routing_key_s;
	int pub_flags = 0;
	char *arguments = NULL;
	str args_s;
	json_obj_ptr jobj = NULL;

	if (fixup_get_svalue(msg, (gparam_p)exchange, &exchange_s) != 0) {
		LM_ERR("cannot get exchange string value\n");
		return -1;
	}

	if (fixup_get_svalue(msg, (gparam_p)routing_key, &routing_key_s) != 0) {
		LM_ERR("cannot get routing_key string value\n");
		return -1;
	}

	if (fixup_get_svalue(msg, (gparam_p)payload, &json_s) != 0) {
		LM_ERR("cannot get json string value : %s\n", payload);
		return -1;
	}

	if (routing_key_s.len > MAX_ROUTING_KEY_SIZE) {
		LM_ERR("routing_key size (%d) > max %d\n", routing_key_s.len, MAX_ROUTING_KEY_SIZE);
		return -1;
	}

	jobj = json_tokener_parse(json_s.s);
	if (jobj == NULL) {
		LM_ERR("empty or invalid JSON payload : %.*s\n", json_s.len, json_s.s);
		return -1;
	}

	json_object_put(jobj);

	if (_pub_flags != NULL) {
		str pub_flags_s;
		if (fixup_get_svalue(msg, (gparam_p)_pub_flags, &pub_flags_s) == 0) {
			pub_flags = atoi(pub_flags_s.s);
		}
	}

	if (args != NULL) {
		if (fixup_get_svalue(msg, (gparam_p)_pub_flags, &args_s) == 0) {
			if(args_s.len > 0) {
				jobj = json_tokener_parse(args_s.s);
				if (jobj == NULL) {
					LM_ERR("empty or invalid JSON arguments : %.*s\n", args_s.len, args_s.s);
					return -1;
				}
				json_object_put(jobj);
				arguments = args_s.s;
			}
		}
	}

	return kz_amqp_pipe_send(&exchange_s, &routing_key_s, &json_s, pub_flags, arguments);
}

int kz_amqp_publish5(struct sip_msg* msg, char* exchange, char* routing_key, char* payload, char* _pub_flags, char* args)
{
	return kz_amqp_publish_ex(msg, exchange, routing_key, payload, _pub_flags, args);
}

int kz_amqp_publish4(struct sip_msg* msg, char* exchange, char* routing_key, char* payload, char* _pub_flags)
{
	return kz_amqp_publish_ex(msg, exchange, routing_key, payload, _pub_flags, NULL);
};

int kz_amqp_publish3(struct sip_msg* msg, char* exchange, char* routing_key, char* payload)
{
	return kz_amqp_publish_ex(msg, exchange, routing_key, payload, NULL, NULL);
}


int kz_amqp_query_ex(struct sip_msg* msg, char* exchange, char* routing_key, char* payload, char* _pub_flags, char* args)
{
	str json_s;
	str exchange_s;
	str routing_key_s;
	struct timeval kz_timeout = kz_qtimeout_tv;
	int pub_flags = 0;
	str args_s;
	char *arguments = NULL;
	json_obj_ptr jobj = NULL;

	if(last_payload_result) {
		pkg_free(last_payload_result);
	}
	last_payload_result = NULL;

	if (fixup_get_svalue(msg, (gparam_p)exchange, &exchange_s) != 0) {
		LM_ERR("cannot get exchange string value\n");
		return -1;
	}

	if (fixup_get_svalue(msg, (gparam_p)routing_key, &routing_key_s) != 0) {
		LM_ERR("cannot get routing_key string value\n");
		return -1;
	}

	if (fixup_get_svalue(msg, (gparam_p)payload, &json_s) != 0) {
		LM_ERR("cannot get json string value : %s\n", payload);
		return -1;
	}

	if (routing_key_s.len > MAX_ROUTING_KEY_SIZE) {
		LM_ERR("routing_key size (%d) > max %d\n", routing_key_s.len, MAX_ROUTING_KEY_SIZE);
		return -1;
	}

	jobj = json_tokener_parse(json_s.s);

	if (jobj == NULL) {
		LM_ERR("empty or invalid JSON payload : %*.s\n", json_s.len, json_s.s);
		return -1;
	}

	json_object_put(jobj);

	if (_pub_flags != NULL) {
		str pub_flags_s;
		if (fixup_get_svalue(msg, (gparam_p)_pub_flags, &pub_flags_s) == 0) {
			pub_flags = atoi(pub_flags_s.s);
		}
	}

	if (args != NULL) {
		if (fixup_get_svalue(msg, (gparam_p)_pub_flags, &args_s) == 0) {
			if(args_s.len > 0) {
				jobj = json_tokener_parse(args_s.s);
				if (jobj == NULL) {
					LM_ERR("empty or invalid JSON arguments : %.*s\n", args_s.len, args_s.s);
					return -1;
				}
				json_object_put(jobj);
				arguments = args_s.s;
			}
		}
	}

	if(kz_query_timeout_spec.type != PVT_NONE) {
		pv_value_t pv_val;
		if(pv_get_spec_value( msg, &kz_query_timeout_spec, &pv_val) == 0) {
			if((pv_val.flags & PV_VAL_INT) && pv_val.ri != 0 ) {
				kz_timeout.tv_usec = (pv_val.ri % 1000) * 1000;
				kz_timeout.tv_sec = pv_val.ri / 1000;
				LM_DBG("setting timeout to %i,%i\n", (int) kz_timeout.tv_sec, (int) kz_timeout.tv_usec);
			}
		}
	}

	json_obj_ptr ret = NULL;
	int res = kz_amqp_pipe_send_receive(&exchange_s, &routing_key_s, &json_s, pub_flags, &kz_timeout, &ret, arguments);

	if(ret != NULL) {
		char* strjson = (char*)json_object_to_json_string(ret);
		int len = strlen(strjson);
		char* value = pkg_malloc(len+1);
		memcpy(value, strjson, len);
		value[len] = '\0';
		last_payload_result = value;
		json_object_put(ret);
	}

	return res != 0 ? -1 : 1;
};

int kz_amqp_query5(struct sip_msg* msg, char* exchange, char* routing_key, char* payload, char* _pub_flags, char* args)
{
	return kz_amqp_query_ex(msg, exchange, routing_key, payload, _pub_flags, args);
}

int kz_amqp_query4(struct sip_msg* msg, char* exchange, char* routing_key, char* payload, char* _pub_flags)
{
	return kz_amqp_query_ex(msg, exchange, routing_key, payload, _pub_flags, NULL);
}

int kz_amqp_query3(struct sip_msg* msg, char* exchange, char* routing_key, char* payload)
{
	return kz_amqp_query_ex(msg, exchange, routing_key, payload, NULL, NULL);
};



int kz_amqp_query_async_ex(struct sip_msg* msg, char* _exchange, char* _routing_key, char* _payload, char* _cb_route, char* _err_route, char* _pub_flags, char* args)
{
	str json_s;
	str exchange_s;
	str routing_key_s;
	str cb_route_s;
	str err_route_s;
	unsigned int timeout = kz_qtimeout_tv.tv_sec * 1000 + (kz_qtimeout_tv.tv_usec / 1000);
    int ret = -1;
    json_obj_ptr json_obj = NULL;
	kz_amqp_cmd_ptr cmd = NULL;
	unsigned int hash_index = 0;
	unsigned int label = 0;
	tm_cell_t *t = 0;
	int err_route = 0;
	int route = 0;
	int pub_flags = 0;
	str args_s;
	char *arguments = NULL;

	if (fixup_get_svalue(msg, (gparam_p)_exchange, &exchange_s) != 0) {
		LM_ERR("cannot get exchange string value\n");
		goto error;
	}

	if (fixup_get_svalue(msg, (gparam_p)_routing_key, &routing_key_s) != 0) {
		LM_ERR("cannot get routing_key string value\n");
		goto error;
	}

	if (fixup_get_svalue(msg, (gparam_p)_payload, &json_s) != 0) {
		LM_ERR("cannot get json string value : %s\n", _payload);
		goto error;
	}

	json_obj = json_tokener_parse(json_s.s);
	if (json_obj == NULL) {
		LM_ERR("empty or invalid JSON payload : %*.s\n", json_s.len, json_s.s);
		return -1;
	}
	json_object_put(json_obj);

	if (routing_key_s.len > MAX_ROUTING_KEY_SIZE) {
		LM_ERR("routing_key size (%d) > max %d\n", routing_key_s.len, MAX_ROUTING_KEY_SIZE);
		return -1;
	}

	if (fixup_get_svalue(msg, (gparam_p)_cb_route, &cb_route_s) != 0) {
		LM_ERR("cannot get cb_route value\n");
		return -1;
	}

	if((route = route_get(&onreply_rt, cb_route_s.s)) == -1) {
		LM_ERR("cannot find reply route %s\n", cb_route_s.s);
		return -1;
	}

	if (fixup_get_svalue(msg, (gparam_p)_err_route, &err_route_s) != 0) {
		LM_ERR("cannot get err_route value\n");
		return -1;
	}

	if((err_route = route_get(&failure_rt, err_route_s.s)) == -1) {
		LM_ERR("cannot find error route %s\n", err_route_s.s);
		return -1;
	}

	if (_pub_flags != NULL) {
		str pub_flags_s;
		if (fixup_get_svalue(msg, (gparam_p)_pub_flags, &pub_flags_s) == 0) {
			pub_flags = atoi(pub_flags_s.s);
		}
	}

	if (args != NULL) {
		if (fixup_get_svalue(msg, (gparam_p)_pub_flags, &args_s) == 0) {
			if(args_s.len > 0) {
				json_obj = json_tokener_parse(args_s.s);
				if (json_obj == NULL) {
					LM_ERR("empty or invalid JSON arguments : %.*s\n", args_s.len, args_s.s);
					return -1;
				}
				json_object_put(json_obj);
				arguments = args_s.s;
			}
		}
	}

	if(kz_query_timeout_spec.type != PVT_NONE) {
		pv_value_t pv_val;
		if(pv_get_spec_value( msg, &kz_query_timeout_spec, &pv_val) == 0) {
			if((pv_val.flags & PV_VAL_INT) && pv_val.ri != 0 ) {
				timeout = pv_val.ri;
				LM_DBG("setting timeout to %i ms\n", timeout);
			} else if((pv_val.flags & PV_VAL_STR) && pv_val.rs.len > 0) {
				unsigned int timeout_from_str = atoi(pv_val.rs.s);
				if(timeout_from_str > 0) {
					timeout = timeout_from_str;
					LM_DBG("setting timeout to %i ms\n", timeout);
				} else {
					LM_WARN("error converting timeout spec to int => %*.s\n", pv_val.rs.len, pv_val.rs.s);
				}
			}
		}
	}

	t = kz_tmb.t_gett();
	if (t == NULL || t == T_UNDEFINED) {
		if(kz_tmb.t_newtran(msg)<0) {
			LM_ERR("cannot create the transaction\n");
			goto error;
		}
		t = kz_tmb.t_gett();
		if (t == NULL || t == T_UNDEFINED) {
			LM_ERR("cannot look up the transaction\n");
			goto error;
		}
	}

	kz_xtm.t_on_failure(err_route);
	kz_tmb.set_fr(msg, 0, timeout);

	if (kz_tmb.t_suspend(msg, &hash_index, &label) < 0) {
		LM_ERR("t_suspend() failed\n");
		goto error;
	}

	cmd = kz_amqp_alloc_pipe_cmd();
	if(cmd == NULL) {
		LM_ERR("failed to allocate kz_amqp_cmd in process %d\n", getpid());
		goto error;
	}
	cmd->exchange = kz_amqp_str_dup(&exchange_s);
	cmd->routing_key = kz_amqp_str_dup(&routing_key_s);
	cmd->payload = kz_amqp_str_dup(&json_s);
	cmd->cb_route = kz_amqp_str_dup(&cb_route_s);
	cmd->err_route = kz_amqp_str_dup(&err_route_s);
	cmd->t_hash = hash_index;
	cmd->t_label = label;
	cmd->t_reply_route = route;
	cmd->t_failure_route = err_route;
	cmd->flags = pub_flags;
	if(arguments != NULL) {
		cmd->arguments = kz_amqp_string_dup(args);
	}
	if(cmd->payload == NULL || cmd->routing_key == NULL || cmd->exchange == NULL) {
		LM_ERR("failed to allocate kz_amqp_cmd parameters in process %d\n", getpid());
		goto error;
	}
	cmd->type = KZ_AMQP_CMD_ASYNC_CALL;
	cmd->consumer = getpid();
	if (write(kz_cmd_pipe, &cmd, sizeof(cmd)) != sizeof(cmd)) {
		LM_ERR("failed to publish message to amqp in process %d, write to command pipe: %s\n", getpid(), strerror(errno));
		goto error;
	}
	return 0;

error:
	if(cmd)
		kz_amqp_free_pipe_cmd(cmd);

	if(hash_index | label)
		kz_tmb.t_cancel_suspend(hash_index, label);

	return ret;
};

int kz_amqp_query_async7(struct sip_msg* msg, char* _exchange, char* _routing_key, char* _payload, char* _cb_route, char* _err_route, char* _pub_flags, char* args)
{
	return kz_amqp_query_async_ex(msg, _exchange, _routing_key, _payload, _cb_route, _err_route, _pub_flags, args);
}

int kz_amqp_query_async6(struct sip_msg* msg, char* _exchange, char* _routing_key, char* _payload, char* _cb_route, char* _err_route, char* _pub_flags)
{
	return kz_amqp_query_async_ex(msg, _exchange, _routing_key, _payload, _cb_route, _err_route, _pub_flags, NULL);
}

int kz_amqp_query_async5(struct sip_msg* msg, char* _exchange, char* _routing_key, char* _payload, char* _cb_route, char* _err_route)
{
	return kz_amqp_query_async_ex(msg, _exchange, _routing_key, _payload, _cb_route, _err_route, NULL, NULL);
}

int kz_amqp_relay(sip_msg_t* msg)
{
    int ret = -1;
    json_obj_ptr json_obj = NULL;
	kz_amqp_cmd_ptr cmd = NULL;
	str body = {msg->buf, msg->len};

	json_obj = json_tokener_parse(msg->buf);
	if (json_obj==NULL) {
		  LM_ERR("empty or invalid JSON payload : %*.s\n", msg->len, msg->buf);
		  return -1;
	}
	json_object_put(json_obj);

	cmd = kz_amqp_alloc_pipe_cmd();
	if(cmd == NULL) {
		LM_ERR("failed to allocate kz_amqp_cmd in process %d\n", getpid());
		goto error;
	}

	cmd->payload = kz_amqp_str_dup(&body);
	cmd->relay_connection_id = msg->rcv.proto_reserved1;
	cmd->type = KZ_AMQP_CMD_RELAY;
	cmd->consumer = getpid();
	if (write(kz_cmd_pipe, &cmd, sizeof(cmd)) != sizeof(cmd)) {
		LM_ERR("failed to publish message to amqp in process %d, write to command pipe: %s\n", getpid(), strerror(errno));
	} else {
		lock_get(&cmd->lock);
		ret = cmd->return_code;
	}

error:
	if(cmd)
		kz_amqp_free_pipe_cmd(cmd);

    return ret;
};

int kz_amqp_stop_relay(sr_event_param_t *evp)
{
    int ret = -1;
	kz_amqp_cmd_ptr cmd = NULL;

	cmd = kz_amqp_alloc_pipe_cmd();
	if(cmd == NULL) {
		LM_ERR("failed to allocate kz_amqp_cmd in process %d\n", getpid());
		goto error;
	}

	cmd->relay_connection_id = evp->rcv->proto_reserved1;
	cmd->type = KZ_AMQP_CMD_STOP_RELAY;
	cmd->consumer = getpid();
	if (write(kz_cmd_pipe, &cmd, sizeof(cmd)) != sizeof(cmd)) {
		LM_ERR("failed to publish message to amqp in process %d, write to command pipe: %s\n", getpid(), strerror(errno));
	} else {
		lock_get(&cmd->lock);
		ret = cmd->return_code;
	}

error:
	if(cmd)
		kz_amqp_free_pipe_cmd(cmd);

    return ret;
};

/*
int kz_amqp_stop_relay(sip_msg_t* msg)
{
    int ret = -1;
	kz_amqp_cmd_ptr cmd = NULL;

	cmd = kz_amqp_alloc_pipe_cmd();
	if(cmd == NULL) {
		LM_ERR("failed to allocate kz_amqp_cmd in process %d\n", getpid());
		goto error;
	}

	cmd->relay_connection_id = msg->rcv.proto_reserved1;
	cmd->type = KZ_AMQP_CMD_STOP_RELAY;
	cmd->consumer = getpid();
	if (write(kz_cmd_pipe, &cmd, sizeof(cmd)) != sizeof(cmd)) {
		LM_ERR("failed to publish message to amqp in process %d, write to command pipe: %s\n", getpid(), strerror(errno));
	} else {
		lock_get(&cmd->lock);
		ret = cmd->return_code;
	}

error:
	if(cmd)
		kz_amqp_free_pipe_cmd(cmd);

    return ret;
};
*/

/*
int kz_amqp_subscribe(struct sip_msg* msg, char* payload)
{
	str exchange_s = STR_NULL;
	str queue_s = STR_NULL;
	str payload_s = STR_NULL;
	str key_s = STR_NULL;
	str subkey_s = STR_NULL;
	int no_ack = 1;
	int federate = 0;
	int consistent_worker = 0;
	str* consistent_worker_key = NULL;
	int wait_for_consumer_ack = 1;
	kz_amqp_queue_ptr queue = NULL;
	kz_amqp_exchange_ptr exchange = NULL;
	kz_amqp_exchange_binding_ptr exchange_binding = NULL;
	kz_amqp_routings_ptr routing = NULL;
	str* event_key = NULL;
    str* event_subkey = NULL;



	json_obj_ptr json_obj = NULL;
	struct json_object* tmpObj = NULL;

	if (fixup_get_svalue(msg, (gparam_p)payload, &payload_s) != 0) {
		LM_ERR("cannot get payload value\n");
		return -1;
	}

	json_obj = kz_json_parse(payload_s.s);
	if (json_obj == NULL)
		return -1;
    

	json_extract_field("exchange", exchange_s);
	json_extract_field("queue", queue_s);
	json_extract_field("event_key", key_s);
	json_extract_field("event_subkey", subkey_s);

	if(key_s.len != 0)
		event_key = &key_s;

	if(subkey_s.len != 0)
		event_subkey = &subkey_s;

	tmpObj = kz_json_get_object(json_obj, "no_ack");
	if(tmpObj != NULL) {
		no_ack = json_object_get_int(tmpObj);
	}

	tmpObj = kz_json_get_object(json_obj, "wait_for_consumer_ack");
	if(tmpObj != NULL) {
		wait_for_consumer_ack = json_object_get_int(tmpObj);
	}

	tmpObj = kz_json_get_object(json_obj, "federate");
	if(tmpObj != NULL) {
		federate = json_object_get_int(tmpObj);
	}

	tmpObj = kz_json_get_object(json_obj, "consistent-worker");
	if(tmpObj != NULL) {
		consistent_worker = json_object_get_int(tmpObj);
	}

	tmpObj = kz_json_get_object(json_obj, "consistent-worker-key");
	if(tmpObj != NULL) {
		consistent_worker_key = kz_str_dup_from_char((char*)json_object_get_string(tmpObj));
	}

	tmpObj = kz_json_get_object(json_obj, "exchange-bindings");
	if(tmpObj != NULL) {
		exchange_binding = kz_amqp_exchange_binding_from_json(tmpObj);
	}

	tmpObj = kz_json_get_object(json_obj, "routing");
	if(tmpObj != NULL) {
		routing = kz_amqp_routing_from_json(tmpObj);
	}

	if(routing == NULL) {
		LM_INFO("creating empty routing key : %s\n", payload_s.s);
		routing = kz_amqp_routing_new("");
	}

	tmpObj = kz_json_get_object(json_obj, "exchange-def");
	if(tmpObj == NULL) {
		tmpObj = json_obj;
	}
	exchange = kz_amqp_exchange_from_json(&exchange_s, tmpObj);

	tmpObj = kz_json_get_object(json_obj, "queue-def");
	if(tmpObj == NULL) {
		tmpObj = json_obj;
	}
	queue = kz_amqp_queue_from_json(&queue_s, tmpObj);

	kz_amqp_bind_ptr bind = kz_amqp_bind_alloc(exchange, exchange_binding, queue, routing, event_key, event_subkey);
	if(bind == NULL) {
		LM_ERR("Could not allocate bind struct\n");
		goto error;
	}

	bind->no_ack = no_ack;
	bind->wait_for_consumer_ack = wait_for_consumer_ack;
	bind->federate = federate;
	bind->consistent_worker = consistent_worker;
	bind->consistent_worker_key = consistent_worker_key;


	kz_amqp_binding_ptr binding = shm_malloc(sizeof(kz_amqp_binding));
	if(binding == NULL) {
		LM_ERR("Could not allocate binding struct\n");
		goto error;
	}
	memset(binding, 0, sizeof(kz_amqp_binding));

	if(kz_bindings->head == NULL)
		kz_bindings->head = binding;

	if(kz_bindings->tail != NULL)
		kz_bindings->tail->next = binding;

	kz_bindings->tail = binding;
	binding->bind = bind;
	bindings_count++;

	if(json_obj != NULL)
		json_object_put(json_obj);

	return 1;

error:
	if(binding != NULL)
		shm_free(binding);

	if(json_obj != NULL)
		json_object_put(json_obj);

	return -1;

}

int kz_amqp_subscribe_simple(struct sip_msg* msg, char* exchange, char* exchange_type, char* queue, char* routing_key)
{
	str exchange_s;
	str exchange_type_s;
	str queue_s;
	str routing_key_s;
	kz_amqp_exchange_ptr exchange_ptr = NULL;
	kz_amqp_queue_ptr queue_ptr = NULL;
	kz_amqp_routings_ptr routing_ptr = NULL;


	if (fixup_get_svalue(msg, (gparam_p)exchange, &exchange_s) != 0) {
		LM_ERR("cannot get exchange string value\n");
		return -1;
	}

	if (fixup_get_svalue(msg, (gparam_p)exchange_type, &exchange_type_s) != 0) {
		LM_ERR("cannot get exchange type string value\n");
		return -1;
	}

	if (fixup_get_svalue(msg, (gparam_p)queue, &queue_s) != 0) {
		LM_ERR("cannot get queue string value\n");
		return -1;
	}

	if (fixup_get_svalue(msg, (gparam_p)routing_key, &routing_key_s) != 0) {
		LM_ERR("cannot get routing_key string value\n");
		return -1;
	}

	exchange_ptr = kz_amqp_exchange_new(&exchange_s, &exchange_type_s);
	queue_ptr = kz_amqp_queue_new(&queue_s);
	routing_ptr = kz_amqp_routing_new(routing_key_s.s);

	kz_amqp_bind_ptr bind = kz_amqp_bind_alloc(exchange_ptr, NULL, queue_ptr, routing_ptr, NULL, NULL);
	if(bind == NULL) {
		LM_ERR("Could not allocate bind struct\n");
		goto error;
	}

	bind->no_ack = 1;

	kz_amqp_binding_ptr binding = shm_malloc(sizeof(kz_amqp_binding));
	if(binding == NULL) {
		LM_ERR("Could not allocate binding struct\n");
		goto error;
	}
	memset(binding, 0, sizeof(kz_amqp_binding));

	if(kz_bindings->head == NULL)
		kz_bindings->head = binding;

	if(kz_bindings->tail != NULL)
		kz_bindings->tail->next = binding;

	kz_bindings->tail = binding;
	binding->bind = bind;
	bindings_count++;

    return 1;

error:
    if(binding != NULL)
    	shm_free(binding);

	return -1;

}
*/


