/*
 * $Id$
 *
 * KazooDB module core functions
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

#include "../../core/mem/mem.h"
#include "../../core/ut.h"
#include "../../core/dprint.h"
#include "../../lib/srdb1/db_pool.h"
#include "../../lib/srdb1/db_ut.h"
#include "../../lib/srdb1/db_res.h"
#include "../../lib/srdb1/db_query.h"
#include "../../core/locking.h"
#include "../../core/globals.h"
#include "../../core/timer.h"
#include "../../core/counters.h"

#include "dbase.h"
#include <stdint.h>
#include <inttypes.h>

extern unsigned int sql_buffer_size;

extern int dbk_trace_hook_enable;

extern stat_var *busy1_stat;
extern stat_var *busy2_stat;
extern stat_var *busy3_stat;
extern stat_var *busy4_stat;
extern stat_var *busy5_stat;
extern stat_var *cmd_stat;

static str   kazoo_sql_str;
static char *kazoo_sql_buf = NULL;

static time_t db_kazoo_to_timet(double rT)
{
	return 86400.0 * (rT - 2440587.5) + 0.5;
}

static double timet_to_sqlite(time_t t)
{
	return ((((double) t) - 0.5) / 86400.0) + 2440587.5;
}

#ifdef WIN32
#include <windows.h>
#elif _POSIX_C_SOURCE >= 199309L
#include <time.h>   // for nanosleep
#else
#include <unistd.h> // for usleep
#endif

void sleep_ms(int milliseconds) // cross-platform sleep function
{
#ifdef WIN32
    Sleep(milliseconds);
#elif _POSIX_C_SOURCE >= 199309L
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
#else
    usleep(milliseconds * 1000);
#endif
}

static gen_lock_t *_db_kazoo_cachesem = NULL;


static inline int db_kazoo_do_submit_query(const db1_con_t* _h, const str *_query,
 		int (*submit_query)(const db1_con_t*, const str*))
 {
 	int ret;
 	unsigned int ms = 0;

 	if(unlikely(cfg_get(core, core_cfg, latency_limit_action)>0))
 		ms = TICKS_TO_MS(get_ticks_raw());

 	ret = submit_query(_h, _query);

 	if(unlikely(cfg_get(core, core_cfg, latency_limit_action)>0)) {
 		ms = TICKS_TO_MS(get_ticks_raw()) - ms;
 		if(ms >= cfg_get(core, core_cfg, latency_limit_action)) {
 				LOG(cfg_get(core, core_cfg, latency_log),
 					"alert - query execution too long [%u ms] for [%.*s]\n",
 				   ms, _query->len<50?_query->len:50, _query->s);
 		}
 	}

 	return ret;
 }

int db_kazoo_lock_init(void)
{
	if(!_db_kazoo_cachesem)
	{
		_db_kazoo_cachesem = lock_alloc();
		if(!_db_kazoo_cachesem)
		{
			LM_CRIT("could not alloc a lock\n");
			return -1;
		}
		if (lock_init(_db_kazoo_cachesem)==0)
		{
			LM_CRIT("could not initialize a lock\n");
			lock_dealloc(_db_kazoo_cachesem);
			return -1;
		}
	}
	return 0;
}

int db_kazoo_lock_destroy(void)
{
	if(!_db_kazoo_cachesem)
		return -1;

	lock_release(_db_kazoo_cachesem);
	lock_get(_db_kazoo_cachesem);
	lock_release(_db_kazoo_cachesem);
	lock_destroy(_db_kazoo_cachesem);
	lock_dealloc(_db_kazoo_cachesem);
	_db_kazoo_cachesem = NULL;

	return 0;
}

int db_kazoo_acquire_lock(struct db_kazoo_connection *conn)
{
	if(!_db_kazoo_cachesem) return 1;

	if(conn->locked == 1) {
		LM_DBG("lock already in place\n");
		return 0;
	}

	LM_DBG("locking\n");

	lock_get(_db_kazoo_cachesem);
	LM_DBG("lock acquired\n");
	conn->locked = 1;

	return 1;
}

void db_kazoo_release_lock(struct db_kazoo_connection *conn)
{
	if(!_db_kazoo_cachesem) return;

	if(conn->locked == 1) {
		LM_DBG("unlocking\n");
		conn->locked = 0;
		lock_release(_db_kazoo_cachesem);
		LM_DBG("unlocked\n");
	} else {
		LM_DBG("unlocking not needed\n");
	}
}

static int db_kazoo_trace_handler(unsigned reason, void *pCtx, void * p, void* param)
{
	int64_t nano;
	int ms;
	switch(reason) {
	case SQLITE_TRACE_STMT:
		LM_INFO("SQL=>%s\n", sqlite3_expanded_sql((sqlite3_stmt *)p));
		break;
	case SQLITE_TRACE_PROFILE:
		if(cfg_get(db_kazoo, db_kazoo_cfg, trace_enable)) {
			int filter = cfg_get(db_kazoo, db_kazoo_cfg, trace_filter);
			nano = *((int64_t *)param);
			ms = nano / 1000000;
			if(filter == 0 || ms > filter) {
				LOG(cfg_get(db_kazoo, db_kazoo_cfg, trace_log_level), "PROFILE=> (%i) : %s\n", ms, sqlite3_expanded_sql((sqlite3_stmt *)p));
			}
		}
		break;
	default:
		break;
	}

	return 0;
}

static int db_kazoo_busy_handler(void *data, int retry)
{
	int group = retry / (cfg_get(db_kazoo, db_kazoo_cfg, retry_on_busy_retries) / 5);
	db1_con_t* _h = (db1_con_t*)data;
	struct db_kazoo_connection *conn = CON_DB_KAZOO(_h);

	if(!cfg_get(db_kazoo, db_kazoo_cfg, retry_on_busy)) {
		return 0;
	}

	if (retry < cfg_get(db_kazoo, db_kazoo_cfg, retry_on_busy_retries)) {
		/* update stats */
		switch(group) {
		case 0:
			break;
		case 1:
			update_stat(busy1_stat, 1);
			break;
		case 2:
			if(retry % (cfg_get(db_kazoo, db_kazoo_cfg, retry_on_busy_retries) / 5) == 0) {
				LM_NOTICE("db_kazoo is busy, %d - %d\n", group, retry);
			}
			update_stat(busy2_stat, 1);
			break;
		case 3:
			if(retry % (cfg_get(db_kazoo, db_kazoo_cfg, retry_on_busy_retries) / 5) == 0) {
				LM_WARN("db_kazoo is busy, %d - %d\n", group, retry);
			}
			update_stat(busy3_stat, 1);
			break;
		default:
			if(retry % (cfg_get(db_kazoo, db_kazoo_cfg, retry_on_busy_retries) / 5) == 0) {
				LM_CRIT("db_kazoo is busy, %d - %d\n", group, retry);
			}
			update_stat(busy4_stat, 1);
			break;
		}

		if(db_kazoo_acquire_lock(conn) == 0)
			sqlite3_sleep(cfg_get(db_kazoo, db_kazoo_cfg, retry_on_busy_sleep));

		/* Return non-zero to let caller retry again. */
		return 1;
	}

	conn->failed = 1;
	update_stat(busy5_stat, 1);

	/* Return zero to let caller return SQLITE_BUSY immediately. */
	return 0;
}

