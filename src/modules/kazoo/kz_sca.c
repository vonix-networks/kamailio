/*
 * Copyright (C) 2012 Andrew Mortensen
 *
 * This file is part of the sca module for Kamailio, a free SIP server.
 *
 * The sca module is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * The sca module is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA. 02110-1301 USA
 */
/*!
 * \file
 * \brief KZ_SCA shared call appearance module
 * \ingroup sca
 * - Module: \ref sca
 * \author Andrew Mortensen
 */

/*!
 * \defgroup sca :: The Kamailio shared call appearance Module
 *
 * The sca module implements Shared Call Appearances. It handles SUBSCRIBE messages for call-info 
 * and line-seize events, and sends call-info NOTIFYs to line subscribers to implement line bridging.
 * The module implements KZ_SCA as defined in Broadworks SIP Access Side Extensions Interface 
 * Specifications, Release 13.0, version 1, sections 2, 3 and 4.
 */
#include "kz_sca_common.h"

#include <sys/types.h>
#include <stdlib.h>

#include "../../core/timer.h"
#include "../../core/timer_proc.h"
#include "../../core/mod_fix.h"
#include "../../core/kemi.h"

#include "kz_sca.h"
#include "kz_sca_appearance.h"
#include "kz_sca_db.h"
#include "kz_sca_call_info.h"
#include "kz_sca_rpc.h"
#include "kz_sca_subscribe.h"

#include "kz_sca_params.h"


/*
 *  MODULE OBJECT
 */
kz_sca_mod *sca = NULL;

/*
 * EXTERNAL API
 */
extern db_func_t kz_pa_dbf;		 // db api
extern struct tm_binds kz_tmb; // tm functions for sending messages
extern struct rr_binds kz_rrb; // rr functions for detecting direction
extern sl_api_t kz_sl_api;		 // sl callback, function for getting to-tag

extern str kz_db_url;
extern int dbk_pua_mode;

/*
 * PROTOTYPES
 */
static int kz_sca_set_config(kz_sca_mod *);
int kz_sca_handle_subscribe_0_f(sip_msg_t *msg);


/*
 * EXPORTED RPC INTERFACE
 */
static rpc_export_t kz_sca_rpc[] = {
	{"kz.sca.all_subscriptions", kz_sca_rpc_show_all_subscriptions, kz_sca_rpc_show_all_subscriptions_doc, 0},
	{"kz.sca.subscription_count", kz_sca_rpc_subscription_count, kz_sca_rpc_subscription_count_doc, 0},
	{"kz.sca.show_subscription", kz_sca_rpc_show_subscription, kz_sca_rpc_show_subscription_doc, 0},
	{"kz.sca.subscribers", kz_sca_rpc_show_subscribers, kz_sca_rpc_show_subscribers_doc, 0},
	{"kz.sca.deactivate_all_subscriptions", kz_sca_rpc_deactivate_all_subscriptions, kz_sca_rpc_deactivate_all_subscriptions_doc, 0},
	{"kz.sca.deactivate_subscription", kz_sca_rpc_deactivate_subscription, kz_sca_rpc_deactivate_subscription_doc, 0},
	{"kz.sca.all_appearances", kz_sca_rpc_show_all_appearances, kz_sca_rpc_show_all_appearances_doc, 0},
	{"kz.sca.show_appearance", kz_sca_rpc_show_appearance, kz_sca_rpc_show_appearance_doc, 0},
	{"kz.sca.seize_appearance", kz_sca_rpc_seize_appearance, kz_sca_rpc_seize_appearance_doc, 0},
	{"kz.sca.update_appearance", kz_sca_rpc_update_appearance, kz_sca_rpc_update_appearance_doc, 0},
	{"kz.sca.release_appearance", kz_sca_rpc_release_appearance, kz_sca_rpc_release_appearance_doc, 0},
	{NULL, NULL, NULL, 0},
};

