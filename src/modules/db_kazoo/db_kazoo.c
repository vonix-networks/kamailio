/*
 * $Id$
 *
 * KazooDB module interface
 *
 * Copyright (C) 2010 Timo Teräs
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <sys/time.h>

#include "../../core/sr_module.h"
#include "../../lib/srdb1/db_query.h"
#include "../../lib/srdb1/db.h"
#include "../../core/counters.h"
#include "../../core/cfg/cfg.h"
#include "../../core/mem/tlsf_malloc.h"
#include "dbase.h"

MODULE_VERSION

#define MAX_RETRIES 100
#define MAX_ROWS 0

int dbk_enable_foreign_keys = 1;
int dbk_enable_triggers = 1;

int dbk_trace_hook_enable = 0;

stat_var *busy1_stat;
stat_var *busy2_stat;
stat_var *busy3_stat;
stat_var *busy4_stat;
stat_var *busy5_stat;

stat_var *cmd_stat;

struct cfg_group_db_kazoo	default_db_kazoo_cfg = {
		1,
		MAX_RETRIES,
		50,
		1,
		0,
		L_DBG,
		0,
		L_DBG,
		20,
		MAX_ROWS,
		DB_POOLING_NONE
	};

void	*db_kazoo_cfg = &default_db_kazoo_cfg;

cfg_def_t	db_kazoo_cfg_def[] = {
	{"retry_on_busy", CFG_VAR_INT, 0, 0, 0, 0, "retry on busy"},
	{"retry_on_busy_retries", CFG_VAR_INT, 0, 0, 0, 0, "number of retries on busy"},
	{"retry_on_busy_sleep", CFG_VAR_INT, 0, 0, 0, 0, "pause between retries"},
	{"sql_log_errors", CFG_VAR_INT, 0, 0, 0, 0, "log sql errors"},
	{"sql_log", CFG_VAR_INT, 0, 0, 0, 0, "log sql"},
	{"sql_log_level", CFG_VAR_INT, 0, 0, 0, 0, "log sql level"},
	{"trace_enable", CFG_VAR_INT, 0, 0, 0, 0, "trace enable"},
	{"trace_log_level", CFG_VAR_INT, 0, 0, 0, 0, "trace log level"},
	{"trace_filter", CFG_VAR_INT, 0, 0, 0, 0, "trace filter"},
	{"max_rows", CFG_VAR_INT, 0, 0, 0, 0, "force limit on select"},
	{"pooling", CFG_VAR_INT, 0, 0, 0, 0, "pooling flag"},
	{0, 0, 0, 0, 0, 0}
};



static int db_kazoo_mod_init(void);
static void db_kazoo_mod_destroy(void);
static void kz_db_log(void *pArg, int iErrCode, const char *zMsg);
static int db_kazoo_bind_api(db_func_t *dbb);

static param_export_t params[] = {
    {"retry_on_busy", PARAM_INT, &default_db_kazoo_cfg.retry_on_busy},
    {"retry_on_busy_retries", PARAM_INT, &default_db_kazoo_cfg.retry_on_busy_retries},
    {"retry_on_busy_sleep", PARAM_INT, &default_db_kazoo_cfg.retry_on_busy_sleep},

    {"log_sql_errors", PARAM_INT, &default_db_kazoo_cfg.log_sql_errors},
    {"sql_log", PARAM_INT, &default_db_kazoo_cfg.log_sql},
    {"sql_log_level", PARAM_INT, &default_db_kazoo_cfg.sql_log_level},

    {"trace_hook", PARAM_INT, &dbk_trace_hook_enable},
    {"trace_enable", PARAM_INT, &default_db_kazoo_cfg.trace_enable},
    {"trace_log_level", PARAM_INT, &default_db_kazoo_cfg.trace_log_level},
    {"trace_filter", PARAM_INT, &default_db_kazoo_cfg.trace_filter},

    {"max_rows", PARAM_INT, &default_db_kazoo_cfg.max_rows},

    {"pooling", PARAM_INT, &default_db_kazoo_cfg.pooling},

    {"enable_foreign_keys", PARAM_INT, &dbk_enable_foreign_keys},
    {"enable_triggers", PARAM_INT, &dbk_enable_triggers},
    {0, 0, 0}
};

stat_export_t mod_stats[] = {
	{"busy1",       STAT_NO_RESET, &busy1_stat},
	{"busy2",       STAT_NO_RESET, &busy2_stat},
	{"busy3",       STAT_NO_RESET, &busy3_stat},
	{"busy4",       STAT_NO_RESET, &busy4_stat},
	{"busy5",       STAT_NO_RESET, &busy5_stat},
	{"cmd",       STAT_NO_RESET, &cmd_stat},
	{0, 0, 0}
};


static void kz_db_log(void *pArg, int iErrCode, const char *zMsg)
{
	if(cfg_get(db_kazoo, db_kazoo_cfg, log_sql_errors)) {
		// ignore database schema changed, iErrCode 17
		if(iErrCode == 17) {
			LM_DBG("(%d) %s\n", iErrCode, zMsg);
		} else if(iErrCode == 283) {
			LM_NOTICE("%s\n", zMsg);
		} else if(iErrCode == 28) {
			LM_DBG("(%d) %s\n", iErrCode, zMsg);
		} else {
			LM_ERR("(%d) %s\n", iErrCode, zMsg);
		}
	}
}

static void kz_db_sql_log(void *pArg, sqlite3* conn, const char *zMsg, int op)
{
	if(cfg_get(db_kazoo, db_kazoo_cfg, log_sql) && op == 1) {
		LOG(cfg_get(db_kazoo, db_kazoo_cfg, sql_log_level), "SQL=> %s\n", zMsg);
	}
}

static int kz_sql_mem_size(void *p)
{
  return tlsf_block_size(p);
}

static int kz_sql_mem_roundup(int x)
{
	return x;
}

static int kz_sql_mem_init(void *user_data)
{
	return 0;
}

static void kz_sql_mem_shutdown(void *user_data)
{
}

static void* kz_sql_mem_alloc(int size)
{
	return pkg_malloc(size);
}

static void* kz_sql_mem_realloc(void *existing, int size)
{
	return pkg_realloc(existing, size);
}

static void kz_sql_mem_free(void *existing)
{
	pkg_free(existing);
}

static struct sqlite3_mem_methods kz_sql_mem_methods = {
	kz_sql_mem_alloc,                   /* xMalloc */
	kz_sql_mem_free,                     /* xFree */
	kz_sql_mem_realloc,                  /* xRealloc */
	kz_sql_mem_size,                     /* xSize */
	kz_sql_mem_roundup,                  /* xRoundup */
	kz_sql_mem_init,                     /* xInit */
	kz_sql_mem_shutdown,                 /* xShutdown */
    0                                 /* pAppData */
  };