static db1_res_t* db_kazoo_new_result(const db1_con_t* _h)
{
	db_kazoo_result_ptr kz_res = NULL;
	db1_res_t* res = db_new_result();
	if(res == NULL) {
		return res;
	}

	kz_res = (db_kazoo_result_ptr)pkg_malloc(sizeof(db_kazoo_result));
	if (kz_res == NULL) {
		LM_ERR("no private memory left allocating db_kazoo_result\n");
		pkg_free(res);
		return NULL;
	}
	memset(kz_res, 0, sizeof(db_kazoo_result));
	RES_PTR(res) = kz_res;
	return res;
}

static db1_res_t* db_kazoo_new_fetch_result(const db1_con_t* _h)
{
	db1_res_t* res = db_kazoo_new_result(_h);
	if(res) {
		((db_kazoo_result_ptr)RES_PTR(res))->stmt = CON_DB_KAZOO(_h)->stmt;
		CON_DB_KAZOO(_h)->stmt = NULL;
	}
	return res;
}

/*
 * Initialize database module
 * No function should be called before this
 */

static struct db_kazoo_connection * db_kazoo_new_connection(const struct db_id* id)
{
	struct db_kazoo_connection *con;
	int rc;

	con = pkg_malloc(sizeof(*con));
	if (!con) {
		LM_ERR("failed to allocate driver connection\n");
		return NULL;
	}

	memset(con, 0, sizeof(*con));
	con->hdr.ref = 1;
	con->hdr.id = (struct db_id*) id; /* set here - freed on error */

	int flags =
			  SQLITE_OPEN_CREATE
			| SQLITE_OPEN_READWRITE;


	rc = sqlite3_open_v2(id->database, &con->conn, flags, NULL);
	if (rc != SQLITE_OK) {
		pkg_free(con);
		LM_ERR("failed to open sqlite database '%s'\n", id->database);
		return NULL;
	}

	return con;
}

int kz_db_count = 0;

#define KZ_DB_TRACE_FLAGS SQLITE_TRACE_PROFILE


static const char * pragmas[] = {
		 "PRAGMA journal_mode=WAL;"
		,"PRAGMA wal_autocheckpoint=25"
		,"PRAGMA automatic_index=true;"
		,"PRAGMA read_uncommitted = true;"
		,NULL
};

int db_kazoo_wal(void *pData, sqlite3 *sql, const char *dbname, int frames)
{
	int pnLog, pnCkpt;
	if (frames % 25 == 0) {
		sqlite3_wal_checkpoint_v2(sql, dbname, SQLITE_CHECKPOINT_TRUNCATE, &pnLog, &pnCkpt);
		LM_INFO("wal: %s => %d, %d, %d\n", dbname, frames, pnLog, pnCkpt);
	}
	return SQLITE_OK;
}

void db_kazoo_init_pragmas(db1_con_t* con)
{
	int i;
	str sql;

	for(i = 0; pragmas[i] != NULL; i++) {
		sql.s = (char*)pragmas[i];
		sql.len = strlen(sql.s);
		db_kazoo_sql(con, &sql);
	}
}

db1_con_t* db_kazoo_init(const str* _url)
{
	db1_con_t* con = db_do_init2(_url, (void *) db_kazoo_new_connection, cfg_get(db_kazoo, db_kazoo_cfg, pooling));
	if(con != NULL) {
		if(dbk_trace_hook_enable) {
			sqlite3_trace_v2(CON_CONNECTION(con), KZ_DB_TRACE_FLAGS, db_kazoo_trace_handler, NULL);
		}
		db_kazoo_init_pragmas(con);
		sqlite3_busy_handler(CON_CONNECTION(con), db_kazoo_busy_handler, con);
//		sqlite3_wal_hook(CON_CONNECTION(con), db_kazoo_wal, NULL);
	}
	return con;
}

