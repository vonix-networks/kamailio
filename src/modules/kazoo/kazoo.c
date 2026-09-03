/*
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

#include "../../core/sr_module.h"
#include "../../lib/srdb1/db.h"
#include "../../core/dprint.h"
#include "../../core/cfg/cfg.h"
#include "../../core/cfg/cfg_struct.h"

#include "../tm/tm_load.h"
#include "../htable/ht_api.h"
#include "../htable/api.h"

#include "../sl/sl.h"

#include "kz_sca_common.h"
#include "kz_sca_params.h"
#include "kz_sca_call_info_extended.h"
#include "kz_sca_notify.h"

#include "kz_amqp_pipe.h"
#include "kz_amqp_util.h"
#include "kz_amqp_mem.h"
#include "kz_amqp.h"

#include "kz_json.h"
#include "kz_fixup.h"
#include "kz_trans.h"
#include "kz_pua.h"
#include "kz_jwt.h"
#include "kazoo_api.h"

#define DBK_DEFAULT_NO_CONSUMERS 1
#define DBK_DEFAULT_NO_WORKERS 8
#define DBK_DEFAULT_CONNECT_TIMEOUT 20
#define DBK_DEFAULT_CHANNELS 25

#define AMQP_WORKERS_RANKING PROC_XWORKER

#define DEFAULT_INCLUDE_ENTITY 1
#define DEFAULT_USE_ENTITY 1

#define DEFAULT_RELAY_ACTIVE 0

extern int log_stderr;

struct tm_binds kz_tmb;
tm_xapi_t kz_xtm;
htable_api_t kz_ht_api;
sl_api_t kz_sl_api;
struct rr_binds kz_rrb;

int kz_sca_mod_init(void);
int kz_sca_child_init(int rank);
int kz_sca_mod_destroy(void);
int kz_sca_handle_subscribe_0_f(sip_msg_t *msg);
int kz_sca_call_info_update_0_f(sip_msg_t *msg);

static int mod_init(void);
static int  mod_child_init(int rank);
static int fire_init_event(int rank);
static void mod_destroy(void);

stat_var *kz_stat_event_delivered;
stat_var *kz_stat_event_received;
stat_var *kz_stat_messages_received;
stat_var *kz_stat_messages_received_self;
stat_var *kz_stat_messages_published;
stat_var *kz_stat_messages_returned;
stat_var *kz_stat_transactions_published;
stat_var *kz_stat_transactions_received;
stat_var *kz_stat_transactions_expired;
stat_var *kz_stat_transactions_delivered;
stat_var *kz_stat_transactions_returned;
stat_var *kz_stat_calls_published;
stat_var *kz_stat_calls_received;
stat_var *kz_stat_calls_delivered;
stat_var *kz_stat_calls_returned;
stat_var *kz_stat_relay_published;
stat_var *kz_stat_relay_received;
stat_var *kz_stat_relay_delivered;
stat_var *kz_stat_relay_returned;

char hostname[HOST_NAME_MAX+1];
str dbk_node_name = str_init("kamailio");
str dbk_node_hostname = { 0, 0 };
str dbk_reg_fs_path = { 0, 0 };

str kz_xavp_status = str_init("amqpc");
static sr_xavp_t *kz_xavp_status_head = NULL;
sr_xavp_t **kz_xavp_status_crt = &kz_xavp_status_head;
gen_lock_t *kz_status_lock = NULL;


str kz_xavp_keys = str_init("keys");
static sr_xavp_t *kz_xavp_keys_head = NULL;
sr_xavp_t **kz_xavp_keys_crt = &kz_xavp_keys_head;
gen_lock_t *kz_keys_lock = NULL;

str dbk_default_worker_name_template = str_init("${Node}@${HostName}-${Listener-Name}<${Process}.${Process-PID}.${Channel-ID}>");
str kz_deliver_to_pid = str_init("Deliver-To-PID");

str dbk_primary_zone_name = str_init("local");
int kz_enable_clock_diff_warning = 0;

int dbk_channels = DBK_DEFAULT_CHANNELS;
int dbk_consumer_processes = DBK_DEFAULT_NO_CONSUMERS;
int dbk_consumer_workers = DBK_DEFAULT_NO_WORKERS;
int dbk_connect_timeout = DBK_DEFAULT_CONNECT_TIMEOUT;

struct timeval kz_qtimeout_tv = (struct timeval){2,0};

str kz_json_escape_str = str_init("%");
char kz_json_escape_char = '%';

str dbk_consumer_event_key = str_init("Event-Category");
str dbk_consumer_event_subkey = str_init("Event-Name");

int dbk_pua_mode = 1;
db_locking_t kz_pua_lock_type = DB_LOCKING_WRITE;
int dbk_use_heartbeats = -1;

int startup_time = 0;

int kz_ws_close_route = 0;

int *kz_worker_pipes_fds = NULL;
int kz_cmd_pipe = 0;
int  kz_cmd_pipe_fds[2] = {-1,-1};

/* database connection */
db1_con_t *kz_pa_db = NULL;
db_func_t kz_pa_dbf;
str kz_presentity_table = str_init("presentity");
str kz_db_url = {0,0};

str kz_amqps_ca_cert = {0,0};
str kz_amqps_cert = {0,0};
str kz_amqps_key = {0,0};
int kz_amqps_verify_peer = 1;
int kz_amqps_verify_hostname = 1;

str kz_query_timeout_avp = {0,0};
pv_spec_t kz_query_timeout_spec;

str kz_app_name = str_init(NAME);

struct cfg_group_kz_amqp default_kz_amqp_cfg = {
		L_NOTICE
	};

