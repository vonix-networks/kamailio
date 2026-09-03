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
#include <event.h>
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
#include "kz_amqp_util.h"


extern int log_stderr;

extern struct tm_binds kz_tmb;

typedef struct json_object *json_obj_ptr;

char* last_payload_result = NULL;

int kz_pv_get_last_query_result(struct sip_msg *msg, pv_param_t *param,	pv_value_t *res)
{
	return last_payload_result == NULL ? pv_get_null(msg, param, res) : pv_get_strzval(msg, param, res, last_payload_result);
}

char* kz_eventData = NULL;
char* kz_eventKey = NULL;
char* kz_call_id = NULL;

int kz_pv_get_event_payload(struct sip_msg *msg, pv_param_t *param,	pv_value_t *res)
{
	return kz_eventData == NULL ? pv_get_null(msg, param, res) : pv_get_strzval(msg, param, res, kz_eventData);
}

const char * kz_event_payload()
{
	return kz_eventData;
}

int kz_pv_get_event_routing_key(struct sip_msg *msg, pv_param_t *param,	pv_value_t *res)
{
	return kz_eventKey == NULL ? pv_get_null(msg, param, res) : pv_get_strzval(msg, param, res, kz_eventKey);
}

int kz_pv_get_call_id(struct sip_msg *msg, pv_param_t *param,	pv_value_t *res)
{
	if (msg->callid && msg->callid->body.len > 0) {
		return pv_get_strval(msg, param, res, &msg->callid->body);
	} else {
		return kz_call_id == NULL ? pv_get_null(msg, param, res) : pv_get_strzval(msg, param, res, kz_call_id);
	}
}

int kz_amqp_consumer_fire_event(char *eventkey)
{
	sip_msg_t *fmsg;
	struct run_act_ctx ctx;
	int rtb, rt;

	LM_DBG("searching event_route[%s]\n", eventkey);
	rt = route_lookup(&event_rt, eventkey);
	if (rt < 0 || event_rt.rlist[rt] == NULL)
	{
		LM_DBG("route %s does not exist\n", eventkey);
		return -2;
	}
	LM_DBG("executing event_route[%s] (%d)\n", eventkey, rt);
	fmsg = faked_msg_get_next();
	rtb = get_route_type();
	set_route_type(ONEVENT_ROUTE);
	if (exec_pre_script_cb(fmsg, EVENT_CB_TYPE)!=0 ) {
		init_run_actions_ctx(&ctx);
		run_top_route(event_rt.rlist[rt], fmsg, 0);
		exec_post_script_cb(fmsg, EVENT_CB_TYPE);
		ksr_msg_env_reset();
	}
	set_route_type(rtb);
	return 0;
}

void kz_fix_script_key(char *buffer)
{
    char * p;
    for (p=buffer ; *p; p++) {
    	*p = tolower(*p);
    	if(*p == '_') *p = '-';
    	if(*p == '.') *p = '-';
    }
}

int kz_amqp_ws_relay(kz_amqp_consumer_delivery_ptr Evt)
{
	sr_event_param_t evp = {0};
	ws_event_info_t wsev;

	memset(&wsev, 0, sizeof(ws_event_info_t));
	wsev.type = SREV_TCP_WS_FRAME_OUT;
	wsev.buf = Evt->payload;
	wsev.len = strlen(Evt->payload);
	wsev.id = Evt->t_hash;
	evp.data = (void *)&wsev;
	return sr_event_exec(SREV_TCP_WS_FRAME_OUT, &evp);
}