/* srdb1 (5.8+) passes the pool entry; the connection struct embeds it first */
static void db_kazoo_free_connection(struct pool_con *_pc)
{
	struct db_kazoo_connection* con = (struct db_kazoo_connection*)_pc;
	if (!con)
		return;

	sqlite3_close(con->conn);
	free_db_id(con->hdr.id);
	db_kazoo_release_lock(con);
	pkg_free(con);
}

void db_kazoo_close(db1_con_t* _h)
{
	db_do_close(_h, db_kazoo_free_connection);
}

int db_kazoo_use_table(db1_con_t* _h, const str* _t)
{
	return db_use_table(_h, _t);
}

static void db_kazoo_cleanup_query(const db1_con_t* _h, db1_res_t** _r)
{
	struct db_kazoo_connection *conn = CON_DB_KAZOO(_h);
	int rc;

	if (_r && (*_r) != NULL && RES_PTR(*_r) != NULL) {
		if(RES_STMT_DB_KAZOO(*_r) != NULL) {
			if (sqlite3_finalize(RES_STMT_DB_KAZOO(*_r)) != SQLITE_OK) {
				LM_DBG("finalize failed: %s\n", sqlite3_errmsg(CON_CONNECTION(_h)));
			}
			update_stat(cmd_stat, -1);
		}
		pkg_free(RES_PTR(*_r));
		RES_PTR(*_r) = NULL;
	}

	if (conn->stmt != NULL) {
		if(conn->failed) {
			LM_CRIT("SQL FAILED> %s\n", sqlite3_expanded_sql(conn->stmt));
			conn->failed = 0;
		}
		rc = sqlite3_finalize(conn->stmt);
		if (rc != SQLITE_OK) {
			LM_ERR("finalize failed: %s\n", sqlite3_errmsg(conn->conn));
		}
		update_stat(cmd_stat, -1);
	}

	conn->stmt = NULL;
	conn->bindpos = 0;
	db_kazoo_release_lock(conn);

}

/*
 * Reset query context
 */
static void db_kazoo_cleanup_transaction(const db1_con_t* _c)
{
	struct db_kazoo_connection *conn = CON_DB_KAZOO(_c);
	int rc;

	if (conn->stmt != NULL) {
		if(conn->failed) {
			LM_CRIT("SQL FAILED> %s\n", sqlite3_expanded_sql(conn->stmt));
			conn->failed = 0;
		}
		conn->last_id = (int) sqlite3_last_insert_rowid(conn->conn);
		conn->affected = (int)sqlite3_changes(conn->conn);
		rc = sqlite3_finalize(conn->stmt);
		if (rc != SQLITE_OK && rc != SQLITE_DONE) {
			LM_DBG("finalize failed: %s\n", sqlite3_errmsg(conn->conn));
		}
		update_stat(cmd_stat, -1);
	} else {
		LM_DBG("no stmt cleaning transaction\n");
	}

	conn->stmt = NULL;
	conn->bindpos = 0;
	db_kazoo_release_lock(conn);
}

/*
 * Convert value to sql-string as db bind index
 */
static int db_kazoo_val2str(const db1_con_t* _c, const db_val_t* _v, char* _s, int* _len)
{
	struct db_kazoo_connection *conn;
	int ret;

	if (!_c || !_v || !_s || !_len || *_len <= 0) {
		LM_ERR("invalid parameter value\n");
		return -1;
	}

	conn = CON_DB_KAZOO(_c);
	if (conn->bindpos >= DB_SQLITE_MAX_BINDS) {
		LM_ERR("too many bindings, recompile with larger DB_SQLITE_MAX_BINDS\n");
		return -2;
	}

	conn->bindarg[conn->bindpos] = _v;
	ret = snprintf(_s, *_len, "?%u", ++conn->bindpos);
	if ((unsigned) ret >= (unsigned) *_len)
		return -11;

	*_len = ret;
	return 0;
}

/*
 * Release a result set from memory
 */
int db_kazoo_free_result(db1_con_t* _h, db1_res_t* _r)
{
	int rc;
	if (!_h || !_r) {
		LM_ERR("invalid parameter value\n");
		return -1;
	}

	if (_r != NULL && RES_DB_KAZOO(_r) != NULL) {
		if(RES_STMT_DB_KAZOO(_r) != NULL) {
			if (sqlite3_finalize(RES_STMT_DB_KAZOO(_r)) != SQLITE_OK) {
				LM_ERR("finalize failed: %s\n", sqlite3_errmsg(CON_CONNECTION(_h)));
			}
			update_stat(cmd_stat, -1);
		}
		pkg_free(RES_DB_KAZOO(_r));
	}

	if (CON_DB_KAZOO(_h)->stmt != NULL) {
		if(CON_DB_KAZOO(_h)->failed) {
			LM_CRIT("SQL FAILED> %s\n", sqlite3_expanded_sql(CON_DB_KAZOO(_h)->stmt));
			CON_DB_KAZOO(_h)->failed = 0;
		}
		rc = sqlite3_finalize(CON_DB_KAZOO(_h)->stmt);
		if (rc != SQLITE_OK) {
			LM_ERR("finalize failed: %s\n", sqlite3_errmsg(CON_DB_KAZOO(_h)->conn));
		}
		update_stat(cmd_stat, -1);
	}

	CON_DB_KAZOO(_h)->stmt = NULL;
	CON_DB_KAZOO(_h)->bindpos = 0;

	if (db_free_result(_r) < 0) {
		LM_ERR("failed to free result structure\n");
		return -1;
	}

	return 0;
}