void	*kz_amqp_cfg = &default_kz_amqp_cfg;

cfg_def_t	kz_amqp_cfg_def[] = {
	{"log_level", CFG_VAR_INT, 0, 0, 0, 0, "amqp log level"},
	{0, 0, 0, 0, 0, 0}
};


struct cfg_group_kz_relay default_kz_relay_cfg = {
		DEFAULT_RELAY_ACTIVE
	};

void	*kz_relay_cfg = &default_kz_relay_cfg;

cfg_def_t	kz_relay_cfg_def[] = {
	{"active", CFG_VAR_INT, 0, 0, 0, 0, "activate ws <-> amqp relay"},
	{0, 0, 0, 0, 0, 0}
};

struct cfg_group_kz_presence default_kz_presence_cfg = {
		DEFAULT_INCLUDE_ENTITY,
		DEFAULT_USE_ENTITY,
		DIALOGINFO_BODY_BUFFER_SIZE,
		PRESENCE_BODY_BUFFER_SIZE,
		MWI_BODY_BUFFER_SIZE
	};

void	*kz_presence_cfg = &default_kz_presence_cfg;

cfg_def_t	kz_presence_cfg_def[] = {
	{"include_entity", CFG_VAR_INT, 0, 0, 0, 0, "include entity in body of payloads"},
	{"use_full_entity", CFG_VAR_INT, 0, 0, 0, 0, "use full entity in notify"},
	{"max_dialoginfo_xml_body_size", CFG_VAR_INT, 0, 0, 0, 0, "max xml body in dialoginfo notify"},
	{"max_presence_xml_body_size", CFG_VAR_INT, 0, 0, 0, 0, "max xml body in presence notify"},
	{"max_mwi_xml_body_size", CFG_VAR_INT, 0, 0, 0, 0, "max xml body in mwi notify"},
	{0, 0, 0, 0, 0, 0}
};

#define KZ_SCA_DEFAULT_DB_UPDATE_INTERVAL 300
#define KZ_SCA_DEFAULT_HASH_TABLE_SIZE -1
#define KZ_SCA_DEFAULT_CALL_INFO_MAX_EXPIRES 3600
#define KZ_SCA_DEFAULT_LINE_SEIZE_MAX_EXPIRES 15
#define KZ_SCA_DEFAULT_PURGE_EXPIRED_INTERVAL 300

struct cfg_group_kz_sca default_kz_sca_cfg = {
	KZ_SCA_DEFAULT_DB_UPDATE_INTERVAL,
	KZ_SCA_DEFAULT_HASH_TABLE_SIZE,
	KZ_SCA_DEFAULT_CALL_INFO_MAX_EXPIRES,
	KZ_SCA_DEFAULT_LINE_SEIZE_MAX_EXPIRES,
	KZ_SCA_DEFAULT_PURGE_EXPIRED_INTERVAL,
	STR_STATIC_INIT("kz_sca_subscriptions"),
	STR_STATIC_INIT("kz_sca_state"),
	STR_NULL,
	STR_NULL,
	STR_STATIC_INIT("$(xavp(hf=>X-AUTH-Token){re.subst,/(.*)@(.*)/\\1/})"),
	STR_STATIC_INIT("kz_presence_monitoring_aliases"),
	STR_STATIC_INIT("Presence-Publishing-Aliases"),
	STR_STATIC_INIT("From-Realm"),
	1,
	L_DBG,
	0
};

void	*kz_sca_cfg = &default_kz_sca_cfg;

cfg_def_t	kz_sca_cfg_def[] = {
	{"db_update_interval", CFG_VAR_INT, 0, 0, 0, 0, "interval between db updates"},
	{"hash_table_size", CFG_VAR_INT, 0, 0, 0, 0, "hash table size"},
	{"call_info_max_expires", CFG_VAR_INT, 0, 0, 0, 0, "call_info max expires"},
	{"line_seize_max_expires", CFG_VAR_INT, 0, 0, 0, 0, "line_seize max expires"},
	{"purge_expired_interval", CFG_VAR_INT, 0, 0, 0, 0, "purge expired interval"},
	{"subs_table", CFG_VAR_STR, 0, 0, 0, 0, "subscriptions table name"},
	{"state_table", CFG_VAR_STR, 0, 0, 0, 0, "appearances state table name"},
	{"outbound_proxy", CFG_VAR_STR, 0, 0, 0, 0, "outbound proxy uri"},
	{"server_address", CFG_VAR_STR, 0, 0, 0, 0, "server address"},
	{"endpoint_spec", CFG_VAR_STR, 0, 0, 0, 0, "endpoint spec"},
	{"aliases_view", CFG_VAR_STR, 0, 0, 0, 0, "aliases view"},
	{"aliases_path", CFG_VAR_STR, 0, 0, 0, 0, "aliases json path"},
	{"realm_path", CFG_VAR_STR, 0, 0, 0, 0, "realm json path"},
	{"process_no_subscribers", CFG_VAR_INT, 0, 0, 0, 0, "process call_info with no subscribers"},
	{"log_level", CFG_VAR_INT, 0, 0, 0, 0, "log level for sca debugging"},
	{"use_next_available", CFG_VAR_INT, 0, 0, 0, 0, "tweak to use next available appearance instead of index"},
	{0, 0, 0, 0, 0, 0}
};

MODULE_VERSION

static tr_export_t mod_trans[] = {
	{ {"kz", sizeof("kz")-1}, kz_tr_parse},
	{ {"jwt", sizeof("jwt")-1}, kz_jwt_tr_parse},
	{ { 0, 0 }, 0 }
};