static int kz_sca_bind_srdb1(kz_sca_mod *scam, db_func_t *db_api)
{
	db1_con_t *db_con = NULL;
	int rc = -1;
	int version = 0;

	if (!scam->cfg->db_url) return 0;

	scam->db_api = db_api;

	// ensure database exists and table schemas are correct
	db_con = db_api->init(scam->cfg->db_url);
	if(db_con == NULL) {
		LM_ERR("kz_sca_bind_srdb1: failed to connect to DB %.*s\n", STR_FMT(scam->cfg->db_url));
		goto done;
	}

	version = db_table_version(db_api, db_con, scam->cfg->subs_table);
	if (version < 0) {
		SCA_DBG("failed to check table version for subs_table %.*s\n", STR_FMT(scam->cfg->subs_table));
		goto done;
	} else if (version == KZ_SCA_DB_SUBSCRIPTIONS_TABLE_VERSION) {
		SCA_DBG("version match for subs_table %.*s\n", STR_FMT(scam->cfg->subs_table));
		scam->db_persistance = 1;
	} else if (version > 0) {
		str tmp = *scam->cfg->subs_table;
		DB_TABLE_VERSION_ERROR(tmp);
		goto done;
	} else {
		SCA_DBG("no version for subs_table %.*s\n", STR_FMT(scam->cfg->subs_table));
	}

	// DB and tables are OK, close DB handle. reopen in each child.
	rc = 0;

done:
	if(db_con != NULL) {
		db_api->close(db_con);
		db_con = NULL;
	}

	return (rc);
}

static int kz_sca_set_config(kz_sca_mod *scam)
{
	scam->cfg = (kz_sca_config *)shm_malloc(sizeof(kz_sca_config));
	if(scam->cfg == NULL) {
		LM_ERR("Failed to shm_malloc module configuration\n");
		return (-1);
	}
	memset(scam->cfg, 0, sizeof(kz_sca_config));

	if(dbk_pua_mode == 1 && kz_db_url.s && kz_db_url.len > 0) {
		scam->cfg->db_url = &kz_db_url;

		if (!cfg_get(kz_sca, kz_sca_cfg, subs_table).s || cfg_get(kz_sca, kz_sca_cfg, subs_table).len <= 0) {
			LM_ERR("kz_sca_set_config: subs_table must be set!\n");
			return (-1);
		}
		scam->cfg->subs_table = &cfg_get(kz_sca, kz_sca_cfg, subs_table);

		if (!cfg_get(kz_sca, kz_sca_cfg, state_table).s || cfg_get(kz_sca, kz_sca_cfg, state_table).len <= 0) {
			LM_ERR("kz_sca_set_config: state_table must be set!\n");
			return (-1);
		}
		scam->cfg->state_table = &cfg_get(kz_sca, kz_sca_cfg, state_table);
	}

	if(cfg_get(kz_sca, kz_sca_cfg, hash_table_size) > 0) {
		scam->cfg->hash_table_size = 1 << cfg_get(kz_sca, kz_sca_cfg, hash_table_size);
	} else {
		scam->cfg->hash_table_size = 512;
	}

	scam->cfg->db_update_interval = cfg_get(kz_sca, kz_sca_cfg, db_update_interval);
	scam->cfg->call_info_max_expires = cfg_get(kz_sca, kz_sca_cfg, call_info_max_expires);
	scam->cfg->line_seize_max_expires = cfg_get(kz_sca, kz_sca_cfg, line_seize_max_expires);
	scam->cfg->purge_expired_interval = cfg_get(kz_sca, kz_sca_cfg, purge_expired_interval);

	if(cfg_get(kz_sca, kz_sca_cfg, outbound_proxy).s) {
		scam->cfg->outbound_proxy = &cfg_get(kz_sca, kz_sca_cfg, outbound_proxy);
	}

	if(cfg_get(kz_sca, kz_sca_cfg, server_address).s) {
		scam->cfg->server_address = &cfg_get(kz_sca, kz_sca_cfg, server_address);
	}

	return (0);
}