/*
 * Send an SQL query to the server
 */
static int db_kazoo_bind_params(struct db_kazoo_connection *conn, sqlite3_stmt *stmt)
{
	const db_val_t *val;
	int rc, i;

	for (i = 1; i <= conn->bindpos; i++) {
		val = conn->bindarg[i - 1];
		if (VAL_NULL(val)) {
			rc = sqlite3_bind_null(stmt, i);
		} else
			switch (VAL_TYPE(val)){
			case DB1_INT:
				rc = sqlite3_bind_int(stmt, i, VAL_INT(val));
				break;
			case DB1_BIGINT:
				rc = sqlite3_bind_int64(stmt, i, VAL_BIGINT(val));
				break;
			case DB1_DOUBLE:
				rc = sqlite3_bind_double(stmt, i, VAL_DOUBLE(val));
				break;
			case DB1_STRING:
				rc = sqlite3_bind_text(stmt, i, VAL_STRING(val), -1, NULL);
				break;
			case DB1_STR:
				rc = sqlite3_bind_text(stmt, i, VAL_STR(val).s, VAL_STR(val).len, NULL);
				break;
			case DB1_DATETIME:
				rc = sqlite3_bind_double(stmt, i, timet_to_sqlite(VAL_TIME(val)));
				break;
			case DB1_BLOB:
				rc = sqlite3_bind_blob(stmt, i,
				VAL_BLOB(val).s, VAL_BLOB(val).len, NULL);
				break;
			case DB1_BITMAP:
				rc = sqlite3_bind_int(stmt, i, VAL_BITMAP(val));
				break;
			default:
				LM_ERR("unknown bind value type %d\n", VAL_TYPE(val));
				return -1;
			}

		if (rc != SQLITE_OK) {
			LM_ERR("Parameter bind failed: %s\n", sqlite3_errmsg(conn->conn));
			return -1;
		}
	}

	return SQLITE_OK;

}

/*
 * Send an SQL query to the server
 */
static int db_kazoo_prepare(const db1_con_t* _h, const str* _s)
{
	struct db_kazoo_connection *conn = CON_DB_KAZOO(_h);
	sqlite3_stmt *stmt;
	int rc;

	conn->rc = 0;
	conn->last_id = 0;
	conn->affected = 0;

	rc = sqlite3_prepare_v2(conn->conn, _s->s, _s->len, &stmt, NULL);
	if (rc != SQLITE_OK) {
		LM_ERR("failed to prepare statement: %.*s : %s\n", _s->len, _s->s, sqlite3_errmsg(conn->conn));
		return -1;
	}
	update_stat(cmd_stat, 1);

	rc = db_kazoo_bind_params(conn, stmt);
	if (rc != SQLITE_OK) {
		LM_ERR("failed to bind parameters: %.*s : %s\n", _s->len, _s->s, sqlite3_errmsg(conn->conn));
		return -1;
	}

	conn->stmt = stmt;


	conn->bindpos = 0;
	return 0;
}

static int db_kazoo_prepare_with_limit(const db1_con_t* _h, const str* _s)
{
	if (cfg_get(db_kazoo, db_kazoo_cfg, max_rows) > 0) {
		kazoo_sql_str.len = snprintf(kazoo_sql_buf, sql_buffer_size, "%.*s limit %i", _s->len, _s->s, cfg_get(db_kazoo, db_kazoo_cfg, max_rows));
		kazoo_sql_str.s = kazoo_sql_buf;
		kazoo_sql_str.s[kazoo_sql_str.len] = '\0';
		return db_kazoo_prepare(_h, &kazoo_sql_str);
	} else {
		return db_kazoo_prepare(_h, _s);
	}

}

int db_kazoo_step3(struct db_kazoo_connection *conn, sqlite3_stmt *stmt)
{
	int rc = sqlite3_step(stmt);
	db_kazoo_release_lock(conn);
	return  rc;
}

int db_kazoo_step2(struct db_kazoo_connection *conn)
{
	int rc = sqlite3_step(conn->stmt);
	db_kazoo_release_lock(conn);
	return  rc;
}


int db_kazoo_step(sqlite3_stmt *stmt)
{
	return sqlite3_step(stmt);
}

static int db_kazoo_transaction(const db1_con_t* _h)
{
	struct db_kazoo_connection *conn = CON_DB_KAZOO(_h);
	int rc = sqlite3_exec(conn->conn, "BEGIN TRANSACTION", NULL, 0, NULL);
	int rc2;

	if (rc != SQLITE_OK) {
		LM_ERR("BEGIN TRANSACTION FAILED: %s\n", sqlite3_errmsg(conn->conn));
		return rc;
	}

	rc = db_kazoo_step2(conn);

	if (rc == SQLITE_DONE) {
		rc2 = sqlite3_exec(conn->conn, "COMMIT TRANSACTION", NULL, 0, NULL);
		LM_DBG("TRANSACTION COMMIT RESULT %i\n", rc2);
	} else {
		rc2 = sqlite3_exec(conn->conn, "ROLLBACK TRANSACTION", NULL, 0, NULL);
		LM_DBG("TRANSACTION ROLLBACK RESULT %i\n", rc2);
	}

	conn->last_id = (int) sqlite3_last_insert_rowid(conn->conn);
	conn->affected = (int)sqlite3_changes(conn->conn);

	return rc;
}