static pv_export_t kz_mod_pvs[] = {
	{{"kzR", (sizeof("kzR")-1)}, PVT_OTHER, kz_pv_get_last_query_result, 0,	0, 0, 0, 0},
	{{"kzE", (sizeof("kzE")-1)}, PVT_OTHER, kz_pv_get_event_payload, 0,	0, 0, 0, 0},
	{{"kzRK", (sizeof("kzRK")-1)}, PVT_OTHER, kz_pv_get_event_routing_key, 0, 0, 0, 0, 0},
	{{"kzTS", (sizeof("kzTS")-1)}, PVT_OTHER, kz_pv_get_rfc3339, 0, 0, 0, 0, 0},
	{{"knode", (sizeof("knode")-1)}, PVT_OTHER, kz_pv_get_node, 0, 0, 0, 0, 0},
	{{"ki", (sizeof("ki")-1)}, PVT_OTHER, kz_pv_get_call_id, kz_pv_set_ki, 0, 0, 0, 0},
	{{"kgts", (sizeof("kgts")-1)}, PVT_OTHER, kz_pv_get_gregorian_timestamp, 0, 0, 0, 0, 0},
	{{"kcii", (sizeof("kcii")-1)}, PVT_OTHER, kz_pv_get_interaction_id, 0, 0, 0, 0, 0},
	{{"kjwt_error", (sizeof("kjwt_error")-1)}, PVT_OTHER, kz_pv_get_jwt_error, 0, 0, 0, 0, 0},
	{{"kz_call_info", (sizeof("kz_call_info")-1)}, PVT_OTHER, kz_sca_pv_get_call_info, 0, kz_sca_pv_parse_call_info_name, 0, 0, 0},
	{{"kz_subs", (sizeof("kz_subs")-1)}, PVT_OTHER, kz_sca_pv_get_subs, 0, kz_sca_pv_parse_subs_name, 0, 0, 0},

	{ {0, 0}, 0, 0, 0, 0, 0, 0, 0 }
};

/*
 *  module interface
 */