static int db_kazoo_bind_api(db_func_t *dbb)
{
	if (dbb == NULL)
		return -1;

	memset(dbb, 0, sizeof(db_func_t));

	dbb->use_table = db_kazoo_use_table;
	dbb->init = db_kazoo_init;
	dbb->close = db_kazoo_close;
	dbb->query = db_kazoo_query;
	dbb->raw_query = db_kazoo_raw_query;
	dbb->fetch_result = db_kazoo_fetch_result;
	dbb->free_result = db_kazoo_free_result;
	dbb->insert = db_kazoo_insert;
	dbb->delete = db_kazoo_delete;
	dbb->update = db_kazoo_update;
	dbb->replace = db_kazoo_replace;
	dbb->last_inserted_id = db_kazoo_last_inserted_id;
	dbb->affected_rows    = db_kazoo_affected_rows;

	return 0;
}

static cmd_export_t cmds[] = {
		{ "db_bind_api", (cmd_function) db_kazoo_bind_api, 0, 0, 0, 0 },
        { 0, 0, 0, 0, 0, 0 }
};


/*
 * 5.1
 */
//struct module_exports exports = {
//		"db_kazoo",
//		DEFAULT_DLFLAGS, /* dlopen flags */
//		cmds, /* module commands */
//		params, /* module parameters */
//		mod_stats, /* exported statistics */
//		0, /* exported MI functions */
//		0, /* exported pseudo-variables */
//		0, /* extra processes */
//		db_kazoo_mod_init, /* module initialization function */
//		0, /* response function*/
//		db_kazoo_mod_destroy, /* destroy function */
//		0 /* per-child init function */
//};

struct module_exports exports = {
		"db_kazoo",
		DEFAULT_DLFLAGS, /* dlopen flags */
		cmds, /* module commands */
		params, /* module parameters */
		0,               /* RPC method exports */
		0, /* exported pseudo-variables */
		0, /* response function*/
		db_kazoo_mod_init, /* module initialization function */
		0, /* per-child init function */
		db_kazoo_mod_destroy /* destroy function */
};


int mod_register(char *path, int *dlflags, void *p1, void *p2)
{
	sqlite3_config(SQLITE_CONFIG_MEMSTATUS, 0, SQLITE_CONFIG_SINGLETHREAD);
	sqlite3_config(SQLITE_CONFIG_LOG, &kz_db_log, NULL);
	sqlite3_config(SQLITE_CONFIG_SQLLOG, &kz_db_sql_log, NULL);
	sqlite3_config(SQLITE_DBCONFIG_ENABLE_FKEY, dbk_enable_foreign_keys, NULL);
	sqlite3_config(SQLITE_DBCONFIG_ENABLE_TRIGGER, dbk_enable_triggers, NULL);
	sqlite3_config(SQLITE_DBCONFIG_ENABLE_TRIGGER, dbk_enable_triggers, NULL);
	sqlite3_config(SQLITE_DBCONFIG_DQS_DDL, 1, NULL);
	sqlite3_config(SQLITE_DBCONFIG_DQS_DML, 1, NULL);
	if(0) sqlite3_config(SQLITE_CONFIG_MALLOC, &kz_sql_mem_methods);

	sqlite3_initialize();

	db_kazoo_alloc_buffer();

	LM_INFO("KazooDB library version %s (compiled using %s)\n", sqlite3_libversion(), SQLITE_VERSION);

	return 0;
}

static int db_kazoo_mod_init(void)
{
	if(db_kazoo_lock_init()) return -1;

	#ifdef STATISTICS
	/* register statistics */
	if (register_module_stats( exports.name, mod_stats)!=0 ) {
		LM_ERR("Failed to register db_kazoo statistics\n");
		return -1;
	}
	#endif

	if(cfg_declare("db_kazoo", db_kazoo_cfg_def, &default_db_kazoo_cfg,
				cfg_sizeof(db_kazoo), &db_kazoo_cfg)){
		LM_ERR("Failed to declare the configuration parameters.\n");
		return -1;
	}

	return 0;
}

static void db_kazoo_mod_destroy(void)
{
	LM_DBG("KazooDB terminate\n");
	db_kazoo_release_buffer();
	db_kazoo_lock_destroy();
	sqlite3_shutdown();
}