#define H3(a,b,c)       ((a<<16) + (b<<8) + c)
#define H4(a,b,c,d)     ((a<<24) + (b<<16) + (c<<8) + d)
//#define H6(a,b,c,d,e,f) ((a<<40) + (b<<32) + (c<<24) + (d<<16) + (e<<8) + f)

static int decltype_to_dbtype(const char *decltype)
{
	/* KazooDB3 has dynamic typing. It does not store the actual
	 * exact type, instead it uses 'affinity' depending on the
	 * value. We have to go through the declaration type to see
	 * what to return.
	 * The loose string matching (4 letter substring match) is what
	 * KazooDB does internally, but our return values differ as we want
	 * the more exact srdb type instead of the affinity. */

	uint32_t h = 0;

	for (; *decltype; decltype++) {
		h <<= 8;
		h += toupper(*decltype);

		switch (h & 0x00ffffff){
		case H3('I', 'N', 'T'):
			return DB1_INT;
		}

		switch (h){
		case H4('S', 'E', 'R', 'I'): /* SERIAL */
			return DB1_INT;
		case H4('B', 'I', 'G', 'I'): /* BIGINT */
			return DB1_BIGINT;
		case H4('C', 'H', 'A', 'R'):
		case H4('C', 'L', 'O', 'B'):
		case H4('V', 'A', 'R', 'C'):
//		case H6('V', 'A', 'R', 'C', 'H', 'A'):
			return DB1_STRING;
		case H4('T', 'E', 'X', 'T'):
			return DB1_STR;
		case H4('R', 'E', 'A', 'L'):
		case H4('F', 'L', 'O', 'A'): /* FLOAT */
		case H4('D', 'O', 'U', 'B'): /* DOUBLE */
			return DB1_DOUBLE;
		case H4('B', 'L', 'O', 'B'):
			return DB1_BLOB;
		case H4('T', 'I', 'M', 'E'):
		case H4('D', 'A', 'T', 'E'):
			return DB1_DATETIME;
		}
	}

	LM_ERR("sqlite decltype '%s' not recognized, defaulting to int", decltype);
	return DB1_INT;
}

static int type_to_dbtype(int type)
{
	switch (type){
	case SQLITE_INTEGER:
		return DB1_INT;
	case SQLITE_FLOAT:
		return DB1_DOUBLE;
	case SQLITE_TEXT:
		return DB1_STR;
	case SQLITE_BLOB:
		return DB1_BLOB;
	default:
		LM_ERR("UNKNOWN %d\n", type);
		/* Unknown, or NULL column value. Assume this is a
		 * string. */
		return DB1_STR;
	}
}

static str* str_dup(const char *_s)
{
	str *s;
	int len = strlen(_s);

	s = (str*) pkg_malloc(sizeof(str) + len + 1);
	if (!s)
		return NULL;

	s->len = len;
	s->s = ((char*) s) + sizeof(str);
	memcpy(s->s, _s, len);
	s->s[len] = '\0';

	return s;
}

static void str_assign(str* s, const char *_s, int len)
{
	s->s = (char *) pkg_malloc(len + 1);
	if (s->s) {
		s->len = len;
		memcpy(s->s, _s, len);
		s->s[len] = 0;
	}
}

/*
 * Read database answer and fill the structure
 */
int db_kazoo_store_result(const db1_con_t* _h, db1_res_t** _r)
{
	struct db_kazoo_connection *conn = CON_DB_KAZOO(_h);
	db1_res_t *res = NULL;
	int i, rc, num_rows = 0, num_cols = 0, num_alloc = 0;
	db_row_t *rows = NULL, *row;
	db_val_t *val;
	sqlite3_stmt *stmt = CON_DB_KAZOO(_h)->stmt;

	rc = db_kazoo_step2(conn);

	if(!_r)
		return rc;

	*_r = NULL;

	res = db_kazoo_new_result(_h);
	if (res == NULL) {
		goto no_mem;
	}

	while (rc == SQLITE_ROW) {

		if (num_rows == 0) {
			/* get column types */
			num_cols = sqlite3_column_count(stmt);
			if (db_allocate_columns(res, num_cols) != 0) {
				LM_ERR("ERROR ALLOCATING COLUMNS %d\n", num_cols);
				goto err;
			}
			RES_COL_N(res) = num_cols;

			for (i = 0; i < RES_COL_N(res); i++) {
				const char *decltype;
				int dbtype;

				RES_NAMES(res)[i] = str_dup(sqlite3_column_name(stmt, i));
				if (RES_NAMES(res)[i] == NULL) {
					goto no_mem;
				}
				decltype = sqlite3_column_decltype(stmt, i);
				if (decltype != NULL)
					dbtype = decltype_to_dbtype(decltype);
				else
					dbtype = type_to_dbtype(sqlite3_column_type(stmt, i));
				RES_TYPES(res)[i] = dbtype;
			}
		}

		if (num_rows >= num_alloc) {
			if (num_alloc)
				num_alloc *= 2;
			else
				num_alloc = 8;
			rows = pkg_realloc(rows, sizeof(db_row_t) * num_alloc);
			if (rows == NULL) {
				goto no_mem;
			}
			RES_ROWS(res) = rows;
		}

		row = &RES_ROWS(res)[num_rows];

		if (db_allocate_row(res, row) != 0) {
			goto no_mem;
		}

		num_rows++;
		RES_ROW_N(res) = num_rows; /* rows in this result set */
		RES_NUM_ROWS(res) = num_rows; /* rows in total */

		for (i = 0, val = ROW_VALUES(row); i < RES_COL_N(res); i++, val++) {
			VAL_TYPE(val) = RES_TYPES(res)[i];
			VAL_NULL(val) = 0;
			VAL_FREE(val) = 0;
			if (sqlite3_column_type(stmt, i) == SQLITE_NULL) {
				VAL_NULL(val) = 1;
			} else
				switch (VAL_TYPE(val)){
				case DB1_INT:
					VAL_INT(val) = sqlite3_column_int(stmt, i);
					break;
				case DB1_BIGINT:
					VAL_BIGINT(val) = sqlite3_column_int64(stmt, i);
					break;
				case DB1_STRING:
					/* first field of struct str* is the char* so we can just
					 * do whatever DB1_STR case does */
				case DB1_STR:
					str_assign(&VAL_STR(val), (const char*) sqlite3_column_text(stmt, i), sqlite3_column_bytes(stmt, i));
					if (!VAL_STR(val).s)
						goto no_mem;
					VAL_FREE(val) = 1;
					break;
				case DB1_DOUBLE:
					VAL_DOUBLE(val) = sqlite3_column_double(stmt, i);
					break;
				case DB1_DATETIME:
					VAL_TIME(val) = db_kazoo_to_timet(sqlite3_column_double(stmt, i));
					break;
				case DB1_BLOB:
					str_assign(&VAL_BLOB(val), (const char*) sqlite3_column_blob(stmt, i), sqlite3_column_bytes(stmt, i));
					if (!VAL_STR(val).s)
						goto no_mem;
					VAL_FREE(val) = 1;
					break;
				default:
					LM_ERR("unhandled db-type\n");
					goto err;
				}
		}
		rc = db_kazoo_step2(conn);
	}

	*_r = res;
	return 0;

no_mem:
	LM_ERR("no private memory left\n");
err:
	if (res)
		db_kazoo_free_result((db1_con_t *)_h, res);
	return -1;
}