static cmd_export_t cmds[] = {
	{"kazoo_publish", (cmd_function) kz_amqp_publish3, 3, fixup_kz_amqp, fixup_kz_amqp_free, ANY_ROUTE},
	{"kazoo_publish", (cmd_function) kz_amqp_publish4, 4, fixup_kz_amqp, fixup_kz_amqp_free, ANY_ROUTE},
	{"kazoo_publish", (cmd_function) kz_amqp_publish5, 5, fixup_kz_amqp, fixup_kz_amqp_free, ANY_ROUTE},
	{"kz_amqp_publish", (cmd_function) kz_amqp_publish3, 3, fixup_kz_amqp, fixup_kz_amqp_free, ANY_ROUTE},
	{"kz_amqp_publish", (cmd_function) kz_amqp_publish4, 4, fixup_kz_amqp, fixup_kz_amqp_free, ANY_ROUTE},
	{"kz_amqp_publish", (cmd_function) kz_amqp_publish5, 5, fixup_kz_amqp, fixup_kz_amqp_free, ANY_ROUTE},

	{"kazoo_query", (cmd_function) kz_amqp_query3, 3, fixup_kz_amqp, fixup_kz_amqp_free, ANY_ROUTE},
	{"kazoo_query", (cmd_function) kz_amqp_query4, 4, fixup_kz_amqp, fixup_kz_amqp_free, ANY_ROUTE},
	{"kazoo_query", (cmd_function) kz_amqp_query5, 5, fixup_kz_amqp, fixup_kz_amqp_free, ANY_ROUTE},
	{"kz_amqp_query", (cmd_function) kz_amqp_query3, 3, fixup_kz_amqp, fixup_kz_amqp_free, ANY_ROUTE},
	{"kz_amqp_query", (cmd_function) kz_amqp_query4, 4, fixup_kz_amqp, fixup_kz_amqp_free, ANY_ROUTE},
	{"kz_amqp_query", (cmd_function) kz_amqp_query5, 5, fixup_kz_amqp, fixup_kz_amqp_free, ANY_ROUTE},

	{"kazoo_query_async", (cmd_function) kz_amqp_query_async5, 5, fixup_kz_async_amqp, fixup_kz_async_amqp_free, ANY_ROUTE},
	{"kazoo_query_async", (cmd_function) kz_amqp_query_async6, 6, fixup_kz_async_amqp, fixup_kz_async_amqp_free, ANY_ROUTE},
	{"kazoo_query_async", (cmd_function) kz_amqp_query_async7, 7, fixup_kz_async_amqp, fixup_kz_async_amqp_free, ANY_ROUTE},
	{"kz_amqp_query_async", (cmd_function) kz_amqp_query_async5, 5, fixup_kz_async_amqp, fixup_kz_async_amqp_free, ANY_ROUTE},
	{"kz_amqp_query_async", (cmd_function) kz_amqp_query_async6, 6, fixup_kz_async_amqp, fixup_kz_async_amqp_free, ANY_ROUTE},
	{"kz_amqp_query_async", (cmd_function) kz_amqp_query_async7, 7, fixup_kz_async_amqp, fixup_kz_async_amqp_free, ANY_ROUTE},

	{"kazoo_async_query", (cmd_function) kz_amqp_query_async5, 5, fixup_kz_async_amqp, fixup_kz_async_amqp_free, ANY_ROUTE},
	{"kazoo_async_query", (cmd_function) kz_amqp_query_async6, 6, fixup_kz_async_amqp, fixup_kz_async_amqp_free, ANY_ROUTE},
	{"kazoo_async_query", (cmd_function) kz_amqp_query_async7, 7, fixup_kz_async_amqp, fixup_kz_async_amqp_free, ANY_ROUTE},
	{"kz_amqp_async_query", (cmd_function) kz_amqp_query_async5, 5, fixup_kz_async_amqp, fixup_kz_async_amqp_free, ANY_ROUTE},
	{"kz_amqp_async_query", (cmd_function) kz_amqp_query_async6, 6, fixup_kz_async_amqp, fixup_kz_async_amqp_free, ANY_ROUTE},
	{"kz_amqp_async_query", (cmd_function) kz_amqp_query_async7, 7, fixup_kz_async_amqp, fixup_kz_async_amqp_free, ANY_ROUTE},

	{"kazoo_pua_publish", (cmd_function) kz_pua_publish, 1, 0, 0, ANY_ROUTE},
	{"kazoo_pua_publish_mwi", (cmd_function) kz_pua_publish_mwi, 1, 0, 0, ANY_ROUTE},
	{"kazoo_pua_publish_presence", (cmd_function) kz_pua_publish_presence, 1, 0, 0, ANY_ROUTE},
	{"kazoo_pua_publish_dialoginfo", (cmd_function) kz_pua_publish_dialoginfo, 1, 0, 0, ANY_ROUTE},
	{"kz_pua_publish", (cmd_function) kz_pua_publish, 1, 0, 0, ANY_ROUTE},
	{"kz_pua_publish_mwi", (cmd_function) kz_pua_publish_mwi, 1, 0, 0, ANY_ROUTE},
	{"kz_pua_publish_presence", (cmd_function) kz_pua_publish_presence, 1, 0, 0, ANY_ROUTE},
	{"kz_pua_publish_dialoginfo", (cmd_function) kz_pua_publish_dialoginfo, 1, 0, 0, ANY_ROUTE},

	{"kazoo_subscribe", (cmd_function) kz_amqp_subscribe_k, 1, fixup_kz_amqp4, fixup_kz_amqp4_free, ANY_ROUTE},
	{"kazoo_subscribe", (cmd_function) kz_amqp_subscribe_simple_k, 4, fixup_kz_amqp4, fixup_kz_amqp4_free, ANY_ROUTE},
	{"kz_amqp_subscribe", (cmd_function) kz_amqp_subscribe_k, 1, fixup_kz_amqp4, fixup_kz_amqp4_free, ANY_ROUTE},
	{"kz_amqp_subscribe", (cmd_function) kz_amqp_subscribe_simple_k, 4, fixup_kz_amqp4, fixup_kz_amqp4_free, ANY_ROUTE},

	{"kazoo_declare_exchange", (cmd_function) kz_amqp_declare_exchange_k,  1, fixup_kz_amqp4, fixup_kz_amqp4_free, ANY_ROUTE},
	{"kz_amqp_declare_exchange", (cmd_function) kz_amqp_declare_exchange_k,  1, fixup_kz_amqp4, fixup_kz_amqp4_free, ANY_ROUTE},

	{"kazoo_json", (cmd_function) kz_json_get_field, 3, fixup_kz_json, fixup_kz_json_free, ANY_ROUTE},
	{"kazoo_json_keys", (cmd_function) kz_json_get_keys, 3, fixup_kz_json, fixup_kz_json_free, ANY_ROUTE},
	{"kazoo_encode", (cmd_function) kz_amqp_encode, 2, fixup_kz_amqp_encode, fixup_kz_amqp_encode_free, ANY_ROUTE},
	{"kz_json", (cmd_function) kz_json_get_field, 3, fixup_kz_json, fixup_kz_json_free, ANY_ROUTE},
	{"kz_json_keys", (cmd_function) kz_json_get_keys, 3, fixup_kz_json, fixup_kz_json_free, ANY_ROUTE},
	{"kz_amqp_encode", (cmd_function) kz_amqp_encode, 2, fixup_kz_amqp_encode, fixup_kz_amqp_encode_free, ANY_ROUTE},

	{"kz_is_jwt", (cmd_function) kz_is_jwt, 1, fixup_kz_amqp, fixup_kz_amqp_free, ANY_ROUTE},
	{"kz_is_valid_jwt", (cmd_function) kz_jwt_validate, 1, fixup_kz_amqp, fixup_kz_amqp_free, ANY_ROUTE},
	{"kz_jwt_token", (cmd_function) kz_jwt_token, 1, fixup_kz_amqp, fixup_kz_amqp_free, ANY_ROUTE},
	{"kz_jwt_header", (cmd_function) kz_jwt_header, 1, fixup_kz_amqp, fixup_kz_amqp_free, ANY_ROUTE},
	{"kz_jwt_payload", (cmd_function) kz_jwt_payload, 1, fixup_kz_amqp, fixup_kz_amqp_free, ANY_ROUTE},

	{"kz_handle_subscribe", (cmd_function)kz_sca_handle_subscribe_0_f, 0, NULL, NULL, REQUEST_ROUTE},
	{"kz_pua_extended_call_info_update", (cmd_function)kz_sca_extended_call_info_update, 0, NULL, 0, REQUEST_ROUTE | FAILURE_ROUTE | ONREPLY_ROUTE},

	{0, 0, 0, 0, 0, 0}
};