void kz_amqp_consumer_event(kz_amqp_consumer_delivery_ptr Evt)
{
    json_obj_ptr json_obj = NULL;
    str ev_name = {0, 0}, ev_category = {0, 0};
    char *buffer = NULL;

    json_obj = kz_json_parse(Evt->payload);
    if (json_obj == NULL) {
		return;
    }

    kz_eventData = Evt->payload;
    if(Evt->routing_key) {
		kz_eventKey = Evt->routing_key;
    }

    buffer = (char*) pkg_malloc(1024);
    if(!buffer) {
    	LM_ERR("no more pkg memory for allocating 1024 bytes\n");
    	goto error;
    }
    memset(buffer, 0, 1024);

    char* key = (Evt->event_key == NULL ? dbk_consumer_event_key.s : Evt->event_key);
    char* subkey = (Evt->event_subkey == NULL ? dbk_consumer_event_subkey.s : Evt->event_subkey);

    json_extract_field(key, ev_category);
    if(ev_category.len == 0 && Evt->event_key) {
	    ev_category.s = Evt->event_key;
	    ev_category.len = strlen(Evt->event_key);
    }

    json_extract_field(subkey, ev_name);
    if(ev_name.len == 0 && Evt->event_subkey) {
	    ev_name.s = Evt->event_subkey;
	    ev_name.len = strlen(Evt->event_subkey);
    }

    sprintf(buffer, "kazoo:consumer-event-%.*s-%.*s",ev_category.len, ev_category.s, ev_name.len, ev_name.s);
    kz_fix_script_key(buffer);
    if(kz_amqp_consumer_fire_event(buffer) != 0) {
        sprintf(buffer, "kazoo:consumer-event-%.*s",ev_category.len, ev_category.s);
        kz_fix_script_key(buffer);
        if(kz_amqp_consumer_fire_event(buffer) != 0) {
            sprintf(buffer, "kazoo:consumer-event-%s-%s", key, subkey);
            kz_fix_script_key(buffer);
            if(kz_amqp_consumer_fire_event(buffer) != 0) {
                sprintf(buffer, "kazoo:consumer-event-%s", key);
                kz_fix_script_key(buffer);
				if(kz_amqp_consumer_fire_event(buffer) != 0) {
					sprintf(buffer, "kazoo:consumer-event");
					if(kz_amqp_consumer_fire_event(buffer) != 0) {
						LM_ERR("kazoo:consumer-event not found");
					}
				}
            }
        }
    }

    pkg_free(buffer);

error:
	if(json_obj)
    	json_object_put(json_obj);

	kz_eventData = NULL;
	kz_eventKey = NULL;
}


int consumer = 0;


int kz_amqp_on_reply(unsigned int t_hash, unsigned int t_label, int route)
{
	int ret;
	struct action *on_reply = onreply_rt.rlist[route];
	ret = kz_tmb.t_continue(t_hash, t_label, on_reply);
	ksr_msg_env_reset();
	return ret;
}

int kz_amqp_on_reply_ex(kz_amqp_consumer_delivery_ptr Evt)
{
	int ret;
	struct action *route = NULL;
	tm_cell_t *t;

	if (kz_tmb.t_lookup_ident(&t, Evt->t_hash, Evt->t_label) < 0) {
		LM_DBG("TRANSACTION NOT FOUND %i: %i : %s\n", Evt->t_hash, Evt->t_label, Evt->payload);
		return -1;
	}

	if (!(t->flags & T_ASYNC_SUSPENDED)) {
		LM_DBG("transaction is not suspended [%u:%u]\n", Evt->t_hash, Evt->t_label);
		kz_tmb.unref_cell(t);
		return -2;
	}

	if(Evt->t_code == 200) {
		route = onreply_rt.rlist[Evt->t_reply_route];
	} else {
		route = failure_rt.rlist[Evt->t_failure_route];
	}

	ret = kz_tmb.t_continue(Evt->t_hash, Evt->t_label, route);
	ksr_msg_env_reset();

	kz_tmb.unref_cell(t);

	return ret;

}

void kz_amqp_cb_ok(kz_amqp_cmd_ptr cmd)
{
	int n = route_lookup(&main_rt, cmd->cb_route);
	if(n==-1) {
		/* route block not found in the configuration file */
		return;
	}
	struct action *a = main_rt.rlist[n];
	kz_tmb.t_continue(cmd->t_hash, cmd->t_label, a);
	ksr_msg_env_reset();
}

void kz_amqp_cb_error(kz_amqp_cmd_ptr cmd)
{
	int n = route_lookup(&main_rt, cmd->err_route);
	if(n==-1) {
		/* route block not found in the configuration file */
		return;
	}
	struct action *a = main_rt.rlist[n];
	kz_tmb.t_continue(cmd->t_hash, cmd->t_label, a);
	ksr_msg_env_reset();
}