int db_kazoo_fetch_result(const db1_con_t* _h, db1_res_t** _r, const int nrows)
{
	sqlite3_stmt *stmt = NULL;
	int i, rc = 0, num_cols=0, num_rows = 0, num_alloc = 0;
	db_row_t *rows = NULL, *row;
	db_val_t *val;
	int is_new = 0;
	struct db_kazoo_connection *conn = CON_DB_KAZOO(_h);

	if (!_h || !_r || nrows < 0) {
		LM_ERR("Invalid parameter value\n");
		return -1;
	}

	/* exit if the fetch count is zero */
	if (nrows == 0) {
		db_kazoo_free_result((db1_con_t*)_h, *_r);
		*_r = 0;
		return 0;
	}


	if(*_r==0) {
		/* Allocate a new result structure */
		*_r = db_kazoo_new_fetch_result(_h);
		if (*_r == 0) {
			LM_ERR("no memory left\n");
			return -2;
		}

		is_new = 1;

	}

	stmt = RES_STMT_DB_KAZOO(*_r);
	if(RES_RC_DB_KAZOO(*_r) == 0 || RES_RC_DB_KAZOO(*_r) == SQLITE_ROW) {
		rc = db_kazoo_step3(conn, stmt);
	} else {
		rc = RES_RC_DB_KAZOO(*_r);
	}

	if(is_new == 1) {

		num_cols = sqlite3_column_count(stmt);
		if (num_cols == 0) {
			LM_DBG("0 columns in fetch\n");
			return 0;
		}

		if (db_allocate_columns(*_r, num_cols) != 0) {
			LM_ERR("no memory left\n");
			return 0;
		}

		RES_COL_N(*_r) = num_cols;

		for (i = 0; i < RES_COL_N(*_r); i++) {
			const char *decltype;
			int dbtype;

			RES_NAMES(*_r)[i] = str_dup(sqlite3_column_name(stmt, i));
			if (RES_NAMES(*_r)[i] == NULL)
				goto no_mem;
			decltype = sqlite3_column_decltype(stmt, i);
			if (decltype != NULL)
				dbtype = decltype_to_dbtype(decltype);
			else
				dbtype = type_to_dbtype(sqlite3_column_type(stmt, i));
			RES_TYPES(*_r)[i] = dbtype;
		}
	} else {
		/* free old rows */
		if(RES_ROWS(*_r)!=0)
			db_free_rows(*_r);
		RES_ROWS(*_r) = 0;
		RES_ROW_N(*_r) = 0;
		if (rc != SQLITE_ROW) {
			return 0;
		}
	}

	while(num_rows < nrows && rc == SQLITE_ROW) {

		if (num_rows >= num_alloc) {
			if (num_alloc)
				num_alloc *= 2;
			else
				num_alloc = 64;
			rows = pkg_realloc(rows, sizeof(db_row_t) * num_alloc);
			if (rows == NULL)
				goto no_mem;
			RES_ROWS(*_r) = rows;
		}

		row = &RES_ROWS(*_r)[num_rows];
		num_rows++;

		if (db_allocate_row(*_r, row) != 0)
			goto no_mem;

		for (i = 0, val = ROW_VALUES(row); i < RES_COL_N(*_r); i++, val++) {
			VAL_TYPE(val) = RES_TYPES(*_r)[i];
			VAL_NULL(val) = 0;
			VAL_FREE(val) = 0;
			if (sqlite3_column_type(stmt, i) == SQLITE_NULL) {
				VAL_NULL(val) = 1;
			} else
				switch (VAL_TYPE(val)){
				case DB1_INT:
					VAL_INT(val) = sqlite3_column_int(stmt, i);
					break;
				case DB1_BIGINT:
					VAL_BIGINT(val) = sqlite3_column_int64(stmt, i);
					break;
				case DB1_STRING:
					/* first field of struct str* is the char* so we can just
					 * do whatever DB1_STR case does */
				case DB1_STR:
					str_assign(&VAL_STR(val), (const char*) sqlite3_column_text(stmt, i), sqlite3_column_bytes(stmt, i));
					if (!VAL_STR(val).s)
						goto no_mem;
					VAL_FREE(val) = 1;
					break;
				case DB1_DOUBLE:
					VAL_DOUBLE(val) = sqlite3_column_double(stmt, i);
					break;
				case DB1_DATETIME:
					VAL_TIME(val) = db_kazoo_to_timet(sqlite3_column_double(stmt, i));
					break;
				case DB1_BLOB:
					str_assign(&VAL_BLOB(val), (const char*) sqlite3_column_blob(stmt, i), sqlite3_column_bytes(stmt, i));
					if (!VAL_STR(val).s)
						goto no_mem;
					VAL_FREE(val) = 1;
					break;
				default:
					LM_ERR("unhandled db-type\n");
					goto err;
				}
		}

		if(num_rows < nrows) {
			rc = db_kazoo_step3(conn, stmt);
		}
	}

	RES_ROW_N(*_r) = num_rows;     /* rows in this result set */
	RES_NUM_ROWS(*_r) += num_rows; /* rows in total */
	RES_LAST_ROW(*_r) = RES_NUM_ROWS(*_r);
	RES_RC_DB_KAZOO(*_r) = rc;

	return 0;

no_mem:
	LM_ERR("no private memory left\n");
err:
	if (*_r) {
		db_kazoo_free_result((db1_con_t*)_h, *_r);
		*_r = 0;
	}

	return -1;

}