static param_export_t params[] = {
	{"node_hostname", PARAM_STR, &dbk_node_hostname},
	{"node_name", PARAM_STR, &dbk_node_name},
	{"amqp_connection", PARAM_STRING|USE_FUNC_PARAM,(void*)kz_amqp_add_connection_k},
	{"amqp_max_channels", INT_PARAM, &dbk_channels},

	{"amqp_consumer_processes", INT_PARAM, &dbk_consumer_processes},
	{"amqp_consumer_workers", INT_PARAM, &dbk_consumer_workers},
	{"amqp_consumer_event_key", PARAM_STR, &dbk_consumer_event_key},
	{"amqp_consumer_event_subkey", PARAM_STR, &dbk_consumer_event_subkey},

	{"amqp_query_timeout_avp", PARAM_STR, &kz_query_timeout_avp},
	{"amqp_query_timeout_micro", INT_PARAM, &kz_qtimeout_tv.tv_usec},
	{"amqp_query_timeout_sec", INT_PARAM, &kz_qtimeout_tv.tv_sec},

	{"pua_mode", INT_PARAM, &dbk_pua_mode},
	{"db_url", PARAM_STR, &kz_db_url},
	{"pua_lock_type", INT_PARAM, &kz_pua_lock_type},
	{"presentity_table", PARAM_STR, &kz_presentity_table},
	{"pua_include_entity", INT_PARAM, &default_kz_presence_cfg.include_entity},
	{"presence_use_full_entity", INT_PARAM, &default_kz_presence_cfg.use_full_entity},

	{"json_escape_char", PARAM_STR, &kz_json_escape_str},

	{"app_name", PARAM_STR, &kz_app_name},

	{"amqp_connect_timeout", INT_PARAM, &dbk_connect_timeout},
	{"amqp_heartbeats", INT_PARAM, &dbk_use_heartbeats},
	{"amqp_primary_zone", PARAM_STR, &dbk_primary_zone_name},

	{"amqps_ca_cert", PARAM_STR, &kz_amqps_ca_cert},
	{"amqps_cert", PARAM_STR, &kz_amqps_cert},
	{"amqps_key", PARAM_STR, &kz_amqps_key},
	{"amqps_verify_peer", INT_PARAM, &kz_amqps_verify_peer},
	{"amqps_verify_hostname", INT_PARAM, &kz_amqps_verify_hostname},

	{"amqp_worker_queue_name_template", PARAM_STR, &dbk_default_worker_name_template},
	{"xavp_status", PARAM_STR, &kz_xavp_status},
	{"enable_clock_diff_warning", INT_PARAM, &kz_enable_clock_diff_warning},
	{"pid_routing_key_header", PARAM_STR, &kz_deliver_to_pid},

	{"amqp_log_level", INT_PARAM, &default_kz_amqp_cfg.log_level},

	{"relay", INT_PARAM, &default_kz_relay_cfg.active},

	{"pua_max_dialoginfo_xml_body_size", INT_PARAM, &default_kz_presence_cfg.max_dialoginfo_xml_body_size},
	{"pua_max_presence_xml_body_size", INT_PARAM, &default_kz_presence_cfg.max_presence_xml_body_size},
	{"pua_max_mwi_xml_body_size", INT_PARAM, &default_kz_presence_cfg.max_mwi_xml_body_size},

	{"sca.db_update_interval", INT_PARAM, &default_kz_sca_cfg.db_update_interval},
	{"sca.hash_table_size", INT_PARAM, &default_kz_sca_cfg.hash_table_size},
	{"sca.call_info_max_expires", INT_PARAM, &default_kz_sca_cfg.call_info_max_expires},
	{"sca.line_seize_max_expires", INT_PARAM, &default_kz_sca_cfg.line_seize_max_expires},
	{"sca.purge_expired_interval", INT_PARAM, &default_kz_sca_cfg.purge_expired_interval},
	{"sca.subs_table", PARAM_STR, &default_kz_sca_cfg.subs_table},
	{"sca.state_table", PARAM_STR, &default_kz_sca_cfg.state_table},
	{"sca.process_no_subscribers", INT_PARAM, &default_kz_sca_cfg.process_no_subscribers},
	{"sca.endpoint_spec", PARAM_STR, &default_kz_sca_cfg.endpoint_spec},
	{"sca.aliases_view", PARAM_STR, &default_kz_sca_cfg.aliases_view},
	{"sca.aliases_path", PARAM_STR, &default_kz_sca_cfg.aliases_path},
	{"sca.realm_path", PARAM_STR, &default_kz_sca_cfg.realm_path},
	{"sca.log_level", INT_PARAM, &default_kz_sca_cfg.log_level},
	{"sca.use_next_available", INT_PARAM, &default_kz_sca_cfg.use_next_available},

	{0, 0, 0}
};

stat_export_t mod_stats[] = {
	{"event-delivered",      STAT_NO_RESET, &kz_stat_event_delivered},
	{"event-received",       STAT_NO_RESET, &kz_stat_event_received},
	{"messages-published",      STAT_NO_RESET, &kz_stat_messages_published},
	{"messages-received",       STAT_NO_RESET, &kz_stat_messages_received},
	{"messages-received-self",  STAT_NO_RESET, &kz_stat_messages_received_self},
	{"messages-returned",       STAT_NO_RESET, &kz_stat_messages_returned},
	{"transactions-published",       STAT_NO_RESET, &kz_stat_transactions_published},
	{"transactions-received",       STAT_NO_RESET, &kz_stat_transactions_received},
	{"transactions-delivered",       STAT_NO_RESET, &kz_stat_transactions_delivered},
	{"transactions-expired",       STAT_NO_RESET, &kz_stat_transactions_expired},
	{"transactions-returned",       STAT_NO_RESET, &kz_stat_transactions_returned},
	{"calls-published",       STAT_NO_RESET, &kz_stat_calls_published},
	{"calls-received",       STAT_NO_RESET, &kz_stat_calls_received},
	{"calls-delivered",       STAT_NO_RESET, &kz_stat_calls_delivered},
	{"calls-returned",       STAT_NO_RESET, &kz_stat_calls_returned},
	{"relay-published",       STAT_NO_RESET, &kz_stat_relay_published},
	{"relay-received",       STAT_NO_RESET, &kz_stat_relay_received},
	{"relay-delivered",       STAT_NO_RESET, &kz_stat_relay_delivered},
	{"relay-returned",       STAT_NO_RESET, &kz_stat_relay_returned},
	{0, 0, 0}
};