int kz_sca_child_init(int rank)
{
	if(rank == PROC_INIT || rank == PROC_TCP_MAIN) {
		return (0);
	}

	if(rank == PROC_MAIN) {
		if(fork_dummy_timer(PROC_TIMER, "KZ_SCA DB SYNC PROCESS",
				   0, // we don't need sockets, just writing to DB
				   kz_sca_subscription_db_update_timer, // timer cb
				   NULL, // parameter passed to callback
				   sca->cfg->db_update_interval) < 0) {
			LM_ERR("kz_sca_child_init: failed to register subscription DB sync timer process\n");
			return (-1);
		}

		return (0);
	}

	if(sca->db_api == NULL || sca->db_api->init == NULL) {
		LM_CRIT("kz_sca_child_init: DB API not loaded!\n");
		return (-1);
	}

	return (0);
}


int kz_sca_mod_init(void)
{
	sca = (kz_sca_mod *)shm_malloc(sizeof(kz_sca_mod));
	if(sca == NULL) {
		LM_ERR("Failed to shm_malloc module object\n");
		return (-1);
	}
	memset(sca, 0, sizeof(kz_sca_mod));

	if(kz_sca_set_config(sca) != 0) {
		LM_ERR("Failed to set configuration\n");
		goto error;
	}

	if(rpc_register_array(kz_sca_rpc) != 0) {
		LM_ERR("Failed to register RPC commands\n");
		goto error;
	}

	if(kz_sca_bind_srdb1(sca, &kz_pa_dbf) != 0) {
		LM_ERR("Failed to initialize required DB API\n");
		goto error;
	}

	sca->tm_api = &kz_tmb;
	sca->rr_api = &kz_rrb;
	sca->sl_api = &kz_sl_api;

	if(kz_sca_hash_table_create(&sca->subscriptions, sca->cfg->hash_table_size) != 0) {
		LM_ERR("Failed to create subscriptions hash table\n");
		goto error;
	}

	if(kz_sca_hash_table_create(&sca->appearances, sca->cfg->hash_table_size) != 0) {
		LM_ERR("Failed to create appearances hash table\n");
		goto error;
	}

	kz_sca_subscriptions_restore_from_db(sca);

	register_timer(kz_sca_subscription_purge_expired, sca, sca->cfg->purge_expired_interval);
	register_timer(kz_sca_appearance_purge_stale, sca, sca->cfg->purge_expired_interval);

	// register separate timer process to write subscriptions to DB.
	// move to 3.3+ timer API (register_basic_timer) at some point.
	// timer process forks in kz_sca_child_init, above.
	register_dummy_timers(1);

	if (sca->db_persistance) LM_INFO("KZ_SCA initialized \n");

	return (0);

error:
	if(sca != NULL) {
		if(sca->cfg != NULL) {
			shm_free(sca->cfg);
		}

		if(sca->subscriptions != NULL) {
			kz_sca_hash_table_free(sca->subscriptions);
		}

		if(sca->appearances != NULL) {
			kz_sca_hash_table_free(sca->appearances);
		}

		shm_free(sca);
		sca = NULL;
	}

	return (-1);
}

void kz_sca_mod_destroy(void)
{
	if(sca == 0)
		return;

	// write back to the DB to retain most current subscription info
	if(kz_sca_subscription_db_update() != 0) {
		if(sca && sca->cfg && sca->cfg->db_url) {
			LM_ERR("kz_sca_mod_destroy: failed to save current subscriptions in DB %.*s", STR_FMT(sca->cfg->db_url));
		}
	}

	kz_sca_db_disconnect();
}

int kz_sca_handle_subscribe_0_f(sip_msg_t *msg)
{
	return kz_sca_handle_subscribe(msg);
}