/*
 * Query table for specified rows
 * _h: structure representing database connection
 * _k: key names
 * _op: operators
 * _v: values of the keys that must match
 * _c: column names to return
 * _n: number of key=values pairs to compare
 * _nc: number of columns to return
 * _o: order by the specified column
 */
int db_kazoo_query(const db1_con_t* _h, const db_key_t* _k, const db_op_t* _op, const db_val_t* _v, const db_key_t* _c, int _n, int _nc, const db_key_t _o, db1_res_t** _r)
{
	int rc = db_do_query(_h, _k, _op, _v, _c, _n, _nc, _o, _r, db_kazoo_val2str, db_kazoo_prepare_with_limit, db_kazoo_store_result);
	return rc;
}



/*
 * Insert a row into specified table
 * _h: structure representing database connection
 * _k: key names
 * _v: values of the keys
 * _n: number of key=value pairs
 */
int db_kazoo_insert(const db1_con_t* _h, const db_key_t* _k, const db_val_t* _v, int _n)
{
	int rc = -1;

	rc = db_do_insert(_h, _k, _v, _n, db_kazoo_val2str, db_kazoo_prepare);
	if (rc == 0) {
		rc = db_kazoo_transaction(_h);
	} else {
		LM_ERR("INSERT failed:  %d - %s\n", rc, sqlite3_errmsg(CON_CONNECTION(_h)));
	}
	db_kazoo_cleanup_transaction(_h);

	return rc == SQLITE_DONE ? 0 : -1;
}

/*
 * Delete a row from the specified table
 * _h: structure representing database connection
 * _k: key names
 * _o: operators
 * _v: values of the keys that must match
 * _n: number of key=value pairs
 */
int db_kazoo_delete(const db1_con_t* _h, const db_key_t* _k, const db_op_t* _o, const db_val_t* _v, int _n)
{
	int rc;

	rc = db_do_delete(_h, _k, _o, _v, _n, db_kazoo_val2str, db_kazoo_prepare);
	if (rc == 0) {
		rc = db_kazoo_transaction(_h);
	} else {
		LM_ERR("DELETE failed:  %d - %s\n", rc, sqlite3_errmsg(CON_CONNECTION(_h)));
	}
	db_kazoo_cleanup_transaction(_h);
	return rc == SQLITE_DONE ? 0 : -1;
}

/*
 * Update some rows in the specified table
 * _h: structure representing database connection
 * _k: key names
 * _o: operators
 * _v: values of the keys that must match
 * _uk: updated columns
 * _uv: updated values of the columns
 * _n: number of key=value pairs
 * _un: number of columns to update
 */
int db_kazoo_update(const db1_con_t* _h, const db_key_t* _k, const db_op_t* _o, const db_val_t* _v, const db_key_t* _uk, const db_val_t* _uv, int _n, int _un)
{
	int rc;

	rc = db_do_update(_h, _k, _o, _v, _uk, _uv, _n, _un, db_kazoo_val2str, db_kazoo_prepare);
	if (rc == 0) {
		rc = db_kazoo_transaction(_h);
	} else {
		LM_ERR("UPDATE failed:  %d - %s\n", rc, sqlite3_errmsg(CON_CONNECTION(_h)));
	}
	db_kazoo_cleanup_transaction(_h);

	return rc == SQLITE_DONE ? 0 : -1;
}