struct module_exports exports = {
	"kazoo",         /* module name */
	DEFAULT_DLFLAGS, /* dlopen flags */
	cmds,            /* cmd (cfg function) exports */
	params,          /* param exports */
	0,               /* RPC method exports */
	kz_mod_pvs,      /* pseudo-variables exports */
	0,               /* response handling function */
	mod_init,        /* module init function */
	mod_child_init,  /* per-child init function */
	mod_destroy      /* module destroy function */
};

int kz_htable_bind()
{
	return htable_load_api(&kz_ht_api);
}

int kz_tm_bind()
{
	load_tm_f  load_tm;
	load_xtm_f  load_xtm;

	if ( !(load_tm=(load_tm_f)find_export("load_tm", NO_SCRIPT, 0)))
	{
		LOG(L_ERR, "cannot import load_tm\n");
		return 0;
	}
	if (load_tm( &kz_tmb )==-1)
		return 0;

	if ( !(load_xtm=(load_xtm_f)find_export("load_xtm", NO_SCRIPT, 0)))
	{
		LOG(L_ERR, "cannot import load_xtm\n");
		return 0;
	}
	if (load_xtm( &kz_xtm )==-1)
		return 0;

	return 1;
}


int kz_sl_bind()
{

	if(sl_load_api(&kz_sl_api) != 0) {
		LM_ERR("Failed to initialize required sl API. Check if the \"sl\" module is loaded.\n");
		return 0;
	}

	return 1;
}

int kz_rr_bind()
{
	if(load_rr_api(&kz_rrb) != 0) {
		LM_ERR("Failed to initialize required rr API. Check if the \"rr\" module is loaded.\n");
		return 0;
	}

	return 1;
}

int kz_external_binds_init() {

	if(!kz_tm_bind())
		return 0;

	if(!kz_sl_bind())
		return 0;

	if(!kz_rr_bind())
		return 0;

	return 1;
}

inline static int kz_parse_avp( str *avp_spec, pv_spec_t *avp, char *txt)
{
	if (pv_parse_spec(avp_spec, avp)==NULL) {
		LM_ERR("malformed or non AVP %s AVP definition\n",txt);
		return -1;
	}
	return 0;
}

static int kz_init_avp(void) {
	if(kz_query_timeout_avp.s)
		kz_query_timeout_avp.len = strlen(kz_query_timeout_avp.s);

	if ( kz_query_timeout_avp.s ) {
		if ( kz_parse_avp(&kz_query_timeout_avp, &kz_query_timeout_spec, "amqp_query_timeout_avp") <0) {
			return -1;
		}
	} else {
		memset( &kz_query_timeout_spec, 0, sizeof(pv_spec_t));
	}

	return 0;
}

int kz_script_pre_cb( struct sip_msg *msg, unsigned int flags, void *param )
{
	sr_xavp_t *list=NULL;
	sr_xavp_t *xavp=NULL;

	if(kz_xavp_status.s==NULL || kz_xavp_status.len<=0) {
		return 1;
	}

	lock_get(kz_status_lock);

	list = xavp_get(&kz_xavp_status, *kz_xavp_status_crt);
	if(list==NULL) {
		xavp_print_list(kz_xavp_status_crt);
		lock_release(kz_status_lock);
		return 1;
	}

	xavp = xavp_clone_level_nodata(list);
	if(xavp) {
		if(xavp_add(xavp, NULL)<0) {
			LM_ERR("error adding xavp in script init\n");
			xavp_destroy_list(&xavp);
		}
	}

	lock_release(kz_status_lock);

    return 1;
}

int kz_script_post_cb( struct sip_msg *msg, unsigned int flags, void *param )
{
    return 1;
}

int kz_init_script_cb()
{
	if (register_script_cb(kz_script_pre_cb, PRE_SCRIPT_CB|REQUEST_CB|EVENT_CB,0)<0) {
		LM_ERR("cannot register script callback\n");
		return -1;
	}

	if (register_script_cb(kz_script_post_cb, POST_SCRIPT_CB|REQUEST_CB|EVENT_CB,0)<0) {
		LM_ERR("cannot register post script callback\n");
		return -1;
	}

	return 0;
}

int kz_init_xavp_status()
{
	sr_xavp_t *new_xavp=NULL;
	sr_xval_t xval;

	kz_status_lock = lock_alloc();
	if(!kz_status_lock)
	{
		LM_CRIT("could not alloc a lock\n");
		return -1;
	}
	if (lock_init(kz_status_lock)==0)
	{
		LM_CRIT("could not initialize a lock\n");
		lock_dealloc(kz_status_lock);
		return -1;
	}

	if(kz_xavp_status.s==NULL || kz_xavp_status.len<=0) {
		return 0;
	}

	xval.type = SR_XTYPE_XAVP;
	xval.v.xavp = new_xavp;
	xavp_add_value(&kz_xavp_status, &xval, kz_xavp_status_crt);

	return 0;

}

