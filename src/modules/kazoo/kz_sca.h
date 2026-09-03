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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *
 */
#include "kz_sca_common.h"

#include "kz_sca_db.h"
#include "kz_sca_hash.h"
#include "kz_sca_params.h"

#ifndef KZ_SCA_H
#define KZ_SCA_H

struct _kz_sca_config
{
	str *outbound_proxy;
	str *db_url;
	str *subs_table;
	str *state_table;
	int db_update_interval;
	int hash_table_size;
	int call_info_max_expires;
	int line_seize_max_expires;
	int purge_expired_interval;
	int onhold_bflag;
	str *server_address;
};
typedef struct _kz_sca_config kz_sca_config;

struct _kz_sca_mod
{
	kz_sca_config *cfg;
	kz_sca_hash_table *subscriptions;
	kz_sca_hash_table *appearances;

	db_func_t *db_api;
	struct tm_binds *tm_api;
	struct rr_binds *rr_api;
	sl_api_t *sl_api;
	int db_persistance;
};
typedef struct _kz_sca_mod kz_sca_mod;

extern kz_sca_mod *sca;

#define SCA_DBG_LEVEL cfg_get(kz_sca, kz_sca_cfg, log_level)
#define SCA_DBG(fmt, args...) LOG(SCA_DBG_LEVEL, fmt, ##args)

#endif // KZ_SCA_H