int db_kazoo_raw_query(const db1_con_t* _h, const str* _s, db1_res_t** _r)
{
	int rc;

	rc = db_kazoo_do_submit_query(_h, _s, db_kazoo_prepare);
	if (rc == 0) {
		if(!strncasecmp(_s->s, "SELECT ", 7)) {
			LM_DBG("RAW SELECT : %.*s\n", _s->len, _s->s);
			rc = db_kazoo_store_result(_h, _r);
			db_kazoo_cleanup_query(_h, _r);
		} else {
			LM_DBG("RAW TRANSACTION : %.*s\n", _s->len, _s->s);
			rc = db_kazoo_transaction(_h);
			db_kazoo_cleanup_transaction(_h);
		}
	}

	if (rc != SQLITE_DONE && rc != SQLITE_OK && rc != SQLITE_ROW) {
		LM_ERR("RAW QUERY failed:  %d - %s\n", rc, sqlite3_errmsg(CON_CONNECTION(_h)));
		return -1;
	}

	return 0;
}

/**
 * Just like insert, but replace the row if it exists.
 * \param _h database handle
 * \param _k key names
 * \param _v values of the keys that must match
 * \param _n number of key=value pairs
 * \return zero on success, negative value on failure
 */
int db_kazoo_replace(const db1_con_t* _h, const db_key_t* _k,
		const db_val_t* _v, const int _n, const int _un, const int _m)
{
	int rc;

	rc = db_kazoo_do_replace(_h, _k, _v, _n, db_kazoo_val2str, db_kazoo_prepare);
	if (rc == 0) {
		rc = db_kazoo_transaction(_h);
	} else {
		LM_ERR("INSERT failed:  %d - %s\n", rc, sqlite3_errmsg(CON_CONNECTION(_h)));
	}
	db_kazoo_cleanup_transaction(_h);

	return rc == SQLITE_DONE ? 0 : -1;

}

/**
 * Returns the last inserted ID.
 * \param _h database handle
 * \return returns the ID as integer or returns 0 if the previous statement
 * does not use an AUTO_INCREMENT value.
 */
int db_kazoo_last_inserted_id(const db1_con_t* _h)
{
	if (!_h) {
		LM_ERR("invalid parameter value\n");
		return -1;
	}
	return CON_DB_KAZOO(_h)->last_id;
}


/**
 * Returns the affected rows of the last query.
 * \param _h database handle
 * \return returns the affected rows as integer or -1 on error.
 */
int db_kazoo_affected_rows(const db1_con_t* _h)
{
	if (!_h) {
		LM_ERR("invalid parameter value\n");
		return -1;
	}
	return CON_DB_KAZOO(_h)->affected;
}

int db_kazoo_sql(const db1_con_t* _h, const str* _s)
{
	int rc;

	rc = sqlite3_exec(CON_CONNECTION(_h), _s->s, NULL, NULL, NULL);

	return rc;
}

 /**
  * Allocate a buffer for database module
  * No function should be called before this
  * \return zero on success, negative value on failure
  */
 int db_kazoo_alloc_buffer()
 {
     if (db_api_init())
     {
         LM_ERR("Failed to initialise db api\n");
 		return -1;
     }

     kazoo_sql_buf = pkg_malloc(sql_buffer_size);
     if (kazoo_sql_buf == NULL)
         return -1;
     else
         return 0;
 }

void db_kazoo_release_buffer()
{
	if (kazoo_sql_buf != NULL) {
		pkg_free(kazoo_sql_buf);
		kazoo_sql_buf = NULL;
	}
}


int db_kazoo_do_replace(const db1_con_t* _h, const db_key_t* _k, const db_val_t* _v,
 	const int _n, int (*val2str) (const db1_con_t*, const db_val_t*, char*,
 	int*), int (*submit_query)(const db1_con_t* _h, const str* _c))
 {
 	int off, ret;
 	str sql_str;

 	if (!_h || !_k || !_v || !val2str|| !submit_query) {
 		LM_ERR("invalid parameter value\n");
 		return -1;
 	}

 	ret = snprintf(kazoo_sql_buf, sql_buffer_size, "replace into %s%.*s%s (",
 			CON_TQUOTESZ(_h), CON_TABLE(_h)->len, CON_TABLE(_h)->s, CON_TQUOTESZ(_h));
 	if (ret < 0 || ret >= sql_buffer_size) goto error;
 	off = ret;

 	ret = db_print_columns(kazoo_sql_buf + off, sql_buffer_size - off, _k, _n, CON_TQUOTESZ(_h));
 	if (ret < 0) return -1;
 	off += ret;

 	ret = snprintf(kazoo_sql_buf + off, sql_buffer_size - off, ") values (");
 	if (ret < 0 || ret >= (sql_buffer_size - off)) goto error;
 	off += ret;

 	ret = db_print_values(_h, kazoo_sql_buf + off, sql_buffer_size - off, _v, _n,
 	val2str);
 	if (ret < 0) return -1;
 	off += ret;

 	if (off + 2 > sql_buffer_size) goto error;
 	kazoo_sql_buf[off++] = ')';
 	kazoo_sql_buf[off] = '\0';
 	sql_str.s = kazoo_sql_buf;
 	sql_str.len = off;

	LM_DBG("DB KAZOO REPLACE => %.*s\n", sql_str.len, sql_str.s);

 	if (db_kazoo_do_submit_query(_h, &sql_str, submit_query) < 0) {
 	        LM_ERR("error while submitting query\n");
 		return -2;
 	}
 	return 0;

  error:
 	LM_ERR("error while preparing replace operation\n");
 	return -1;
 }