int kz_init_jwt_keys()
{
	sr_xavp_t *new_xavp=NULL;
	sr_xval_t xval;

	kz_keys_lock = lock_alloc();
	if(!kz_keys_lock)
	{
		LM_CRIT("could not alloc a lock\n");
		return -1;
	}
	if (lock_init(kz_keys_lock)==0)
	{
		LM_CRIT("could not initialize a lock\n");
		lock_dealloc(kz_keys_lock);
		return -1;
	}

	if(kz_xavp_keys.s==NULL || kz_xavp_keys.len<=0) {
		return 0;
	}

	xval.type = SR_XTYPE_XAVP;
	xval.v.xavp = new_xavp;
	xavp_add_value(&kz_xavp_keys, &xval, kz_xavp_keys_crt);

	return kz_htable_bind();

}

int kz_init_xavp()
{
	if(kz_init_xavp_status() != 0) {
		return -1;
	}

	if(kz_init_jwt_keys() != 0) {
		return -1;
	}

	return 0;
}

void kz_init_relay() {
	struct sip_msg *fmsg;
	if( faked_msg_init() < 0) return;
	fmsg = faked_msg_next();
	str relay_str = str_init("$def(KZ_WS_AMQP_RELAY)");
	pv_spec_t relay_spec;
	if (pv_parse_spec(&relay_str, &relay_spec) != NULL) {
		pv_value_t pv_val;
		if (pv_get_spec_value(fmsg, &relay_spec, &pv_val) == 0) {
			if((pv_val.flags & PV_VAL_INT)) {
				default_kz_relay_cfg.active = pv_val.ri;
			} else if ((pv_val.flags & PV_VAL_STR)) {
				if (pv_val.rs.len > 0 && pv_val.rs.s) {
					default_kz_relay_cfg.active = atoi(pv_val.rs.s) == 1 ? 1 : 0;
				} else {
					default_kz_relay_cfg.active = 1;
				}
			}
		}
	}
}

static int kz_nosip_rcv_msg(sr_event_param_t *evp)
{
	char c;
	sip_msg_t* msg = (sip_msg_t*)evp->data;

	if(!cfg_get(kz_relay, kz_relay_cfg, active)) {
		LM_DBG("NOT active => %.*s\n", msg->len, msg->buf);
		return 1;
	}

	if (msg->rcv.proto != PROTO_WSS && msg->rcv.proto != PROTO_WS) {
		LM_DBG("NOT WEBSOCKET => %.*s\n", msg->len, msg->buf);
		return 1;
	}

	LM_DBG("NOSIP => %.*s\n", msg->len, msg->buf);
	c = msg->buf[msg->len];
	msg->buf[msg->len] = '\0';
	kz_amqp_relay(msg);
	msg->buf[msg->len] = c;

	return 0;
}

static int kz_on_ws_close(sr_event_param_t *evp)
{
	if(!cfg_get(kz_relay, kz_relay_cfg, active)) {
		return 0;
	}

	kz_amqp_stop_relay(evp);

	return 0;
}

static int mod_init(void) {
    startup_time = (int) time(NULL);
    kz_json_escape_char = kz_json_escape_str.s[0];
    int i;

    if (dbk_node_hostname.s == NULL) {
	LM_ERR("You must set the node_hostname parameter\n");
	return -1;
    }

   	if(kz_init_avp()) {
   		LM_ERR("Error in avp params\n");
   		return -1;
   	}

    if(!kz_external_binds_init()) {
   		return -1;
    }

    if(kz_init_xavp() < 0) {
    	return -1;
    }

    if(kz_init_script_cb() < 0) {
    	return -1;
    }

    if(dbk_pua_mode == 1) {
		kz_db_url.len = kz_db_url.s ? strlen(kz_db_url.s) : 0;
		LM_DBG("db_url=%s/%d/%p\n", ZSW(kz_db_url.s), kz_db_url.len,kz_db_url.s);
		kz_presentity_table.len = strlen(kz_presentity_table.s);

		if(kz_db_url.len > 0) {

			/* binding to database module  */
			if (db_bind_mod(&kz_db_url, &kz_pa_dbf))
			{
				LM_ERR("Database module not found\n");
				return -1;
			}


			if (!DB_CAPABILITY(kz_pa_dbf, DB_CAP_ALL))
			{
				LM_ERR("Database module does not implement all functions"
						" needed by kazoo module\n");
				return -1;
			}

			kz_pa_db = kz_pa_dbf.init(&kz_db_url);
			if (!kz_pa_db)
			{
				LM_ERR("Connection to database failed\n");
				return -1;
			}

			kz_pa_dbf.close(kz_pa_db);
			kz_pa_db = NULL;
		}
    }

	#ifdef STATISTICS
	/* register statistics */
	if (register_module_stats( exports.name, mod_stats)!=0 ) {
		LM_ERR("Failed to register kazoo statistics\n");
		return -1;
	}
	#endif

	kz_init_relay();

	if (default_kz_relay_cfg.active == 1) {
		LM_NOTICE("relay is active\n");
	}

	if (kz_sca_mod_init() != 0) {
		LM_ERR("failed to initialize sca.\n");
		return -1;
	}

	if(cfg_declare("kz.amqp", kz_amqp_cfg_def, &default_kz_amqp_cfg, cfg_sizeof(kz_amqp), &kz_amqp_cfg)) {
		LM_ERR("Failed to declare amqp configuration parameters.\n");
		return -1;
	}

	if(cfg_declare("kz.presence", kz_presence_cfg_def, &default_kz_presence_cfg, cfg_sizeof(kz_presence), &kz_presence_cfg)) {
		LM_ERR("Failed to declare presence configuration parameters.\n");
		return -1;
	}

	if(cfg_declare("kz.relay", kz_relay_cfg_def, &default_kz_relay_cfg, cfg_sizeof(kz_relay), &kz_relay_cfg)) {
		LM_ERR("Failed to declare relay configuration parameters.\n");
		return -1;
	}

	if(cfg_declare("kz.sca", kz_sca_cfg_def, &default_kz_sca_cfg, cfg_sizeof(kz_sca), &kz_sca_cfg)) {
		LM_ERR("Failed to declare sca configuration parameters.\n");
		return -1;
	}

    int total_workers = dbk_consumer_workers + 1;

    register_procs(total_workers);
    cfg_register_child(total_workers);

	if (pipe(kz_cmd_pipe_fds) < 0) {
		LM_ERR("cmd pipe() failed\n");
		return -1;
	}

    kz_worker_pipes_fds = (int*) shm_malloc(sizeof(int) * (dbk_consumer_workers) * 2 );
    for(i=0; i < dbk_consumer_workers; i++) {
    	kz_worker_pipes_fds[i*2] = kz_worker_pipes_fds[i*2+1] = -1;
		if (pipe(&kz_worker_pipes_fds[i*2]) < 0) {
			LM_ERR("worker pipe(%d) failed\n", i);
			return -1;
		}
    }

	kz_cmd_pipe = kz_cmd_pipe_fds[1];

	if(cfg_get(kz_relay, kz_relay_cfg, active)) {
		/* register non-sip hook */
		sr_event_register_cb(SREV_RCV_NOSIP, kz_nosip_rcv_msg);
		/* register ws_close hooks */
		sr_event_register_cb(SREV_TCP_WS_CLOSE, kz_on_ws_close);
		cfg_get(core, core_cfg, sip_parser_log) = 4;
	}

    return 0;
}

int mod_register(char *path, int *dlflags, void *p1, void *p2)
{
	struct hostent* h;
	gethostname(hostname, HOST_NAME_MAX+1);
	h = gethostbyname(hostname);
	memcpy(hostname, h->h_name, HOST_NAME_MAX+1);
	dbk_node_hostname.s = hostname;
	dbk_node_hostname.len = strlen(hostname);

	if(kz_tr_init_buffers()<0)
	{
		LM_ERR("failed to initialize transformations buffers\n");
		return -1;
	}
	return register_trans_mod(path, mod_trans);
}

static int mod_child_init(int rank)
{
	int pid;

	if (rank==PROC_INIT) {
		fire_init_event(rank);
	}

	if (rank==PROC_INIT || rank==PROC_TCP_MAIN)
		return 0;

	if (rank==PROC_MAIN) {

		kz_sca_child_init(rank);

		if(kz_amqp_start_worker_procs() < 0) return -1;

		pid=fork_process(AMQP_WORKERS_RANKING, "AMQP Manager", 1);
		if (pid<0)
			return -1; /* error */
		if(pid==0){
			if (cfg_child_init()) return -1;
			kz_amqp_manager_proc(kz_cmd_pipe_fds);
		}
		return 0;

	}


	if(rank == AMQP_WORKERS_RANKING && dbk_pua_mode == 1) {
		if (kz_pa_dbf.init==0)
		{
			LM_CRIT("child_init: database not bound\n");
			return -1;
		}
		kz_pa_db = kz_pa_dbf.init(&kz_db_url);
		if (!kz_pa_db)
		{
			LM_ERR("child %d: unsuccessful connecting to database\n", rank);
			return -1;
		}

		if (kz_pa_dbf.use_table(kz_pa_db, &kz_presentity_table) < 0)
		{
			LM_ERR( "child %d:unsuccessful use_table presentity_table\n", rank);
			return -1;
		}
		LM_DBG("child %d: Database connection opened successfully\n", rank);
	}

	return 0;
}

static int fire_init_event(int rank)
{
	struct sip_msg *fmsg;
	struct run_act_ctx ctx;
	int rtb, rt;

	LM_DBG("rank is (%d)\n", rank);
	if (rank!=PROC_INIT)
		return 0;

	rt = route_get(&event_rt, "kazoo:mod-init");
	if(rt>=0 && event_rt.rlist[rt]!=NULL) {
		LM_DBG("executing event_route[kazoo:mod-init] (%d)\n", rt);
		if(faked_msg_init()<0)
			return -1;
		fmsg = faked_msg_next();
		rtb = get_route_type();
		set_route_type(REQUEST_ROUTE);
		init_run_actions_ctx(&ctx);
		run_top_route(event_rt.rlist[rt], fmsg, &ctx);
		if(ctx.run_flags&DROP_R_F)
		{
			LM_ERR("exit due to 'drop' in event route\n");
			return -1;
		}
		set_route_type(rtb);
	}

	return 0;
}


static void mod_destroy(void) {

	kz_sca_mod_destroy();

	if (kz_worker_pipes_fds) {
		shm_free(kz_worker_pipes_fds);
	}

	lock_destroy(kz_status_lock);
	lock_dealloc(kz_status_lock);
	kz_status_lock = NULL;
	xavp_destroy_list(kz_xavp_status_crt);

	lock_destroy(kz_keys_lock);
	lock_dealloc(kz_keys_lock);
	kz_keys_lock = NULL;
	xavp_destroy_list(kz_xavp_keys_crt);

}

sr_xavp_t ** kz_get_status_crt()
{
	return kz_xavp_status_crt;
}

sr_xavp_t ** kz_get_keys_crt()
{
	return kz_xavp_keys_crt;
}
