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

#include "kz.h"

KZ_BEGIN_EXTERN_C

char *kz_tr_set_crt_buffer(void);

KZ_END_EXTERN_C

extern "C" {

#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h> // TODO is this needed

#include "../../core/str.h"
#include "../../core/lock_ops.h"
#include "../../core/mem/mem.h"
#include "../../core/mem/shm_mem.h"
#include "../../core/dprint.h"
#include "../../core/fmsg.h"
#include "../../core/cfg/cfg_struct.h"
#include "../../core/hashes.h"
#include "../../core/xavp.h"
#include "../../core/counters.h"
#include "../../core/pvar.h"

#include "kazoo_params.h"
#include "kz_amqp_defs.h"
#include "kz_amqp_mem.h"
#include "hashes.h"

extern str dbk_primary_zone_name;
extern str kz_deliver_to_pid;
extern char* last_payload_result;
extern char* kz_call_id;
sr_xavp_t ** kz_get_status_crt();
extern gen_lock_t * kz_status_lock;
extern int *kz_worker_pipes_fds;

extern stat_var *kz_stat_event_delivered;
extern stat_var *kz_stat_event_received;
extern stat_var *kz_stat_messages_published;
extern stat_var *kz_stat_messages_received;
extern stat_var *kz_stat_messages_received_self;
extern stat_var *kz_stat_messages_returned;
extern stat_var *kz_stat_transactions_published;
extern stat_var *kz_stat_transactions_received;
extern stat_var *kz_stat_transactions_delivered;
extern stat_var *kz_stat_transactions_expired;
extern stat_var *kz_stat_transactions_returned;
extern stat_var *kz_stat_calls_published;
extern stat_var *kz_stat_calls_received;
extern stat_var *kz_stat_calls_delivered;
extern stat_var *kz_stat_calls_returned;
extern stat_var *kz_stat_relay_published;
extern stat_var *kz_stat_relay_received;
extern stat_var *kz_stat_relay_delivered;
extern stat_var *kz_stat_relay_returned;

int fork_process(int child_id,char *desc,int make_sock);
void kz_amqp_cb_ok(kz_amqp_cmd_ptr cmd);
void kz_amqp_cb_error(kz_amqp_cmd_ptr cmd);
int kz_amqp_on_reply_ex(kz_amqp_consumer_delivery_ptr Evt);
void kz_amqp_consumer_event(kz_amqp_consumer_delivery_ptr Evt);
int kz_amqp_ws_relay(kz_amqp_consumer_delivery_ptr Evt);
void kz_amqp_util_encode_string(const char * key, char *pdest);

}

static inline pid_t kz_gettid(void)
{
  return syscall(SYS_gettid);
}

#include <string>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <memory>
#include <sys/wait.h>
#include <type_traits>
#include <cstdlib>
#include <cxxabi.h>
#include <regex>
#include <functional>
#include <atomic>
#include <mutex>
#include <random>
#include <regex>

typedef signed char		int8_t;

#include "kazoo.hpp"
#include "include/pipe_loop.hpp"
#include "include/uuid.hpp"
#include "include/amqp_ows.hpp"
#include "include/date.hpp"

using json = nlohmann::json;

#define AMQP_WORKERS_RANKING 127

#define SECONDS_IN_MINUTE 60
#define SECONDS_IN_HOUR   3600
#define SECONDS_IN_DAY    86400
#define SECONDS_IN_WEEK   604800
#define SECONDS_IN_YEAR   31540000

#define UNIX_EPOCH_IN_GREGORIAN 62167219200
#define MILLISECONDS_IN_SECOND  1000
#define MILLISECONDS_IN_MINUTE  (MILLISECONDS_IN_SECOND * SECONDS_IN_MINUTE)
#define MILLISECONDS_IN_HOUR    (MILLISECONDS_IN_SECOND * SECONDS_IN_HOUR)
#define MILLISECONDS_IN_DAY     (MILLISECONDS_IN_SECOND * SECONDS_IN_DAY)

#define MICROSECONDS_IN_SECOND (1000 * MILLISECONDS_IN_SECOND)


std::string server_uuid;

std::string do_replace( std::string const & in, std::string const & from, std::string const & to )
{
  return std::regex_replace( in, std::regex(from), to );
}


namespace q
{

template<class BidirIt, class Traits, class CharT, class UnaryFunction>
std::basic_string<CharT> regex_replace(BidirIt first, BidirIt last,
    const std::basic_regex<CharT,Traits>& re, UnaryFunction f)
{
    std::basic_string<CharT> s;

    typename std::match_results<BidirIt>::difference_type
        positionOfLastMatch = 0;
    auto endOfLastMatch = first;

    auto callback = [&](const std::match_results<BidirIt>& match)
    {
        auto positionOfThisMatch = match.position(0);
        auto diff = positionOfThisMatch - positionOfLastMatch;

        auto startOfThisMatch = endOfLastMatch;
        std::advance(startOfThisMatch, diff);

        s.append(endOfLastMatch, startOfThisMatch);
        s.append(f(match));

        auto lengthOfMatch = match.length(0);

        positionOfLastMatch = positionOfThisMatch + lengthOfMatch;

        endOfLastMatch = startOfThisMatch;
        std::advance(endOfLastMatch, lengthOfMatch);
    };

    std::regex_iterator<BidirIt> begin(first, last, re), end;
    std::for_each(begin, end, callback);

    s.append(endOfLastMatch, last);

    return s;
}

template<class Traits, class CharT, class UnaryFunction>
std::string regex_replace(const std::string& s,
    const std::basic_regex<CharT,Traits>& re, UnaryFunction f)
{
    return regex_replace(s.cbegin(), s.cend(), re, f);
}

}

namespace kz
{

void reset_call_id()
{
	if(kz_call_id) {
		pkg_free(kz_call_id);
	}
	kz_call_id = NULL;
};

void set_call_id_str_len(char *ci, int len)
{
	reset_call_id();
	if(!ci) return;
	char* value = (char*) pkg_malloc(len+1);
	if(value) {
		memcpy(value, ci, len);
		value[len] = '\0';
		kz_call_id = value;
	}
}

void set_call_id_str(char *ci)
{
	reset_call_id();
	if(!ci) return;
	int len = strlen(ci);
	set_call_id_str_len(ci, len);
}

void set_call_id(kz_amqp_consumer_delivery_ptr Evt)
{
	reset_call_id();
	if(!Evt) return;
	set_call_id_str(Evt->ci);
}

std::string encode(const std::string& to_encode)
{
	char routing_key_buff[MAX_ROUTING_KEY_SIZE+1];
	memset(routing_key_buff,0, sizeof(routing_key_buff));
	kz_amqp_util_encode_string(to_encode.c_str(), routing_key_buff);
	return std::string(routing_key_buff);
}


std::string strformat(const std::string fmt_str, ...)
{
    va_list ap;
    char *fp = NULL;
    va_start(ap, fmt_str);
    vasprintf(&fp, fmt_str.c_str(), ap);
    va_end(ap);
    std::unique_ptr<char[]> formatted(fp);
    return std::string(formatted.get());
}

std::string hostname()
{
	return std::string(dbk_node_hostname.s, dbk_node_hostname.len);
}

std::string node_name()
{
	return std::string(dbk_node_name.s, dbk_node_name.len);
}


std::string node()
{
	std::ostringstream stringStream;
	stringStream << node_name() << "@" << hostname();
	return stringStream.str();
}

// copied from core/xavp.c xavp_get_internal
sr_xavp_t *kz_xavp_get(str *name, sr_xavp_t **list)
{
	sr_xavp_t *avp;
	unsigned int id;

	if(name==NULL || name->s==NULL)
		return NULL;
	id = get_hash1_raw(name->s, name->len);

	if(!list || !(*list))
		return NULL;
	avp = *list;
	while(avp)
	{
		if(avp->id==id && avp->name.len==name->len && strncmp(avp->name.s, name->s, name->len) == 0) {
			return avp;
		}
		avp = avp->next;
	}
	return NULL;
}

sr_xavp_t *kz_xavp_store_value(str *name, sr_xval_t *val, sr_xavp_t **list)
{
	sr_xavp_t *cur;

	if(val==NULL)
		return NULL;

	/* Find the current value */
	cur = kz_xavp_get(name, list);
	if(cur==NULL) {
		return xavp_add_value(name, val, list);
	} else {
		return xavp_set_value(name, 0, val, list);
	}
}

typedef std::map<std::string, int> xavp_counters;

void kz_xavp_status_update(xavp_counters counters)
{
	sr_xavp_t **xavp=NULL;
	sr_xavp_t *list=NULL;
	sr_xavp_t *new_xavp=NULL;
	sr_xval_t xval;
	sr_xavp_t **top=NULL;

	if(kz_xavp_status.s==NULL || kz_xavp_status.len<=0) {
		return;
	}

	lock_get(kz_status_lock);
	top = kz_get_status_crt();

	list = kz_xavp_get(&kz_xavp_status, top);
	xavp = list ? &list->val.v.xavp : &new_xavp;

	memset(&xval, 0, sizeof(sr_xval_t));
	xval.type = SR_XTYPE_LONG;

	for(auto& c : counters)
	{
		str name = {(char*)c.first.c_str(), (int)c.first.length()};
		xval.v.l = c.second;
		kz_xavp_store_value(&name, &xval, xavp);
	}

	if(list==NULL) {
		/* no kz_xavp_status xavp in root list - add it */
		xval.type = SR_XTYPE_XAVP;
		xval.v.xavp = *xavp;
		if(xavp_add_value(&kz_xavp_status, &xval, top)==NULL) {
			LM_ERR("cannot add amqp xavp to root list\n");
			xavp_destroy_list(xavp);
		}
	}

	lock_release(kz_status_lock);

}

std::string rand_string(int max_length)
{
	std::string possible_characters = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
	std::random_device rd;
    std::mt19937 engine(rd());
    std::uniform_int_distribution<> dist(0, possible_characters.size()-1);
    std::string ret = "";
    for(int i = 0; i < max_length; i++){
        int random_index = dist(engine); //get index between 0 and possible_characters.size()-1
        ret += possible_characters[random_index];
    }
    return ret;
}

std::ostringstream& AMQPLog::Get(AMQPLogLevel level, const char * loc, const char * fun)
{
   os << loc << fun << (strchr(fun, '(') ? " : " : "() : ");
   messageLevel = level;
   return os;
}

AMQPLog::~AMQPLog()
{
	if(messageLevel <= cfg_get(kz_amqp, kz_amqp_cfg, log_level)) {
		os << std::endl;
		std::lock_guard<std::mutex> lck (mtx);
		std::string str_log = os.str();
//		LOG___(DEFAULT_FACILITY, messageLevel, NULL, "", "%s", (char*) str_log.c_str());
		KZ_LOG_FX(DEFAULT_FACILITY, messageLevel, NULL, "", NULL, "%s", (char*) str_log.c_str());
	}
	os.clear();
}

uint64_t current_timestamp_in_micro()
{
	using namespace std::chrono;
	using time_stamp = time_point<system_clock, microseconds>;
	time_stamp ts = time_point_cast<microseconds>(system_clock::now());
	return (uint64_t) ts.time_since_epoch().count();
}

uint64_t current_timestamp_in_nano()
{
	using namespace std::chrono;
	using time_stamp = time_point<system_clock, nanoseconds>;
	time_stamp ts = time_point_cast<nanoseconds>(system_clock::now());
	return (uint64_t) ts.time_since_epoch().count();
}

std::mutex AMQPLog::mtx;

std::mutex stats_mtx;

void kz_mt_update_stat(stat_var *stat, int count)
{
	std::lock_guard<std::mutex> lck (stats_mtx);
	update_stat(stat, count);
}

Connections mystore = GlobalConnections::getInstance();
Exchanges gExchanges = GlobalExchanges::getInstance();
Exchanges gDeclaredExchanges = GlobalExchanges::getInstance();

typedef std::map<std::string,kz_amqp_cmd_ptr> sync_calls_map;

sync_calls_map sync_calls;
std::mutex sync_calls_mutex;

template <typename Collection,typename unop>
void for_each(Collection col, unop op){
  std::for_each(col.begin(),col.end(),op);
}

template <typename Collection,typename Predicate>
Collection filter(Collection col,Predicate predicate) {
	Collection loc;
	std::copy_if( col.begin() , col.end() , std::inserter( loc , loc.end()) , predicate);
	return loc;
}

typedef std::vector<kz::ConnectionInfo> ConnectionInfoList;
typedef std::map<std::string,ConnectionInfoList> ZoneMap;
std::map<std::string,kz::Listener> listeners;

ZoneMap zone_map(kz::Connections connections)
{
	ZoneMap zone_map;
	for(auto& it : connections)
	{
		std::string zone((const char *) it.second["zone"]);
		if(zone_map.find(zone) != zone_map.end()) {
			zone_map[zone].push_back(it.second);
		} else {
			zone_map[zone] = ConnectionInfoList({it.second});
		}
	}

	return zone_map;
};

ZoneMap zonemap;
ZoneMap federated;
ConnectionInfoList primary_zone;
std::string local_zone;

std::map<std::string, int> connection_count;
std::map<std::string, int> connection_error;
std::map<std::string, int> connection_zone_count;
std::map<std::string, int> connection_zone_error;
std::map<std::string, int> connection_name_count;
std::map<std::string, int> connection_name_error;
std::mutex count_mutex;

void initialize_counters()
{
	for(auto& it : mystore) {
		connection_count[it.first] = 0;
		connection_error[it.first] = 0;
	}

	connection_zone_count[local_zone] = 0;
	connection_zone_error[local_zone] = 0;
	for(auto& it : federated) {
		connection_zone_count[it.first] = 0;
		connection_zone_error[it.first] = 0;
	}

	for(auto& it : listeners) {
		connection_name_count[it.first] = 0;
		connection_name_error[it.first] = 0;

		connection_name_count[it.first + "::" + local_zone] = 0;
		connection_name_error[it.first + "::" + local_zone] = 0;
		for(auto& f : federated) {
			connection_name_count[it.first + "::" + f.first] = 0;
			connection_name_error[it.first + "::" + f.first] = 0;
		}
	}
}

class SIPWorker : public kz::PipeWorker<kz_amqp_consumer_delivery_ptr>
{
public:
	SIPWorker(int in, int out) :
		kz::PipeWorker<kz_amqp_consumer_delivery_ptr>(in, out)
		{
		};

	bool send_error(kz_amqp_cmd_ptr cmd)
	{
		cmd->return_code = -1;
		kz_amqp_consumer_delivery_ptr ptr = (kz_amqp_consumer_delivery_ptr) shm_malloc(sizeof(kz_amqp_consumer_delivery));
		if(ptr == NULL) {
			LM_ERR("NO MORE SHARED MEMORY!");
			return false;
		}
		memset(ptr, 0, sizeof(kz_amqp_consumer_delivery));
		ptr->cmd = cmd;

		if (write(out(), &ptr, sizeof(ptr)) != sizeof(ptr)) {
			kz_amqp_free_consumer_delivery(ptr);
			return false;
		}

		return true;
	}

	void send_result(kz_amqp_consumer_delivery_ptr cmd)
	{

	}

protected:

	void TransactionOk(kz_amqp_cmd_ptr cmd)
	{
		kz_amqp_cb_ok(cmd);
	}

	void TransactionError(kz_amqp_cmd_ptr cmd)
	{
		kz_amqp_cb_error(cmd);
	}

	void handle_call_command(kz_amqp_consumer_delivery_ptr Evt)
	{
		if(!Evt->no_stat) update_stat(kz_stat_calls_delivered, 1);
		kz_amqp_cmd_ptr cmd = Evt->cmd;
		cmd->return_payload = Evt->payload;
		cmd->ci = Evt->ci;
		Evt->payload = NULL;
		Evt->ci = NULL;
		Evt->cmd = NULL;
		lock_release(&cmd->lock);
	}

	void handle_async_call_command(kz_amqp_consumer_delivery_ptr Evt)
	{
		kz_amqp_cmd_ptr cmd = Evt->cmd;
		cmd->ci = Evt->ci;
		Evt->ci = NULL;

		if(cmd->return_code == 200) {
			set_last_result(Evt->payload);
			TransactionOk(cmd);
		} else {
			reset_last_result();
			TransactionError(cmd);
		}
	}

	void handle_transaction(kz_amqp_consumer_delivery_ptr Evt)
	{
		AMQP_DBG << "received transaction reply " << Evt->t_hash <<":" << Evt->t_label;
		if(!Evt->no_stat) update_stat(kz_stat_transactions_delivered, 1);
		set_last_result(Evt->payload);
		int res = kz_amqp_on_reply_ex(Evt);
		AMQP_DBG << "handle transaction result " << res;
		if(res != 0) {
			update_stat(kz_stat_transactions_expired, 1);
			Evt->no_stat = 1;
			handle_event(Evt);
		}

	}

	void handle_relay(kz_amqp_consumer_delivery_ptr Evt)
	{
		AMQP_DBG << "received relay message " << Evt->t_hash;
		if(!Evt->no_stat) update_stat(kz_stat_relay_delivered, 1);
		int res = kz_amqp_ws_relay(Evt);
		AMQP_DBG << "handle relay result " << res;
		if(res != 0) {
			update_stat(kz_stat_transactions_expired, 1);
			Evt->no_stat = 1;
			handle_event(Evt);
		}

	}

	void handle_command(kz_amqp_consumer_delivery_ptr Evt)
	{
		switch(Evt->cmd->type)
		{
		case KZ_AMQP_CMD_ASYNC_CALL:
			handle_async_call_command(Evt);
			break;

		case KZ_AMQP_CMD_CALL:
			AMQP_WARN << "let devs know this happened";
			handle_call_command(Evt);
			break;

		default:
			AMQP_WARN << "unhandled command : " << Evt->cmd->type << " - " << Evt->cmd->payload;
			break;
		}
	};

	void handle_event(kz_amqp_consumer_delivery_ptr Evt)
	{
		if(!Evt->no_stat) update_stat(kz_stat_event_delivered, 1);
		AMQP_DBG << "consumer " << my_pid()
					<< " : " << (Evt->event_category ? Evt->event_category : "no_event_category")
					<< " : " << (Evt->event_name ? Evt->event_name : "no_event_name");
		AMQP_TRACE << "consumer " << my_pid()
					<< (Evt->event_category ? Evt->event_category : "no_event_category")
					<< " : " << (Evt->event_name ? Evt->event_name : "no_event_name")
					<< " payload " << (Evt->payload ? Evt->payload : " NULL");
		if(Evt->payload) kz_amqp_consumer_event(Evt);
	}

	void reset_last_result()
	{
		if(last_payload_result) {
			pkg_free(last_payload_result);
		}
		last_payload_result = NULL;
	};

	void set_last_result(char *payload)
	{
		reset_last_result();
		int len = strlen(payload);
		char* value = (char*) pkg_malloc(len+1);
		if(value) {
			memcpy(value, payload, len);
			value[len] = '\0';
			last_payload_result = value;
		}
	}

	virtual void OnData(kz_amqp_consumer_delivery_ptr Evt)
	{
		cfg_update();

		set_call_id(Evt);

		if(Evt->type == KZ_AMQP_CALL) {
			handle_command(Evt);
		} else if(Evt->type == KZ_AMQP_TRANSACTION) {
			handle_transaction(Evt);
		} else if(Evt->type == KZ_AMQP_EVENT) {
			handle_event(Evt);
		} else if(Evt->type == KZ_AMQP_RELAY) {
			handle_relay(Evt);
		} else {
			AMQP_ERR << "unexpected delivery type " << Evt->type;
		}
		kz_amqp_free_consumer_delivery(Evt);
	}

	virtual void OnStop()
	{

		AMQP_DBG << "handle pending reads";
		kz_amqp_consumer_delivery_ptr cmd = NULL;
		while ((cmd = Read()) != NULL) {
			kz_amqp_free_consumer_delivery(cmd);
		}
		AMQP_DBG << "done handle pending reads";
		reset_last_result();
		reset_call_id();
	};

};

std::vector<SIPWorker*> sip_workers;
std::atomic<uint> sip_worker_idx(0);

int GetNextSIPWorkerIndex()
{
	int idx = sip_worker_idx;
	if(idx >= sip_workers.size()) {
		idx = 0;
	}

	sip_worker_idx++;
	if(sip_worker_idx >= sip_workers.size()) {
		sip_worker_idx = 0;
	}
	return idx;
}

void fire_amqp_event(std::string category, std::string event, std::string name, std::string host, std::string zone, std::string msg = "")
{
	json jobj = json::object();
	std::string uuid_str = uuids::to_string(uuids::uuid_system_generator{}());
	jobj["Event-Category"] = category;
	jobj["Event-Name"] = event;
	jobj["host"] = host;
	jobj["zone"] = zone;
	jobj["name"] = name;
	jobj["message"] = msg;
	jobj["Msg-ID"] = uuid_str;

	kz_amqp_consumer_delivery_ptr ptr = kz_amqp_alloc_consumer_delivery();
	if(ptr == NULL) {
		AMQP_ERR << "error allocating shared memory for consumer delivery : " << jobj;
		return;
	}
	ptr->no_stat = 1;
	ptr->type = KZ_AMQP_EVENT;

	std::ostringstream jobjStream;
	jobjStream << jobj;
	std::string str_payload = jobjStream.str();
	ptr->payload = kz_amqp_string_dup((char*)str_payload.c_str());
	ptr->ci = kz_amqp_string_dup((char*)uuid_str.c_str());

	int idx = GetNextSIPWorkerIndex();
	AMQP_TRACE << "sending to sip worker " << idx << " : " << jobj;
	sip_workers[idx]->receive(ptr);

}

typedef json *json_ptr;

class AMQPHandler : public LibEvHandler
{
private:
	virtual void onError(AMQP::TcpConnection *connection, const char *message) override;

	virtual void onReady(AMQP::TcpConnection *connection) override;

	virtual void onDetached(AMQP::TcpConnection *connection) override;

	virtual void onProperties(AMQP::TcpConnection *connection, const AMQP::Table &server, AMQP::Table &client) override;

	virtual uint16_t onNegotiate(AMQP::TcpConnection *connection, uint16_t interval) override;

	void dec_connection(std::string uri, std::string zone, std::string name);

public:
    AMQPHandler(struct ev_loop *loop) :
    	LibEvHandler(loop, dbk_connect_timeout),
    	_error(false),
    	_connected(false)
    {}

    virtual ~AMQPHandler() = default;

	bool hasError()
	{
		return _error;
	}

	void setError(AMQP::TcpConnection *connection, const char *message)
	{
		onError(connection, message);
	}

protected:
	bool _error;
	bool _connected;
};

class AMQPConnection : public AMQP::TcpConnection
{
public:
	AMQPConnection(AMQPHandler *handler, const kz::ConnectionInfo& info, std::string name) :
		AMQP::TcpConnection(handler, info.uri()),
		m_info(info),
		m_handler(handler),
		m_name(name)
	{}

	AMQPHandler * handler() const {return m_handler;}
	const ConnectionInfo& info() {return m_info;}
	const std::string& name() {return m_name;}

	void setError(const char *message)
	{
		m_handler->setError(this, message);
	}

protected:
	const ConnectionInfo& m_info;
	AMQPHandler *m_handler;
	std::string m_name;

};


void AMQPHandler::onError(AMQP::TcpConnection *connection, const char *message)
{
	std::lock_guard<std::mutex> lck (count_mutex);
	const ConnectionInfo& info = static_cast<AMQPConnection*>(connection)->info();
	const std::string& name = static_cast<AMQPConnection*>(connection)->name();
	std::string zone = info["zone"];
	std::string uri = info.uri();

	int c1 = ++connection_error[uri];
	int c2 = ++connection_zone_error[zone];
	int c3 = ++connection_zone_error[zone + "::" + name];
	int c4 = ++connection_name_error[name];
	int c5 = ++connection_name_error[name + "::" + zone];

	AMQP_DBG << uri << " error (" << c1 << "/" << c2 << "/" << c3 << "/" << c4 << "/" << c5 << ") : " << message;

	kz_xavp_status_update(xavp_counters({{uri + "::error", c1}
	                                    ,{zone + "::error", c2}
	                                    ,{zone + "::" + name + "::error", c3}
	                                    ,{name + "::error", c4}
	                                    ,{name + "::" + zone + "::error", c5}
	                                    }));
	// log exceptions
	if(strcmp(message, "connection prematurely closed by client") &&
	   strcmp(message, "Channel is in error state")) {
			fire_amqp_event("connection", "error", name, uri, zone, std::string(message));
	}

	_error = true;
	LibEvHandler::onError(connection, message);
}

void AMQPHandler::onReady(AMQP::TcpConnection *connection)
{
	std::lock_guard<std::mutex> lck (count_mutex);
	const ConnectionInfo& info = static_cast<AMQPConnection*>(connection)->info();
	const std::string& name = static_cast<AMQPConnection*>(connection)->name();
	std::string zone = info["zone"];
	std::string uri = info.uri();

	int c1 = ++connection_count[uri];
	int c2 = ++connection_zone_count[zone];
	int c3 = ++connection_zone_count[zone + "::" + name];
	int c4 = ++connection_name_count[name];
	int c5 = ++connection_name_count[name + "::" + zone];

	kz_xavp_status_update(xavp_counters({{uri, c1}
	                                    ,{zone, c2}
	                                    ,{zone + "::" + name, c3}
	                                    ,{name, c4}
	                                    ,{name + "::" + zone, c5}
	                                    }));
	fire_amqp_event("connection", "open", name, uri, zone);

	if(c1 == 1) {
		fire_amqp_event("connection", "connection-available", name, uri, zone);
		AMQP_DBG << "amqp first connection available for uri " << uri;
	}
	if(c2 == 1) {
		fire_amqp_event("connection", "zone-available", name, uri, zone);
		AMQP_DBG << "amqp first connection available for zone available " << zone;
	}
	if(c3 == 1) {
		fire_amqp_event("connection", "zone-listener-available", name, uri, zone);
		AMQP_DBG << "amqp first connection available for listener " << name;
	}
	if(c4 == 1) {
		fire_amqp_event("connection", "listener-available", name, uri, zone);
		AMQP_DBG << "amqp first connection available for listener " << name;
	}
	if(c5 == 1) {
		fire_amqp_event("connection", "listener-zone-available", name, uri, zone);
		AMQP_DBG << "amqp first connection available for listener " << name << " in zone " << zone;
	}
	AMQP_DBG << "amqp connections in zone " << zone << " " << c1 << " , " << c2;
	_error = false;
	_connected = true;
	LibEvHandler::onReady(connection);
}

void AMQPHandler::onDetached(AMQP::TcpConnection *connection)
{
	std::lock_guard<std::mutex> lck (count_mutex);

	const ConnectionInfo& info = static_cast<AMQPConnection*>(connection)->info();
	const std::string& name = static_cast<AMQPConnection*>(connection)->name();
	std::string zone = info["zone"];
	std::string uri = info.uri();

	if(_connected) dec_connection(uri, zone, name);

	LibEvHandler::onDetached(connection);
}

uint16_t AMQPHandler::onNegotiate(AMQP::TcpConnection *connection, uint16_t interval)
{
	return LibEvHandler::onNegotiate(connection, dbk_use_heartbeats < 0 ? interval : dbk_use_heartbeats);
}

void AMQPHandler::onProperties(AMQP::TcpConnection *connection, const AMQP::Table &server, AMQP::Table &client)
{
	AMQP::Table capabilities;
    capabilities["authentication_failure_close"] = true;
    capabilities["basic.nack"] = true;
    capabilities["connection.blocked"] = true;
    capabilities["consumer_cancel_notify"] = true;
    capabilities["exchange_exchange_bindings"] = true;
    capabilities["publisher_confirms"] = true;

    const ConnectionInfo& cinfo = static_cast<AMQPConnection*>(connection)->info();
    AMQP::Table info(cinfo.properties());

    client["product"] = "2600Hz AMQP library";
    client["version"] = "1.0.0";
    client["platform"] = "kazoo";
    client["copyright"] = "Copyright 2019 2600Hz";
    client["information"] = "http://www.2600hz.com";
    client["capabilities"] = capabilities;
    client["info"] = info;

}

void AMQPHandler::dec_connection(std::string uri, std::string zone, std::string name)
{
	int c1 = --connection_count[uri];
	int c2 = --connection_zone_count[zone];
	int c3 = --connection_zone_count[zone + "::" + name];
	int c4 = --connection_name_count[name];
	int c5 = --connection_name_count[name + "::" + zone];

	kz_xavp_status_update(xavp_counters({{uri, c1}
	                                    ,{zone, c2}
	                                    ,{zone + "::" + name, c3}
	                                    ,{name, c4}
	                                    ,{name + "::" + zone, c5}
	                                    }));
	fire_amqp_event("connection", "closed", name, uri, zone);


	if(c1 == 0) {
		fire_amqp_event("connection", "connection-unavailable", name, uri, zone);
		AMQP_DBG << "amqp last connection in zone " << zone << " closed : " << uri;
	}
	if(c2 == 0) {
		fire_amqp_event("connection", "zone-unavailable", name, uri, zone);
		AMQP_DBG << "amqp zone " << zone << " is disconnected";
	}
	if(c3 == 0) {
		fire_amqp_event("connection", "zone-listener-unavailable", name, uri, zone);
		AMQP_DBG << "amqp zone " << zone << " is disconnected";
	}
	if(c4 == 0) {
		fire_amqp_event("connection", "listener-unavailable", name, uri, zone);
		AMQP_DBG << "amqp zone " << zone << " is disconnected";
	}
	if(c5 == 0) {
		fire_amqp_event("connection", "listener-zone-unavailable", name, uri, zone);
		AMQP_DBG << "amqp zone " << zone << " is disconnected";
	}
}

class AMQPMessage : public AMQP::MetaData
{
private:
	std::string _exchange;
	std::string _routingkey;
	std::string _body;
	std::string _consumerTag;
	uint64_t _deliveryTag;
	bool _redelivered = false;
	uint16_t _channel;
	pid_t  _thread_id;
	std::string _queue;
	bool _returned = false;
	uint16_t  _code = 200;
	std::string _reason = std::string("OK");

public:
	AMQPMessage(const AMQP::Message &message
	            ,const std::string& queue
	            ,std::string body
	            ,const std::string& consumerTag
	            ,uint64_t deliveryTag
	            ,bool redelivered
	            ,uint16_t channel
	            ) :
    	_exchange(message.exchange()),
    	_routingkey(message.routingkey()),
    	_body(std::move(body)),
    	_consumerTag(consumerTag),
    	_deliveryTag(deliveryTag),
    	_redelivered(redelivered),
    	_channel(channel),
    	_thread_id(kz_gettid()),
    	_queue(queue)
    {
    	set(message);
    }

	AMQPMessage(const AMQP::Message &message
	            ,std::string body
	            ,uint16_t code
	            ,const std::string &reason
	            ,uint16_t channel
	            ) :
       	_exchange(message.exchange()),
       	_routingkey(message.routingkey()),
    	_body(std::move(body)),
    	_deliveryTag(0),
    	_channel(channel),
    	_thread_id(kz_gettid()),
    	_queue(""),
    	_returned(true),
	    _code(code),
	    _reason(reason.c_str())
    {
    	set(message);
    }

	AMQPMessage(const AMQP::Envelope &message
	            ,std::string body
	            ,uint16_t code
	            ,const std::string &reason
	            ,std::string exchange
	            ,std::string rk
	            ,uint16_t channel
	            ) :
    	_exchange(exchange.c_str()),
    	_routingkey(rk.c_str()),
    	_body(std::move(body)),
    	_deliveryTag(0),
    	_channel(channel),
    	_thread_id(kz_gettid()),
    	_queue(""),
    	_returned(true),
	    _code(code),
	    _reason(reason.c_str())
    {
    	set(message);
    }

	AMQPMessage(const AMQPMessage &message) = delete;

    const char* body() const { return _body.c_str(); }
    const std::string& exchange() const { return _exchange; }
    const std::string& routingkey() const { return _routingkey; }
    const std::string& consumerTag() const { return _consumerTag; }
    const uint64_t deliveryTag() const { return _deliveryTag; }
    const bool redelivered() const { return _redelivered; }
    const bool was_returned() const { return _returned; }
    const uint16_t channel() const { return _channel; }
    const pid_t thread() const { return _thread_id; }
    const std::string& queue() const { return _queue; }
    const uint16_t code() const { return _code; }
    const std::string& reason() const { return _reason; }

    /**
     *  Destructor
     */
    virtual ~AMQPMessage() = default;

};

template<template <typename...> class MAP, class KEY, class VALUE>
std::vector<VALUE>
values(const MAP<KEY, VALUE>& map)
{
	std::vector<VALUE> result;
	result.reserve(map.size());
	for(const auto& it : map){
		result.emplace_back(it.second);
	}
	return result;
}

std::mutex delete_exchange_mutex;

typedef std::function<void(const AMQPMessage &message, uint64_t deliveryTag, bool redelivered)> consumer_callback;
typedef std::function<void(const AMQPMessage &message, uint16_t code, const std::string &description)> returned_callback;

class AMQPChannel : public AMQP::TcpChannel
{

#define deliver(M) \
        if(listener.consumer_spawn) { \
	        std::thread t(&AMQPChannel::consumer_callback_thread, this, callback, std::move(M)); \
	        t.detach(); \
        } else { \
	        callback(M); \
        }


public:
	AMQPChannel(AMQPConnection *_connection, kz::Listener& _listener, consumer_callback _callback, returned_callback _returned, bool _federated) :
		AMQP::TcpChannel(_connection),
		listener(_listener),
		connection(_connection),
		callback(_callback),
		returned(_returned),
		federated(_federated)
	{

		this->onReady([this] {
			declare_exchanges();
			publisher_confirms();
			start_queues();
		});

	}

	bool publish(json_ptr EnvelopePtr)
	{
		if(!EnvelopePtr) {
			AMQP_ERR << "no data to publish";
			return false;
		}

		json& Envelope = *EnvelopePtr;

		if(Envelope.find("payload") == Envelope.end()) {
			AMQP_ERR << "no payload to publish";
			return false;
		}

		std::string exchange = Envelope.find("exchange") != Envelope.end() ? Envelope.at("exchange").get<std::string>() : "";

		std::string routing = Envelope.find("routing-key") != Envelope.end() ? Envelope.at("routing-key").get<std::string>() : "";

		json payload = Envelope["payload"];

		bool IsTransaction = Envelope.find("is-async-call") != Envelope.end() ? true : false;
		bool IsSync = Envelope.find("is-sync-call") != Envelope.end() ? true : false;

		int flags = Envelope.find("publish-flags") != Envelope.end() ? Envelope.at("publish-flags").get<int>() : 0;

		bool hasServer = true;
		if(payload.find("Server-ID") == payload.end()) {
			if(!server_id.empty()) {
				payload["Server-ID"] = server_id;
			} else {
				hasServer = false;
			}
		}

		ensure_missing_headers(payload);

		std::stringstream s;
		s << payload;

		std::string body = s.str();


		AMQP::Envelope envelope(body.data(), body.length());
		envelope.setContentType("application/json");

		uint64_t timestamp = current_timestamp_in_micro();
		envelope.setTimestamp(timestamp);

		AMQP::Table arguments;
		arguments["server_uuid"] = server_uuid;

		if(routing.find("pid://") == 0) {
			std::regex regex("pid:\\/\\/([^\\/]+)\\/(.+)");
			std::smatch match;
			if (std::regex_search(routing, match, regex) && match.size() > 1) {
				std::string key(kz_deliver_to_pid.s, kz_deliver_to_pid.len);
				arguments[key] = match.str(1);
				routing = match.str(2);
			} else {
				AMQP_CRIT << "Really? pid::// changes here => " << routing;
			}
		}

		if(Envelope.find("arguments") != Envelope.end()) {
			json args = Envelope["arguments"];
			AMQP::from_json(args, arguments);
		}

		envelope.setHeaders(arguments);

		if(payload.find("Msg-ID") != payload.end()) {
			std::string msg_id = payload["Msg-ID"].get<std::string>();
			envelope.setMessageID(msg_id);
		}

		// callback that handles messages that could not be sent to a queue
		auto cb_return = [this](const AMQP::Message &message, uint16_t code, const std::string &description) {
	    	std::string body = std::string(message.body(), message.bodySize());
			AMQP_DBG  << "message returned : " << code << " " << description << " => " << body;

	    	kz_mt_update_stat(kz_stat_messages_returned, 1);

	    	AMQPMessage m(message, body, code, description, id());
	    	returned(m, code, description);

		};

		if((IsSync || IsTransaction) && !hasServer) {
			AMQP_DBG << "not ready for channel publish " << exchange << "(" << routing << ") " << payload["Event-Category"] << ":" << payload["Event-Name"] << " " << payload["Msg-ID"];
        	AMQPMessage m(envelope, body, 412, std::string("listener not ready"), exchange, routing, id());
        	returned(m, 412, "listener not ready");
		} else {
	        AMQP_DBG << "channel publish " << exchange << "(" << routing << ") " << payload["Event-Category"] << ":" << payload["Event-Name"] << " " << payload["Msg-ID"];
			AMQP::Channel::publish(exchange, routing, envelope, flags).onReturned(cb_return);
			if(IsSync) {
				kz_mt_update_stat(kz_stat_calls_published, 1);
			} else if(IsTransaction) {
				kz_mt_update_stat(kz_stat_transactions_published, 1);
			} else {
				kz_mt_update_stat(kz_stat_messages_published, 1);
			}

		}

		return true;
	}

protected:
	kz::Listener& listener;
	AMQPConnection *connection;
	std::string server_id = std::string();
	consumer_callback callback;
	returned_callback returned;
	bool federated;
	std::string _consumerTag;

	void declare_exchanges()
	{
		for(auto& e : listener.exchanges) { declare_exchange(e); };
		for(auto& q : listener.queues) { for(auto& b : kz::values(q.bindings.map())) { declare_exchange(b.exchange());}; };
	}

	void declare_exchange(const kz::Exchange& exchange)
	{
		declareExchange(exchange.name, exchange.amqp_type, exchange.options, exchange.arguments)
				.onSuccess([this, exchange] {
					AMQP_DBG << "exchange " << exchange.name << " declared as " << exchange.type;

					for(auto& exc : exchange.bindings.map()) {
						const kz::ExchangeBinding& binding = exc.second;
						declare_exchange(binding.exchange());
						for(auto& rk : binding.routing()) {
							std::string rkey = expand(rk, true);
							bindExchange(binding.exchange().name, exchange.name, rkey, binding.arguments())
								.onSuccess([exchange, binding, rkey] {
									AMQP_DBG << "exchange " << exchange.name << " bound to exchange " << binding.exchange().name << " with routing key " << rkey;
								})
								.onError([this, exchange, binding, rkey](const char *msg) {
									AMQP_ERR << "error binding exchange " << exchange.name << " to exchange " << binding.exchange().name << " with routing key " << rkey;
									connection->setError(msg);
								});
						}
					}
				})
				.onError([this, exchange](const char *msg) {
					AMQP_ERR << "error declaring exchange " << exchange.name << " - " << msg;
					if(exchange.delete_on_error) {
				        std::thread t(&AMQPChannel::deleteExchange, this, connection->name(), connection->info(), exchange);
				        t.detach();
					}
					connection->setError(msg);
				});
	}

	void deleteExchange(const std::string& name, const ConnectionInfo& info, const kz::Exchange& exchange)
	{
		if (delete_exchange_mutex.try_lock()) {
			AMQP_DBG << "trying to remove exchange " << exchange.name;
			auto *loop = ev_loop_new(0);
			AMQPHandler handler(loop);
			AMQPConnection c(&handler, info, name);
			AMQP::TcpChannel ch(&c);
			ch.removeExchange(exchange.name, AMQP::ifunused)
					.onSuccess([exchange] {
						AMQP_DBG << "exchange removed - " << exchange.name;
					})
					.onError([exchange](const char *msg) {
						AMQP_DBG << "error removing exchange - " << exchange.name << " - "  << msg;
					})
					.onFinalize([&c, exchange] {
						AMQP_DBG << "final to remove exchange " << exchange.name;
						c.close(true);
					});
			 ev_run(loop, 0);
			 ev_loop_destroy(loop);
			 std::chrono::milliseconds wait = std::chrono::milliseconds(3000);
			 std::this_thread::sleep_for(wait);
			 AMQP_DBG << "finished remove exchange " << exchange.name;
			 delete_exchange_mutex.unlock();
		}
	}

	void start_queues()
	{
		for(auto& e : listener.queues) { start_queue(e); };
	}


	bool HasFederatedBindings(const kz::Queue& queue)
	{
		for(auto& qb : queue.bindings.map()) {
			auto& binding = qb.second;
			if(binding.IsFederated()) {
				return true;
			}
		}
		return false;
	}

	void start_queue(const kz::Queue& queue)
	{
		if((!federated) || HasFederatedBindings(queue)) {
			std::string name = expand(queue.name);
			AMQP_DBG << "queue '" << name << "' being declared with flags " << queue.options;
			declareQueue(name, queue.options, queue.arguments)
				.onSuccess([this, queue](const std::string &name, uint32_t messagecount, uint32_t consumercount) {
					AMQP_DBG << "queue " << name << " declared";
					bind_queue(queue, name);
					start_consumer(queue, name);
			 	 })
				.onError([this, name](const char *msg) {
					AMQP_ERR << "error declaring queue '" << name << "' - " << msg;
					connection->setError(msg);
				});
		}
	}

	void bind_queue(const kz::Queue& queue, const std::string &name)
	{
		AMQP_DBG << "binding queue " << name;
		for(auto& qb : queue.bindings.map()) {
			auto& binding = qb.second;
			if((!federated) || binding.IsFederated()) {
				if(binding.exchange().amqp_type == AMQP::ExchangeType::direct) {
					server_id = name;
				}
				for(auto& rk : binding.routing()) {
					std::string routing = expand(rk, true);
					if(routing.empty() && binding.exchange().amqp_type == AMQP::ExchangeType::direct) {
						routing = server_id;
					}
					bindQueue(binding.exchange().name, name, routing, binding.arguments())
					    .onSuccess([queue, name, binding, routing] {
						    AMQP_DBG << "queue " << name << " bound to exchange " << binding.exchange().name << " with routing key " << routing;
					    })
						.onError([this, name, binding, routing](const char *msg) {
							AMQP_ERR << "error binding queue " << name << " to exchange " << binding.exchange().name << " with routing key " << routing;
							connection->setError(msg);
						});
				}
			}
		}
	}

	void start_consumer(const kz::Queue& queue, const std::string &name)
	{
		AMQP_DBG << "starting consumer for " << name << " with flags " << queue.consume.options;
	    auto startCb = [name, this](const std::string &consumertag) {
	    	_consumerTag = consumertag;
	       AMQP_DBG << "consumer started in queue " << name << " with tag " << consumertag;
	    };

	    // callback function that is called when the consume operation failed
	    auto errorCb = [name, this](const char *msg) {
	    	AMQP_DBG << "error starting consumer for queue " << name << " - " << msg;
			connection->setError(msg);
	    };

	    // callback operation when a message was received
	    auto messageCb = [this, name, queue](const AMQP::Message &message, /*const std::string &consumerTag,*/ uint64_t deliveryTag, bool redelivered) {

			AMQP_DBG << "received message with tag " << deliveryTag << " - " << message.exchange() << " - " << message.routingkey();
			AMQP_TRACE << "received message with tag " << deliveryTag << " : " << std::string(message.body(), message.bodySize());
	    	kz_mt_update_stat(kz_stat_messages_received, 1);

	        // acknowledge the message
	        if(!(queue.consume.options & kz::noack)) {
	        	ack(deliveryTag);
	        }

	        std::string body(message.body(), message.bodySize());
        	AMQPMessage m(message
        	            ,name
        	            ,body
        	            ,_consumerTag
        	            ,deliveryTag
        	            ,redelivered
        	            ,id());
	        callback(m, deliveryTag, redelivered);

	    };

	    if(queue.consume.qos.prefetch_count > 0) {
	        setQos(queue.consume.qos.prefetch_count, queue.consume.qos.is_shared);
	    }

	    consume(name, queue.consume.options, queue.consume.arguments)
			.onConsuming(startCb)
			.onMessage(messageCb)
			.onError(errorCb);
	}

	void consumer_callback_thread(consumer_callback callback, const AMQPMessage &msg, uint64_t deliveryTag, bool redelivered)
	{
		callback(msg, deliveryTag, redelivered);
	}

	std::string encode(const std::string& to_encode)
	{
		char routing_key_buff[MAX_ROUTING_KEY_SIZE+1];
		memset(routing_key_buff,0, sizeof(routing_key_buff));
		kz_amqp_util_encode_string(to_encode.c_str(), routing_key_buff);
		return std::string(routing_key_buff);
	}

	std::string expand(const std::string& to_expand, bool should_encode = false)
	{
		std::regex regex("(\\$\\{([^\\}]*)\\})");

		AMQP::Table t;
		t["Node"] = std::string(dbk_node_name.s, dbk_node_name.len);
		t["Process"] = strformat("%i", process_no);
		t["Channel-ID"] = strformat("%i", id());
		t["Thread-ID"] = strformat("%i", kz_gettid());
		t["Process-PID"] = strformat("%i", my_pid());
		t["Listener-Name"] = listener.name;
		t["HostName"] = std::string(dbk_node_hostname.s, dbk_node_hostname.len);

		auto run = [t, this, should_encode](const std::smatch& m) {
			std::ostringstream s;
			AMQP::owstream ows(s.rdbuf());
			ows << quotes::noquotes << (should_encode ? encode(t[m.str(2)]) : t[m.str(2)]) ;
		    return s.str();
		};

		return q::regex_replace(to_expand, regex, run);
	}

	void publisher_confirms()
	{
        if(listener.publisher_confirms) {
        	confirmSelect().onSuccess([&]() {
        	}).onAck([&](uint64_t deliveryTag, bool multiple) {
				AMQP_DBG << "ack received publishing" << deliveryTag;

        	}).onNack([&](uint64_t deliveryTag, bool multiple, bool requeue) {
				AMQP_WARN << "nack received publishing" << deliveryTag;
        	});
        }
	}

	void ensure_missing_headers(json& payload)
	{
		if(payload.find("App-Name") == payload.end()) {
			payload["App-Name"] = "kamailio";
		}

		if(payload.find("App-Version") == payload.end()) {
			payload["App-Version"] = VERSION;
		}

		if(payload.find("Node") == payload.end()) {
			payload["Node"] = node();
		}

		if(payload.find("Msg-ID") == payload.end()) {
			payload["Msg-ID"] = uuids::to_string(uuids::uuid_system_generator{}());
		}

	}

};


class AMQPWorker : public kz::PipeThread<json_ptr>
{
public:
	AMQPWorker(ConnectionInfoList _connections, kz::Listener& _listener, consumer_callback _callback, returned_callback _returned, std::string _zone, bool _federated) :
		kz::PipeThread<json_ptr>(),
		connections(_connections),
		listener(_listener),
		callback(_callback),
		returned(_returned),
		zone(_zone),
		federated(_federated)
		{
		};

	virtual void start()
	{
		this->m_thread = std::thread(&AMQPWorker::run_thread, this);
	}


protected:
	ConnectionInfoList connections;
	kz::Listener& listener;
	std::vector<AMQPChannel*> channels;
	int cur_channel = 0;
	consumer_callback callback;
	returned_callback returned;
	std::string zone;
	bool federated;

	virtual void OnData(json_ptr Evt)
	{

		json& jobj = *Evt;

		int idx = cur_channel;
		if(jobj.find("AMQP-Hash") != jobj.end()) {
			std::string hash = jobj["AMQP-Hash"].get<std::string>();
			idx = kz_core_hash(hash.data(), hash.length(), channels.size());
		} else {
			cur_channel++;
			if(cur_channel >= channels.size()) {
				cur_channel = 0;
			}
		}
		auto channel = channels[idx];

		AMQP_DBG << "publishing to " << jobj["exchange"] << " - " << jobj["routing-key"];

		channel->publish(Evt);
		delete Evt;
	}

	void run_thread()
	{
		auto idx = 0;
		AMQP_DBG << "starting worker thread " << kz_gettid() << " on " << my_pid();
		while (true) {
			AMQPHandler handler(raw_loop);
			const kz::ConnectionInfo& info = connections[idx];
			AMQPConnection c(&handler, info, listener.name);

			AMQP_DBG << "starting worker loop with " << listener.num_channels << " channel in thread " << kz_gettid() << " on " << my_pid();

			for (auto i = 0; i < listener.num_channels; i++) {
				auto channel = new AMQPChannel(&c, listener, callback, returned, federated);
				channels.push_back(channel);
			};

			run_loop();

			for (auto &iter : channels) {
				if (iter->connected())
					iter->close();
				delete iter;
			}
			channels.clear();

			if(!c.closed()) c.close();

			if (!handler.hasError() || was_stopped()) {
				break;
			}

			std::chrono::milliseconds wait = std::chrono::milliseconds(3000);
			// TODO
			// exponential backoff
			idx++;
			if(idx >= connections.size()) {
				if(idx == 1) {
					// we only have one connection

				} else {
					// we have 1+ connections

				}
				idx = 0;
			} else {

			}
			AMQP_DBG << "reconnecting to " << info.uri() << " in " << wait.count() << " ms";
			std::ostringstream os;
			os << "reconnecting in " << wait.count() << " ms";
			std::string msg = os.str();
			fire_amqp_event("connection", "message", listener.name, info.uri(), zone , msg);
			std::this_thread::sleep_for(wait);
		}
	}



};


class AMQPListener : public kz::PipeThread<json_ptr>
{
	typedef std::vector<int> num_list;
	typedef std::map<std::string, num_list> zonemap;

public:
	AMQPListener(kz::Listener& listener) :
		kz::PipeThread<json_ptr>(),
		_listener(listener),
		consumer_idx(0)
		{
		};

	kz::Listener& listener() const { return _listener;};

protected:
	uint worker_idx = 0;
	std::vector<AMQPWorker*> workers;
	zonemap zones;
	kz::Listener& _listener;
	std::atomic<uint> consumer_idx;

	virtual void OnStart()
	{
		AMQP_DBG << "starting listener " << _listener.name << " - " << local_zone;
		auto worker_id = 0;
		AMQP_DBG << "starting " << _listener.num_workers << " workers for listener " << _listener.name << " - " << local_zone;
		for (auto i = 0; i < _listener.num_workers; i++) {

			AMQP_DBG << "starting worker " << worker_id << " for listener " << _listener.name << " in zone " << local_zone;

			auto cb = [this, worker_id](const AMQPMessage &message, uint64_t deliveryTag, bool redelivered) {
				OnMessage(message, worker_id, local_zone, deliveryTag, redelivered);
			};

			auto ret = [this, worker_id](const AMQPMessage &message, uint16_t code, const std::string &description) {
				OnReturned(message, worker_id, local_zone, code, description);
			};

			auto worker = new AMQPWorker(primary_zone, _listener, cb, ret, local_zone, false);
			worker->start();
			workers.push_back(worker);
			zones[local_zone].push_back(worker_id);
			worker_id++;
		}

		/* federation */
		bool hasFederation = false;
		for(auto& q : _listener.queues) { for(auto& b : kz::values(q.bindings.map())) { if(b.IsFederated()) hasFederation = true;}; };
		if(hasFederation) {
			for(auto& f : federated) {
				std::string federatedZone = f.first;
				AMQP_DBG << "FEDERATED ZONE - " << _listener.name << " - " << federatedZone;
				for (auto i = 0; i < _listener.num_federated_workers; i++) {

					AMQP_DBG << "starting federated worker " << worker_id << " for listener " << _listener.name << " in zone " << federatedZone;

					auto cb = [this, worker_id, federatedZone](const AMQPMessage &message, uint64_t deliveryTag, bool redelivered) {
						OnMessage(message, worker_id, federatedZone, deliveryTag, redelivered);
					};

					auto ret = [this, worker_id, federatedZone](const AMQPMessage &message, uint16_t code, const std::string &description) {
						OnReturned(message, worker_id, federatedZone, code, description);
					};


					auto worker = new AMQPWorker(f.second, _listener, cb, ret, federatedZone, true);
					worker->start();
					workers.push_back(worker);
					zones[federatedZone].push_back(worker_id);
					worker_id++;
				}
			}
		}

	}


	virtual void OnStop()
	{
		for (auto &iter : workers) {
			iter->stop();
			delete iter;
		}

		workers.clear();
		zones.clear();

	}

	std::string kz_call_id(const json& jobj)
	{
		std::string str = "";
		std::string sep = "";
		if(jobj.find("Call-ID") != jobj.end()) {
			str = jobj["Call-ID"].get<std::string>();
			sep = "|";
		}

		if(jobj.find("Msg-ID") != jobj.end()) {
			str.append(sep);
			str.append(jobj["Msg-ID"].get<std::string>());
		}

		return str;

	}

	virtual void OnData(json_ptr Evt)
	{
		json& jobj = *Evt;
		int idx = -1;
		int worker_id = -1;

		auto zone_str = std::string(dbk_primary_zone_name.s, dbk_primary_zone_name.len);

		if (jobj.find("AMQP-Worker-ID") != jobj.end()) {
	        worker_id = jobj.at("AMQP-Worker-ID").get<int>();
	        idx = 0;
		}

		if(idx == -1) {
			if (jobj.find("AMQP-Worker-Zone") != jobj.end()) {
				zone_str = jobj.at("AMQP-Worker-Zone").get<std::string>();
			}
		}

		auto & sel = zones[zone_str];

		if(idx == -1) {
			if(sel.size() < 1) {
				AMQP_WARN << "no workers for zone " << zone_str;
			} else {
				if (jobj.find("AMQP-Hash") != jobj.end()) {
					std::string hash = jobj.at("AMQP-Hash").get<std::string>();
					idx = kz_core_hash(hash.c_str(), (int)hash.size(), sel.size());
				} else {
					idx = worker_idx++;
					if (worker_idx >= sel.size())
						worker_idx = 0;
				}
			}
		}

		if(idx >= 0) {
			if(worker_id == -1) {
				worker_id = sel[idx];
			}
			AMQP_DBG << "sending to worker " << jobj["exchange"] << " - " << jobj["routing-key"];

			if(!workers[worker_id]->receive(Evt)) {
				AMQP_ERR << "error forwarding amqp payload to zone " << zone_str << " worker " << worker_id;
				delete Evt;
			}
		} else {
			delete Evt;
		}
	}

	void OnMessage(const AMQPMessage &message, int worker_id, const std::string& zone, uint64_t deliveryTag, bool redelivered)
	{
		const AMQP::Table& props = message.headers();
		const std::string& sender_uuid = props["server_uuid"];
		bool has_server_uuid = !sender_uuid.empty();

		const std::string& relay_uuid = props["relay-id"];
		bool is_relay = message.exchange() == "relay"; // !relay_uuid.empty();

		/*
		 * verify if we received message sent by us
		 */
		if((!_listener.deliver_local) && has_server_uuid && sender_uuid == server_uuid) {
			kz_mt_update_stat(kz_stat_messages_received_self, 1);
			AMQP_DBG << "dropping message published by this server";
			return;
		}

		json jobj;
		try
		{
			jobj = json::parse(message.body());
		}
		catch(const std::exception& exc)
		{
			AMQP_ERR << "error parsing body - " << exc.what() << " => " << message.body();
			return;
		}

		jobj["AMQP-Broker-Zone"] = zone;
		jobj["AMQP-Worker-ID"] = worker_id;

		uint64_t received = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		jobj["AMQP-Received"] = received;

		if(message.hasTimestamp()) {
			uint64_t ts = message.timestamp();
			int64_t elapsed = received - ts;
			jobj["AMQP-Timestamp"] = ts;
			jobj["AMQP-Timestamp-Original"] = message.timestamp();
			if(elapsed <= 0) {
				if(kz_enable_clock_diff_warning) {
					AMQP_NOTICE << "clocks between this server and " << jobj["Node"].get<std::string>() << " are out of sync at least by " << std::abs(elapsed) << " μs";
				}
				jobj["AMQP-Elapsed-Micro"] = 0;
			} else {
				jobj["AMQP-Elapsed-Micro"] = received - ts;
			}
		}

		if(jobj.find("Server-ID") != jobj.end()) {
			std::string server_id = jobj["Server-ID"].get<std::string>();
			std::ostringstream stringStream;
			stringStream << "consumer://"
					<< _listener.name << "/"
					<< worker_id << "/"
					<< server_id;
			jobj["Server-ID"] = stringStream.str();
		}

		std::string msg_id = message.messageID();
		if(msg_id.empty()) {
			if(jobj.find("Msg-ID") != jobj.end()) {
				msg_id = jobj["Msg-ID"].get<std::string>();
			}
		}

		if(is_relay) {
			handle_relay(jobj, relay_uuid);
		} else if(msg_id.find("tm://") == 0) {
			handle_transaction_response(jobj, msg_id, 200);
		} else if(msg_id.find("sync://") == 0) {
			handle_call_response(jobj, msg_id);
		} else {
			handle_event(jobj, deliveryTag, message.routingkey());
		}

	}

	void OnReturned(const AMQPMessage &message, int worker_id, const std::string& zone, uint16_t code, const std::string &description)
	{
		json jobj;
		try
		{
			jobj = json::parse(message.body());
		}
		catch(const std::exception& exc)
		{
			AMQP_ERR << "error parsing body - " << exc.what() << " => " << message.body();
			return;
		}

		jobj["AMQP-Broker-Zone"] = zone;
		jobj["AMQP-Worker-ID"] = worker_id;

		uint64_t received = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		jobj["AMQP-Received"] = received;

		if(message.hasTimestamp()) {
			uint64_t ts = message.timestamp();
			int64_t elapsed = received - ts;
			jobj["AMQP-Timestamp"] = ts;
			if(elapsed <= 0) {
				if(kz_enable_clock_diff_warning) {
					AMQP_NOTICE << "clocks between this server and " << jobj["Node"].get<std::string>() << " are out of sync at least by " << std::abs(elapsed) << " μs";
				}
				jobj["AMQP-Elapsed-Micro"] = 0;
			} else {
				jobj["AMQP-Elapsed-Micro"] = received - ts;
			}
		}

		std::string msg_id = message.messageID();
		if(msg_id.empty()) {
			if(jobj.find("Msg-ID") != jobj.end()) {
				msg_id = jobj["Msg-ID"].get<std::string>();
			}
		}

		jobj["Original-Event-Category"] = jobj["Event-Category"];
		jobj["Original-Event-Name"] = jobj["Event-Name"];
		jobj["Event-Category"] = "amqp_error";
		jobj["Event-Name"] = "message_returned";
		jobj["Error-Code"] = code;
		jobj["Error-Reason"] = description;

		if(msg_id.find("tm://") == 0) {
			handle_transaction_response(jobj, msg_id, code, true);
		} else if(msg_id.find("sync://") == 0) {
			handle_call_response(jobj, msg_id, true);
		} else if(msg_id.find("relay://") == 0) {
			handle_relay(jobj, msg_id, true);
		} else {
			handle_event(jobj, message.deliveryTag(), message.routingkey(), true);
		}

	}

	int GetWorkerIndex(const std::string& routingkey, const json& jobj)
	{
		/*
		 * find the consumer worker
		 */
		int idx = consumer_idx;
		if(idx >= sip_workers.size()) {
			idx = 0;
		}

		if(_listener.consistent_worker) {
			std::string consistent;
			if(_listener.consistent_worker_key.empty()) {
				consistent = routingkey;
			} else {
				if(jobj.find(_listener.consistent_worker_key) != jobj.end()) {
					consistent = jobj[_listener.consistent_worker_key].get<std::string>();
				} else {
					consistent = routingkey;
				}
			}
			AMQP_DBG << "listener has consistent-worker : " << _listener.consistent_worker_key << " : " << consistent;
			idx = kz_core_hash(consistent.c_str(), consistent.length(), sip_workers.size());
		} else {
			consumer_idx++;
			if(consumer_idx >= sip_workers.size()) {
				consumer_idx = 0;
			}
		}
		return idx;
	}

	int GetNextWorkerIndex()
	{
		int idx = consumer_idx;
		if(idx >= sip_workers.size()) {
			idx = 0;
		}

		consumer_idx++;
		if(consumer_idx >= sip_workers.size()) {
			consumer_idx = 0;
		}
		return idx;
	}

	void handle_event(const json& jobj, const uint64_t deliveryTag, const std::string& routingKey, bool returned = false)
	{
		if(!returned) kz_mt_update_stat(kz_stat_event_received, 1);
		kz_amqp_consumer_delivery_ptr ptr = kz_amqp_alloc_consumer_delivery();
		if(ptr == NULL) {
			AMQP_ERR << "error allocating shared memory for consumer delivery : " << jobj;
			return;
		}

		std::string event_category;
		std::string event_name;

		ptr->type = KZ_AMQP_EVENT;
		ptr->no_stat = returned;
		ptr->delivery_tag = deliveryTag;
		ptr->routing_key = kz_amqp_string_dup((char*)routingKey.c_str());
		if(jobj.find("Event-Category") != jobj.end()) {
			event_category = jobj["Event-Category"].get<std::string>();
			ptr->event_category = kz_amqp_string_dup((char*)event_category.c_str());
		}
		if(jobj.find("Event-Name") != jobj.end()) {
			event_name = jobj["Event-Name"].get<std::string>();
			ptr->event_name = kz_amqp_string_dup((char*)event_name.c_str());
		}

		std::string ci = kz_call_id(jobj);
		ptr->ci = kz_amqp_string_dup((char*)ci.c_str());


		std::ostringstream jobjStream;
		jobjStream << jobj;
		std::string str_payload = jobjStream.str();
		ptr->payload = kz_amqp_string_dup((char*)str_payload.c_str());

		int idx = GetWorkerIndex(routingKey, jobj);
		AMQP_DBG << "sending to sip worker " << idx << " : " << routingKey << " : " << event_category << " : " << event_name;
		AMQP_TRACE << "sending to sip worker " << idx << " : " << routingKey << " : " << event_category << " : " << event_name << " : " << jobj;
		sip_workers[idx]->receive(ptr);

	}

	void handle_call_response(const json& jobj, const std::string& msg_id, bool returned = false)
	{
		if(returned) {
			kz_mt_update_stat(kz_stat_calls_returned, 1);
		} else {
			kz_mt_update_stat(kz_stat_calls_received, 1);
		}

		kz_amqp_cmd_ptr cmd = NULL;
        sync_calls_mutex.lock();
        auto search = sync_calls.find(msg_id.substr(7));
        if (search != sync_calls.end()) {
        	cmd = search->second;
        	sync_calls.erase(search);
        }
        sync_calls_mutex.unlock();

        if(cmd != NULL) {
    		std::ostringstream jobjStream;
    		jobjStream << jobj;
    		std::string str_payload = jobjStream.str();
    		cmd->return_payload = kz_amqp_string_dup((char*)str_payload.c_str());
    		cmd->return_code = cmd->return_payload == NULL || returned ? 500 : 200;
			kz_mt_update_stat(kz_stat_calls_delivered, 1);
			std::string ci = kz_call_id(jobj);
			cmd->ci = kz_amqp_string_dup((char*)ci.c_str());

    		lock_release(&cmd->lock);
        }
	}

	void handle_transaction_response(const json& jobj, const std::string& msg_id, uint16_t code, bool returned = false)
	{
		if(returned) {
			kz_mt_update_stat(kz_stat_transactions_returned, 1);
		} else {
			kz_mt_update_stat(kz_stat_transactions_received, 1);
		}

		kz_amqp_consumer_delivery_ptr ptr = kz_amqp_alloc_consumer_delivery();
		if(ptr == NULL) {
			AMQP_ERR << "error allocating shared memory for consumer delivery : " << jobj;
			return;
		}
		ptr->no_stat = returned;
		ptr->type = KZ_AMQP_TRANSACTION;

		std::string ci = kz_call_id(jobj);
		ptr->ci = kz_amqp_string_dup((char*)ci.c_str());

		char id[37];
		sscanf(msg_id.c_str(),"tm://%i:%i/%i:%i/%36s", &ptr->t_hash, &ptr->t_label, &ptr->t_reply_route, &ptr->t_failure_route, id);
		ptr->message_id = kz_amqp_string_dup(id);

		std::ostringstream jobjStream;
		jobjStream << jobj;
		std::string str_payload = jobjStream.str();
		ptr->payload = kz_amqp_string_dup((char*)str_payload.c_str());
		ptr->t_code = code;

		int idx = GetNextWorkerIndex();

		AMQP_DBG << "delivering transaction reply " << ptr->t_hash <<":" << ptr->t_label << " to sip worker " << idx;
		sip_workers[idx]->receive(ptr);

	}

	void handle_relay(const json& jobj, const std::string& msg_id, bool returned = false)
	{
		if(returned) {
			kz_mt_update_stat(kz_stat_relay_returned, 1);
		} else {
			kz_mt_update_stat(kz_stat_relay_received, 1);
		}

		if(jobj.find("Payload") == jobj.end()) {
			AMQP_ERR << "relay message without Payload : " << jobj;
			return;
		}

		kz_amqp_consumer_delivery_ptr ptr = kz_amqp_alloc_consumer_delivery();
		if(ptr == NULL) {
			AMQP_ERR << "error allocating shared memory for consumer delivery : " << jobj;
			return;
		}
		ptr->no_stat = returned;
		ptr->type = KZ_AMQP_RELAY;

		std::string ci = kz_call_id(jobj);
		ptr->ci = kz_amqp_string_dup((char*)ci.c_str());

		char id[37];
		sscanf(msg_id.c_str(),"%i/%36s", &ptr->t_hash, id);

		json payload = jobj["Payload"];

		std::ostringstream jobjStream;
		jobjStream << payload ;

		std::string str_payload = jobjStream.str();
		ptr->payload = kz_amqp_string_dup((char*)str_payload.c_str());

		int idx = GetNextWorkerIndex();

		AMQP_DBG << "delivering relay message " << ptr->t_hash << " to sip worker " << idx;
		sip_workers[idx]->receive(ptr);
	}

};


class Manager : public kz::PipeWorker<kz_amqp_cmd_ptr>
{
public:
	Manager(int cmd_pipes[2]) :
		kz::PipeWorker<kz_amqp_cmd_ptr>(cmd_pipes)
		{
		};

protected:
	std::map<std::string,AMQPListener*> amqp_listeners;
	std::map<int,std::string> relay_ids;
	json last_nodes_payload;
	bool has_last_nodes_payload;

	virtual void OnTerminate(int signal)
	{
		using namespace std::chrono;
		using namespace date;


		if (!has_last_nodes_payload) return;

		has_last_nodes_payload = false;

		json payload = last_nodes_payload;
		payload["Expires"] = 0;

		auto envelope = json::object();

		envelope["exchange"] = "nodes";
		envelope["routing-key"] = node();

		envelope["payload"] = payload;

		auto jobj = new json(envelope);

		AMQP_INFO << "sending last nodes to publish ";

		std::string listener = "default";
		AMQPListener* l = amqp_listeners[listener];
		l->receive(jobj);

		std::chrono::milliseconds wait = std::chrono::milliseconds(1500);
		std::this_thread::sleep_for(wait);

	}

	virtual void OnStart()
	{

		AMQP_DBG << "starting listeners";
		for(auto& l : listeners) {
			AMQPListener *listener = new AMQPListener(l.second);
			listener->start();
			amqp_listeners[l.first] = listener;
		}
		AMQP_DBG << "listeners started";

	};

	virtual void OnStop()
	{

		AMQP_INFO << "stopping listeners";
		for(auto& l : amqp_listeners) {
			l.second->stop();
		}
		AMQP_INFO << "listeners stopped";

		AMQP_INFO << "handle pending reads";
		kz_amqp_cmd_ptr cmd = NULL;
		while ((cmd = Read()) != NULL) {
			cmd->return_code = -1;
			lock_release(&cmd->lock);
		}
		AMQP_INFO << "done handle pending reads";
	};

	void handle_publish(kz_amqp_cmd_ptr cmd)
	{
		json payload;
		try
		{
			payload = json::parse(cmd->payload);
		}
		catch(const std::exception& exc)
		{
			AMQP_ERR << "error parsing body - " << exc.what() << " => " << cmd->payload;
			cmd->return_code = -1;
			lock_release(&cmd->lock);
			return;
		}

		auto envelope = json::object();
		std::string listener = "default";
		std::string routing_key = cmd->routing_key;
		std::string exchange = cmd->exchange;

		if (exchange.compare("nodes") == 0) {
			last_nodes_payload = payload;
			has_last_nodes_payload = true;
		}

		envelope["exchange"] = cmd->exchange;
		envelope["publish-flags"] = cmd->flags;

		if(routing_key.find("consumer://") == 0) {
			std::regex regex("consumer:\\/\\/([^\\/]+)\\/(\\d+)\\/(.+)");
			std::smatch match;
			if (std::regex_search(routing_key, match, regex) && match.size() > 1) {
				listener = match.str(1);
				envelope["AMQP-Worker-ID"] = atoi(match.str(2).c_str());
				routing_key = match.str(3);
			} else {
				AMQP_CRIT << "Really? consumer::// changes here => " << routing_key;
			}
		}

		envelope["routing-key"] = routing_key;
		envelope["payload"] = payload;
		if(payload.find("Msg-ID") != payload.end()) {
			envelope["AMQP-Hash"] = payload["Msg-ID"];
		}

		if (cmd->arguments != NULL) {
			try
			{
				envelope["arguments"] = json::parse(cmd->arguments);
			}
			catch(const std::exception& exc)
			{
				AMQP_ERR << "error parsing arguments - " << exc.what() << " => " << cmd->arguments;
				cmd->return_code = -1;
				lock_release(&cmd->lock);
				return;
			}
		}

		auto jobj = new json(envelope);

		AMQP_DBG << "sending to publish " << cmd->exchange << " - " << cmd->routing_key;

		AMQPListener* l = amqp_listeners[listener];
		l->receive(jobj);

		cmd->return_code = 1;
		lock_release(&cmd->lock);

	}

	void handle_call(kz_amqp_cmd_ptr cmd)
	{

		uuids::uuid id = uuids::uuid_system_generator{}();

		json payload;
		try
		{
			payload = json::parse(cmd->payload);
		}
		catch(const std::exception& exc)
		{
			AMQP_ERR << "error parsing body - " << exc.what() << " => " << cmd->payload;
			return;
		}

		std::string msg_id = uuids::to_string(id);

		sync_calls_mutex.lock();
		sync_calls[msg_id] = cmd;
		sync_calls_mutex.unlock();

		std::ostringstream stringStream;
		stringStream << "sync://" << msg_id;
		payload["Msg-ID"] = stringStream.str();

		auto envelope = json::object();
		envelope["exchange"] = cmd->exchange;
		envelope["routing-key"] = cmd->routing_key;
		envelope["publish-flags"] = cmd->flags;
		envelope["payload"] = payload;
		envelope["is-sync-call"] = true;

		auto jobj = new json(envelope);

		AMQP_TRACE << "CALLING: " << envelope;

		AMQPListener* l = amqp_listeners["default"];
		l->receive(jobj);

		std::chrono::milliseconds timeout( (cmd->timeout.tv_sec * 1000) + (cmd->timeout.tv_usec / 1000));
		std::thread t([msg_id, timeout]() {
		        std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
		        sync_calls_mutex.lock();
		        auto search = sync_calls.find(msg_id);
		        if (search != sync_calls.end()) {
		        	kz_amqp_cmd_ptr cmd = search->second;
		        	cmd->return_code = -1;
		        	lock_release(&cmd->lock);
		        	sync_calls.erase(search);
		        }
		        sync_calls_mutex.unlock();
		});
		t.detach();

	}

	void handle_async_call(kz_amqp_cmd_ptr cmd)
	{
		uuids::uuid id = uuids::uuid_system_generator{}();
		AMQP_DBG << "received async call : " << uuids::to_string(id) << " " << cmd->exchange;
		AMQP_TRACE << "received async call : " << uuids::to_string(id) << " " << cmd->exchange << " - " << cmd->routing_key << " - " << cmd->payload;

		json payload;
		try
		{
			payload = json::parse(cmd->payload);
		}
		catch(const std::exception& exc)
		{
			AMQP_ERR << "error parsing body - " << exc.what() << " => " << cmd->payload;
			kz_amqp_free_pipe_cmd(cmd);
			return;
		}

		std::ostringstream stringStream;
		stringStream << "tm://"
				<< cmd->t_hash << ":" << cmd->t_label << "/"
				<< cmd->t_reply_route << ":" << cmd->t_failure_route << "/"
				<< uuids::to_string(id);
		payload["Msg-ID"] = stringStream.str();

		auto envelope = json::object();
		envelope["exchange"] = cmd->exchange;
		envelope["routing-key"] = cmd->routing_key;
		envelope["publish-flags"] = cmd->flags;
		envelope["payload"] = payload;
		envelope["is-async-call"] = true;


		auto jobj = new json(envelope);

		AMQPListener* l = amqp_listeners["default"];
		l->receive(jobj);

		kz_amqp_free_pipe_cmd(cmd);
	}

	void handle_relay(kz_amqp_cmd_ptr cmd)
	{
		if(amqp_listeners.find("relay") == amqp_listeners.end()) {
			return;
		}

		json payload;
		try
		{
			payload = json::parse(cmd->payload);
		}
		catch(const std::exception& exc)
		{
			AMQP_ERR << "error parsing body - " << exc.what() << " => " << cmd->payload;
			cmd->return_code = -1;
			lock_release(&cmd->lock);
			return;
		}

		if (relay_ids.find(cmd->relay_connection_id) == relay_ids.end()) {
			relay_ids[cmd->relay_connection_id] = uuids::to_string(uuids::uuid_system_generator{}());
		}
		std::string relay_id = strformat("%i/%s", cmd->relay_connection_id, relay_ids[cmd->relay_connection_id].c_str());

		auto envelope = json::object();
		auto arguments = json::object();

		payload["Event-Category"] = "relay";
		payload["Event-Name"] = "message";

		envelope["exchange"] = "relay";
		envelope["routing-key"] = "c-beam";
		if (payload.find("request_id") != payload.end()) {
			AMQP_DBG << "adding request_id - " << payload["request_id"];
			payload["Msg-ID"] = payload["request_id"].get<std::string>();
		}
		envelope["payload"] = payload;

 
		arguments["relay-id"] = relay_id;

		envelope["arguments"] = arguments;
		envelope["AMQP-Hash"] = relay_id;

		auto jobj = new json(envelope);

		AMQP_DBG << "sending relay to publish";

		std::string listener = "relay";
		AMQPListener* l = amqp_listeners[listener];
		l->receive(jobj);

		cmd->return_code = 1;
		lock_release(&cmd->lock);

	}

	void handle_stop_relay(kz_amqp_cmd_ptr cmd)
	{

		if (relay_ids.find(cmd->relay_connection_id) == relay_ids.end()) {
			AMQP_DBG << "replay_connection_id not found : " << cmd->relay_connection_id;
		}

		auto payload = json::object();
		payload["Event-Category"] = "relay";
		payload["Event-Name"] = "stop";

		std::string relay_id = strformat("%i/%s", cmd->relay_connection_id, relay_ids[cmd->relay_connection_id].c_str());
		auto arguments = json::object();
		arguments["relay-id"] = relay_id;

		auto envelope = json::object();
		envelope["exchange"] = "relay";
		envelope["routing-key"] = "time-to-die";
		envelope["payload"] = payload;
		envelope["arguments"] = arguments;
		envelope["AMQP-Hash"] = relay_id;

		auto jobj = new json(envelope);

		AMQP_DBG << "sending stop relay to publish";

		std::string listener = "relay";
		AMQPListener* l = amqp_listeners[listener];
		l->receive(jobj);

		relay_ids.erase(cmd->relay_connection_id);

		cmd->return_code = 1;
		lock_release(&cmd->lock);

	}

	virtual void OnData(kz_amqp_cmd_ptr cmd)
	{
		cfg_update();

		switch(cmd->type)
		{
		case KZ_AMQP_CMD_PUBLISH:
			handle_publish(cmd);
			break;

		case KZ_AMQP_CMD_CALL:
			handle_call(cmd);
			break;

		case KZ_AMQP_CMD_ASYNC_CALL:
			handle_async_call(cmd);
			break;

		case KZ_AMQP_CMD_RELAY:
			handle_relay(cmd);
			break;

		case KZ_AMQP_CMD_STOP_RELAY:
			handle_stop_relay(cmd);
			break;

		default:
			AMQP_ERR << "unhandled command : " << cmd->type << " - " << cmd->payload;
			break;
		}
	}

private:
	int worker = 0;

};


void setup_main_listener()
{
	Exchange targeted("targeted", "direct");
	targeted.options = kz::AMQPFlag::none;
	gExchanges["targeted"] = targeted;

	Exchange registrar("registrar", "topic");
	registrar.options = kz::AMQPFlag::none;
	gExchanges["registrar"] = registrar;

	Exchange callmgr("callmgr", "topic");
	callmgr.options = kz::AMQPFlag::none;
	gExchanges["callmgr"] = callmgr;

	Queue queue(std::string(dbk_default_worker_name_template.s, dbk_default_worker_name_template.len));
	queue.options = kz::AMQPFlag::autodelete;
	queue.consume.options = kz::AMQPFlag::noack;

	queue.addBinding(targeted, kz::RoutingKeys({""}));

	std::vector<kz::Exchange> exchanges;
	exchanges.push_back(callmgr);
	exchanges.push_back(registrar);
	exchanges.push_back(targeted);
	for(auto& b : gDeclaredExchanges) {
		exchanges.push_back(b.second);
	}

	Listener main;
	main.name = "default";
	main.exchanges = exchanges;
	main.queues = {queue};
	main.num_channels = dbk_channels;
	main.num_workers = dbk_consumer_processes;
	main.consumer_spawn = true;

	listeners["default"] = main;

}

void setup_relay_listener()
{
	Exchange relay("relay", "topic");
	relay.options = kz::AMQPFlag::none;
	gExchanges["relay"] = relay;

	AMQP::Table relay_x_args;
	relay_x_args["hash-header"] = "relay-id";
	relay_x_args["x-nodename"] = node().c_str();

	std::string relay_uuid = uuids::to_string(uuids::uuid_system_generator{}());
	std::string exchange_name = strformat("relay.%s", relay_uuid.c_str());
	Exchange relay_x(exchange_name, "x-consistent-hash", relay_x_args);
	relay_x.options = kz::AMQPFlag::autodelete;
	gExchanges[exchange_name] = relay_x;
	relay_x.addBinding(relay, kz::RoutingKeys({encode(node())}));

	Queue relay_x_q;
	relay_x_q.options = kz::AMQPFlag::autodelete;
	relay_x_q.consume.options = kz::AMQPFlag::noack;
	relay_x_q.addBinding(relay_x, kz::RoutingKeys({"20"}));

	Listener relay_l;
	relay_l.name = "relay";
	relay_l.exchanges = {relay_x};
	relay_l.queues = {relay_x_q};
	relay_l.num_channels = 3;
	relay_l.num_workers = 4;
	relay_l.consumer_spawn = true;

	listeners["relay"] = relay_l;

}

}

extern "C" void kz_amqp_manager_proc(int cmd_pipes[2])
{

	using namespace kz;

	AMQP_DBG << "starting amqp manager " << my_pid();

	server_uuid = uuids::to_string(uuids::uuid_system_generator{}());

	zonemap = zone_map(mystore);
	local_zone = std::string(dbk_primary_zone_name.s, dbk_primary_zone_name.len);

	auto isFederated = [&](const std::pair<std::string, ConnectionInfoList> &it) {
		return it.first != local_zone;
	};

	primary_zone = zonemap[local_zone];
	federated = filter(zonemap, isFederated);

	kz::setup_main_listener();

	if(cfg_get(kz_relay, kz_relay_cfg, active)) {
		kz::setup_relay_listener();
	}

	kz::initialize_counters();

	kz::Manager mgr(cmd_pipes);
	mgr.start();


	AMQP_INFO << "stopping manager " << my_pid();

	_exit(0);
}


void kz_amqp_worker_proc(kz::SIPWorker* worker)
{
	AMQP_DBG << "starting amqp sip worker " << my_pid();
	worker->start();
	AMQP_DBG << "stopping amqp sip worker " << my_pid();
	_exit(0);
}


extern "C" int kz_amqp_start_worker_procs()
{
	for(int n=0; n < dbk_consumer_workers; n++) {
		auto worker = new kz::SIPWorker(kz_worker_pipes_fds[n*2], kz_worker_pipes_fds[n*2+1]);
		auto pid = fork_process(AMQP_WORKERS_RANKING, "AMQP Consumer Worker", 1);
		if (pid < 0)
			return -1;
		if(pid==0) {
			if (cfg_child_init()) return -1;
			kz_amqp_worker_proc(worker);
		} else {
			kz::sip_workers.push_back(worker);
		}
	}
	return 0;
}

extern "C" int kz_amqp_add_connection(char* url_str)
{

	using namespace kz;

	char* url = strdup( url_str);
	char* zone_str_ptr = NULL;

	AMQP_DBG << "adding connection " << url;

	if(!strncmp(url, "zone=", 5)) {
		char* ptr = strchr(url, ';');
		zone_str_ptr = url+(5*sizeof(char));
		if(ptr == NULL) {
			AMQP_ERR << "missing ';' at the end of zone name '" << url << "'";
			free(url);
			return -1;
		}
		*ptr = '\0';
		if(strlen(zone_str_ptr) == 0) {
			AMQP_ERR << "invalid zone name '" << url << "'";
			free(url);
			return -1;
		}
		url = ++ptr;
	}

	std::string str_url(url);
	try
	{
		AMQP::Address amqp_url(str_url);
		std::string str_zone;
		bool isLocal = false;
		if(zone_str_ptr == NULL) {
			str_zone = std::string(dbk_primary_zone_name.s, dbk_primary_zone_name.len);
			isLocal = true;
		} else {
			str_zone = std::string(zone_str_ptr);
			isLocal = (str_zone == std::string(dbk_primary_zone_name.s, dbk_primary_zone_name.len));
		}

		AMQP::Table props;
		props["zone"] = str_zone;
		props["is_local"] = isLocal;

		ConnectionInfo connection(str_url, props);

		mystore[connection.uri()] = connection;

		AMQP_DBG << "connection added : " << json(connection);
	}
	catch(const std::exception& exc)
	{
		AMQP_ERR << "error parsing amqp url - " << exc.what() << " => " << str_url;
		return -1;
	}

	return 0;

}

extern "C" int kz_amqp_subscribe(char* payload)
{
	using namespace kz;
	AMQP_DBG << "GOT A SUBSCRIBE " << payload;

	json jobj;
	Listener main;

	try
	{
		jobj = json::parse(payload);
		main = jobj;
	}
	catch(const std::exception& exc)
	{
		AMQP_ERR << "error parsing subscribe - " << exc.what() << " => " << payload;
		return -1;
	}


	AMQP_DBG << "TRANSLATED TO " << json(main);

	listeners[main.name] = std::move(main);


	return 1;
}

KZ_EXTERN_C int kz_pv_set_ki(struct sip_msg* msg, pv_param_t *param, int op, pv_value_t *val)
{
	if((val==NULL) || (val->flags & PV_VAL_NULL) || ((val->flags & PV_VAL_STR) && val->rs.len == 0))
	{
		kz::reset_call_id();
		return 0;
	}

	if(!(val->flags & PV_VAL_STR))
	{
		AMQP_ERR << "str value required to set ki";
		return -1;
	}

	kz::set_call_id_str_len(val->rs.s, val->rs.len);

	return 0;

}

KZ_EXTERN_C int kz_pv_get_node(struct sip_msg *msg, pv_param_t *param,	pv_value_t *res)
{
	std::string str = kz::node();

	char * buffer = kz_tr_set_crt_buffer();
	strncpy(buffer, (char *) str.c_str(), (int) str.length());
	buffer[str.length()] = '\0';

	return pv_get_strzval(msg, param, res, buffer);
}

KZ_EXTERN_C int kz_pv_get_gregorian_timestamp(struct sip_msg *msg, pv_param_t *param, pv_value_t *res)
{
	time_t t;
	t = time(NULL) + UNIX_EPOCH_IN_GREGORIAN;
	return pv_get_uintval(msg, param, res, (unsigned int)t);
}

KZ_EXTERN_C int kz_pv_get_interaction_id(struct sip_msg *msg, pv_param_t *param, pv_value_t *res)
{
	uint64_t received = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() + UNIX_EPOCH_IN_GREGORIAN;
	std::string uuid_str = uuids::to_string(uuids::uuid_system_generator{}());
	std::string str = kz::strformat("%ld-%s", received, uuid_str.substr(0, 8).c_str());

	char * buffer = kz_tr_set_crt_buffer();
	strncpy(buffer, (char *) str.c_str(), (int) str.length());
	buffer[str.length()] = '\0';

	return pv_get_strzval(msg, param, res, buffer);
}

extern "C" int kz_amqp_subscribe_simple(char* str_exchange, char* str_exchange_type, char* str_queue, char* str_routing_key)
{
	using namespace kz;
	Exchange exchange(str_exchange, str_exchange_type);
	gExchanges[exchange.name] = exchange;
	Queue queue(str_queue);
	queue.addBinding(exchange, {str_routing_key});

	std::string name = kz::rand_string(5);
	Listener main;
	main.name = name;
	main.exchanges = {std::move(exchange)};
	main.queues = {std::move(queue)};
	main.num_channels = 35;
	main.num_workers = 10;
	main.consumer_spawn = true;

	kz::listeners[name] = std::move(main);

    return 1;

}

extern "C" int kz_amqp_declare_exchange(char* payload)
{
	using namespace kz;
	AMQP_DBG << "got a declare exchange " << payload;

	json jobj;
	Exchange exchange;

	try
	{
		jobj = json::parse(payload);
		exchange = jobj;
	}
	catch(const std::exception& exc)
	{
		AMQP_ERR << "error parsing declare exchange - " << exc.what() << " => " << payload;
		return -1;
	}

	AMQP_DBG << "translated to " << json(exchange);

	gDeclaredExchanges[exchange.name] = std::move(exchange);

	return 1;
}


namespace kz
{

bool ExchangeBindings::add(std::string exchange, std::vector<std::string> keys, const AMQP::Table& arguments)
{

	if(gExchanges.find(exchange) != gExchanges.end())
		return add(gExchanges[exchange], keys, arguments);

	return false;
}

bool ExchangeBindings::add(const Exchange& exchange, std::vector<std::string> keys, const AMQP::Table& arguments)
{
	  if(_map.find(exchange.name) != _map.end()) {
		  ExchangeBinding& existing = _map[exchange.name];
		  for (auto key : keys) {
			  existing.addKey(key);
		  }
	  } else {
		  ExchangeBinding q(exchange, keys, arguments);
		  _map[exchange.name] = q;
	  }
	  return true;
}

bool Exchange::addBinding(std::string exchange, std::vector<std::string> keys, const AMQP::Table& arguments)
{
	return bindings.add(exchange, keys, arguments);
}

bool Exchange::addBinding(const Exchange& exchange, std::vector<std::string> keys, const AMQP::Table& arguments)
{
	return bindings.add(exchange, keys, arguments);
}

bool QueueBindings::add(std::string exchange, std::vector<std::string> keys, bool federated, const AMQP::Table& arguments)
{

	if(gExchanges.find(exchange) != gExchanges.end())
		return add(gExchanges[exchange], keys, federated, arguments);
	AMQP_ERR << "exchange '" << exchange << "' not found";
	return false;
}

bool QueueBindings::add(const Exchange& exchange, std::vector<std::string> keys, bool federated, const AMQP::Table& arguments)
{
	if(_map.find(exchange.name) != _map.end()) {
		QueueBinding& existing = _map[exchange.name];
		for (auto key : keys) {
			existing.addKey(key);
		}
		if(!existing.IsFederated()) existing.IsFederated(federated) ;
	} else {
		QueueBinding q(exchange, keys, federated, arguments);
		_map[exchange.name] = q;
	}
	return true;
}

bool Queue::addBinding(std::string exchange, std::vector<std::string> keys, bool federated, const AMQP::Table& arguments)
{
	return bindings.add(exchange, keys, federated, arguments);
}

bool Queue::addBinding(const Exchange& exchange, std::vector<std::string> keys, bool federated, const AMQP::Table& arguments)
{
	return bindings.add(exchange, keys, federated, arguments);
}

const std::string ConnectionInfo::default_uri = "amqp://localhost";


}

namespace kz
{

using json = nlohmann::json;

void to_json(json& j, const ConnectionInfo& p) {
    j = json{{"uri", std::string(p.uri())}
            ,{"properties", p.properties()}
            };
}

void from_json(const json& j, ConnectionInfo& p) {
    p.set_uri(j.at("uri").get<std::string>());
	if (j.find("properties") != j.end()) {
        p.set_properties(j.at("properties").get<AMQP::Table>());
	}
}


void to_json(json& j, const ExchangeBindings& p) {
	for(auto& b : p.map())
	{
		json binding = json::object();
		const ExchangeBinding& qb = b.second;
		binding["routing-keys"] = qb.routing();
		binding["arguments"] = qb.arguments();
		j[qb.exchange().name] = binding;
	}
}

void from_json(const json& j, ExchangeBindings& p) {
	for(auto& item : j.items())
	{
		std::string exchange = item.key();
		auto val = item.value();
		RoutingKeys keys = val.find("routing-keys") != val.end() ? val["routing-keys"].get<std::vector<std::string>>() : std::vector<std::string>();
		AMQP::Table arguments = val.find("arguments") != val.end() ? val["arguments"].get<AMQP::Table>() : AMQP::Table();
		p.add(exchange, keys, arguments);
	}
}

void to_json(json& j, const Exchange& p) {
	j = json::object();

    j["name"] = p.name;
    j["type"] = p.type;
	if(p.options) j["options"] = p.options;
	if(p.arguments.count() > 0) j["arguments"] = p.arguments;
    if(p.bindings.map().size() > 0) {
        j["bindings"] = p.bindings;
    }
}

void from_json(const json& j, Exchange& p) {
    p.name = j.at("name").get<std::string>();
    p.type = j.at("type").get<std::string>();
	if (j.find("arguments") != j.end()) {
        p.arguments = j.at("arguments").get<AMQP::Table>();
	}
	if (j.find("options") != j.end()) {
        p.options = j.at("options").get<Flag>();
	}
	if (j.find("bindings") != j.end()) {
        p.bindings = j.at("bindings").get<ExchangeBindings>();
	}
}


    void to_json(json& j, const QueueBindings& p) {
    	for(auto& b : p.map())
    	{
    		json binding = json::object();
    		const QueueBinding& qb = b.second;
    		binding["routing-keys"] = qb.routing();
    		if(qb.IsFederated()) binding["federated"] = qb.IsFederated();
    		if(qb.arguments().count() > 0)	binding["arguments"] = qb.arguments();
    		j[qb.exchange().name] = binding;
    	}
    }

    void from_json(const json& j, QueueBindings& p) {
    	for(auto& item : j.items())
    	{
    		std::string exchange = item.key();
    		auto val = item.value();
    		RoutingKeys keys = val.find("routing-keys") != val.end() ? val["routing-keys"].get<std::vector<std::string>>() : std::vector<std::string>();
    		bool federated = val.find("federated") != val.end() ? val["federated"].get<bool>() : false;
    		AMQP::Table arguments = val.find("arguments") != val.end() ? val["arguments"].get<AMQP::Table>() : AMQP::Table();
    		p.add(exchange, keys, federated, arguments);
    	}
    }

    void to_json(json& j, const Qos& p) {
        j = json{{"prefetch-count", p.prefetch_count}
                ,{"prefetch-shared", p.is_shared}
                };
    }

    void from_json(const json& j, Qos& p) {
    	if (j.find("prefetch-count") != j.end()) {
            p.prefetch_count = j.at("prefetch-count").get<int>();
    	}
    	if (j.find("prefetch-shared") != j.end()) {
            p.is_shared = j.at("prefetch-shared").get<bool>();
    	}
    }


    void to_json(json& j, const Flag& p) {
    	j = json::array();
#define X(a, b, c) if( p & a) j.push_back(c);
    	FLAGS_TABLE
#undef X
    }

    void from_json(const json& j, Flag& p) {
    	for (auto& y : j)
    	{
    		std::string v = y.get<std::string>();
#define X(a, b, c) if( v == c) p |= a;
    	FLAGS_TABLE
#undef X
    	}
    }

    void to_json(json& j, const Consume& p) {
    	j = json::object();
    	if(p.qos.is_shared || p.qos.prefetch_count > 0) j["qos"] = p.qos;
    	if(p.options) j["options"] = p.options;
    	if(p.arguments.count() > 0) j["arguments"] = p.arguments;
    }

    void from_json(const json& j, Consume& p) {
    	if (j.find("arguments") != j.end()) {
            p.arguments = j.at("arguments").get<AMQP::Table>();
    	}
    	if (j.find("options") != j.end()) {
            p.options = j.at("options").get<Flag>();
    	}
    	if (j.find("qos") != j.end()) {
    		p.qos = j.at("qos").get<Qos>();
    	}
    }


    void to_json(json& j, const Queue& p) {
    	j = json::object();

        j["name"] = p.name;

    	if(p.options) j["options"] = p.options;

    	if(p.arguments.count() > 0) j["arguments"] = p.arguments;

    	if(p.consume.qos.is_shared ||
    			p.consume.qos.prefetch_count > 0 ||
    			p.consume.options ||
    			p.consume.arguments.count() > 0
    	) j["consume"] = p.consume;

        if(p.bindings.map().size() > 0) {
            j["bindings"] = p.bindings;
        }
    }

    void from_json(const json& j, Queue& p) {
    	if (j.find("name") != j.end()) {
            p.name = j.at("name").get<std::string>();
    	}
    	if (j.find("arguments") != j.end()) {
            p.arguments = j.at("arguments").get<AMQP::Table>();
    	}
    	if (j.find("options") != j.end()) {
            p.options = j.at("options").get<Flag>();
    	}
    	if (j.find("consume") != j.end()) {
            p.consume = j.at("consume").get<Consume>();
    	}
    	if (j.find("bindings") != j.end()) {
            p.bindings = j.at("bindings").get<QueueBindings>();
    	}
    }

    void to_json(json& j, const Listener& p) {
    	j = json::object();

        j["name"] = p.name;
    	if(p.arguments.count() > 0) j["arguments"] = p.arguments;


    	j["num-channels"] = p.num_channels;
    	j["num-workers"] = p.num_workers;
    	j["num-federated-workers"] = p.num_federated_workers;
    	if(p.consistent_worker) j["consistent-worker"] = p.consistent_worker;
    	if(!p.consistent_worker_key.empty()) j["consistent-worker-key"] = p.consistent_worker_key;
    	if(p.consumer_spawn) j["consumer-spawn"] = p.consumer_spawn;
    	if(p.deliver_local) j["deliver-local"] = p.deliver_local;
    	if(p.publisher_confirms) j["publisher-confirms"] = p.publisher_confirms;

    	if(p.exchanges.size() > 0) j["exchanges"] = p.exchanges;
    	if(p.queues.size() > 0) j["queues"] = p.queues;

    }

    void from_json(const json& j, Listener& p) {
    	if (j.find("name") != j.end()) {
            p.name = j.at("name").get<std::string>();
    	}
    	if (j.find("arguments") != j.end()) {
            p.arguments = j.at("arguments").get<AMQP::Table>();
    	}
    	if (j.find("exchanges") != j.end()) {
            p.exchanges = j.at("exchanges").get<std::vector<Exchange>>();
    	}
    	if (j.find("queues") != j.end()) {
            p.queues = j.at("queues").get<std::vector<Queue>>();
    	}

    	if (j.find("num-channels") != j.end()) {
            p.num_channels = j.at("num-channels").get<int>();
    	}
    	if (j.find("num-workers") != j.end()) {
            p.num_workers = j.at("num-workers").get<int>();
    	}
    	if (j.find("num-federated-workers") != j.end()) {
            p.num_federated_workers = j.at("num-federated-workers").get<int>();
    	}
    	if (j.find("consistent-worker") != j.end()) {
            p.consistent_worker = j.at("consistent-worker").get<bool>();
    	}
    	if (j.find("consistent-worker-key") != j.end()) {
            p.consistent_worker_key = j.at("consistent-worker-key").get<std::string>();
    	}
    	if (j.find("consumer-spawn") != j.end()) {
            p.consumer_spawn = j.at("consumer-spawn").get<bool>();
    	}
    	if (j.find("deliver-local") != j.end()) {
            p.deliver_local = j.at("deliver-local").get<bool>();
    	}
    	if (j.find("publisher-confirms") != j.end()) {
            p.publisher_confirms = j.at("publisher-confirms").get<bool>();
    	}

    	// legacy
    	if (j.find("exchange") != j.end()) {
    		std::string exchange_name = j.at("exchange").get<std::string>();
    		std::string exchange_type;
    		bool exchange_auto_delete = false;
    		bool exchange_delete_on_error = false;
			AMQP::Table exchange_args;


    		std::string queue_name;
    		std::vector<std::string> queue_routing;
    		bool queue_noack = true;
    		bool queue_exclusive = true;
    		bool queue_autodelete = true;

    		bool federate = false;

    		if (j.find("exchange-def") != j.end()) {
    			json exchange_def = j["exchange-def"];
    			if(exchange_def.find("type") != exchange_def.end()) {
        			exchange_type = exchange_def.at("type").get<std::string>();
    			}
    			if(exchange_def.find("auto_delete") != exchange_def.end()) {
        			exchange_auto_delete = exchange_def.at("auto_delete").get<bool>();
    			}
    			if(exchange_def.find("delete_on_error") != exchange_def.end()) {
        			exchange_delete_on_error = exchange_def.at("delete_on_error").get<bool>();
    			}
		    	if(exchange_def.find("arguments") != exchange_def.end()) {
		    		exchange_args = exchange_def.at("arguments").get<AMQP::Table>();
		    	}
    		} else {
    			exchange_type = j.at("type").get<std::string>();
    		}

    		if (j.find("federate") != j.end()) {
    			federate = j.at("federate").get<bool>();
    		}

    		Exchange exchange(exchange_name, exchange_type, exchange_args);
    		exchange.options = AMQPFlag::none;
    		if(exchange_auto_delete) {
    			exchange.options |= AMQPFlag::autodelete;
    		}
    		exchange.delete_on_error = exchange_delete_on_error;

    		if (j.find("exchange-bindings") != j.end()) {
    			json exchange_bindings = j["exchange-bindings"];
    			for (auto it = exchange_bindings.begin(); it != exchange_bindings.end(); ++it)
    			{
    				AMQP::Table exchange_binding_args;
    				AMQP::Table exchange_binding_routing_args;
    				std::string exchange_b_name = it.key();
    				json exchange_binding = it.value();
    				std::string exchange_b_type = exchange_binding["type"];
    		    	if(exchange_binding.find("arguments") != exchange_binding.end()) {
    		    		exchange_binding_args = exchange_binding.at("arguments").get<AMQP::Table>();
    		    	}

    				std::vector<std::string> exchange_b_routing;
    				auto y = exchange_binding["routing"];
    		     	if(y.type() == json::value_t::string) {
    		     		exchange_b_routing = {exchange_binding.at("routing").get<std::string>()};
    		     	} else {
    		     		exchange_b_routing = exchange_binding.at("routing").get<std::vector<std::string>>();
    		     	}
    		    	if (exchange_binding.find("routing-arguments") != exchange_binding.end()) {
    		    		exchange_binding_routing_args = exchange_binding.at("routing-arguments").get<AMQP::Table>();
    		    	}

    		     	Exchange exchange_b(exchange_b_name, exchange_b_type, exchange_binding_args);
    		     	exchange_b.options = AMQPFlag::none;
    		     	p.exchanges.push_back(std::move(exchange_b));
    		     	gExchanges[exchange_b_name] = exchange_b;
    		     	exchange.addBinding(exchange_b_name, exchange_b_routing, exchange_binding_routing_args);
    			}
    		}

	     	gExchanges[exchange_name] = exchange;
	     	p.exchanges.push_back(std::move(exchange));


    		if (j.find("queue") != j.end()) {
    			queue_name = j.at("queue").get<std::string>();
    		}

    		if (j.find("routing") != j.end()) {
    			auto r = j["routing"];
    	     	if(r.type() == json::value_t::string) {
    	     		queue_routing = {j.at("routing").get<std::string>()};
    	     	} else {
    	     		queue_routing = j.at("routing").get<std::vector<std::string>>();
    	     	}
    		}

    		if (j.find("no_ack") != j.end()) {
    			queue_noack = j.at("no_ack").get<bool>();
    		}

    		if (j.find("exclusive") != j.end()) {
    			queue_exclusive = j.at("exclusive").get<bool>();
    		}

    		if (j.find("auto_delete") != j.end()) {
    			queue_autodelete = j.at("auto_delete").get<bool>();
    		}

    		Queue queue(queue_name);
    		queue.options = AMQPFlag::none;
    		queue.consume.options = AMQPFlag::none;

    		if(queue_exclusive) {
    			queue.options |= AMQPFlag::exclusive;
    			queue.consume.options |= AMQPFlag::exclusive;
    		}

    		if(queue_noack) {
    			queue.consume.options |= AMQPFlag::noack;
    		}

    		if(queue_autodelete) {
    			queue.options |= AMQPFlag::autodelete;
    		}

    		if(queue_routing.size() == 0) {
    			queue_routing = {""};
    		}
    		queue.addBinding(exchange_name, queue_routing, federate);

    		p.queues = {std::move(queue)};

    	}

    }

}

namespace AMQP
{

typedef std::map<std::string, std::shared_ptr<Field> > FieldMap;
typedef std::vector<std::shared_ptr<Field>> FieldArray;


void to_json(json& j, const Array& p) {
    j = p.Fields();
}


void from_json(const json& j, Array& p) {
	for (auto& y : j)
	{
     	switch(y.type())
     	{

     	case json::value_t::string:
     		p.push_back(LongString(y.get<std::string>()));
     		break;

     	case json::value_t::boolean:
     		p.push_back(BooleanSet(y.get<bool>()));
     		break;

     	case json::value_t::number_integer:
     	case json::value_t::number_unsigned:
     		p.push_back(Long(y.get<int>()));
     		break;


     	case json::value_t::number_float:
     		p.push_back(Float(y.get<float>()));
     		break;

     	case json::value_t::object:
     		p.push_back(y.get<Table>());
     		break;

     	case json::value_t::array:
     		p.push_back(y.get<Array>());
     		break;

     	default:
     		break;
     	}
    }
}

    void to_json(json& j, const Table& p) {
        j = p.Fields();
    }

    void from_json(const json& j, Table& p) {
         for (auto& x : j.items()) {
        	 auto y = x.value();
         	switch(y.type())
         	{

         	case json::value_t::string:
         		p[x.key()] = y.get<std::string>();
         		break;

         	case json::value_t::boolean:
         		p[x.key()] = y.get<bool>();
         		break;

         	case json::value_t::number_integer:
         	case json::value_t::number_unsigned:
         		p[x.key()] = y.get<int>();
         		break;

         	case json::value_t::number_float:
         		p[x.key()] = y.get<float>();
         		break;

         	case json::value_t::object:
         		p[x.key()] = y.get<Table>();
         		break;

         	case json::value_t::array:
         		p[x.key()] = y.get<Array>();
         		break;

         	default:
         		break;
         	}
        }
    }

    void to_json(json& j, const std::shared_ptr<Field>& p) {
    	j = *p;
    }


    template <typename T, typename I>
    void to_json(json& j, const FieldProxy<T,I>& p) {
    	j = p.get();
    };

    void to_json(json& j, const Field& p) {
        // create field based on type
        switch (p.typeID())
        {
            case 't':   j = (bool) static_cast<BooleanSet>(p).value();break;
            case 'b':   j = static_cast<Octet>(p).value();break;
            case 'B':   j = static_cast<UOctet>(p).value();break;
            case 'U':   j = static_cast<Short>(p).value();break;
            case 'u':   j = static_cast<UShort>(p).value();break;
            case 'I':   j = static_cast<Long>(p).value();break;
            case 'i':   j = static_cast<ULong>(p).value();break;
            case 'L':   j = static_cast<LongLong>(p).value();break;
            case 'l':   j = static_cast<ULongLong>(p).value();break;
            case 'f':   j = static_cast<Float>(p).value();break;
            case 'd':   j = static_cast<Double>(p).value();break;
            case 'D':   j = static_cast<DecimalField>(p).value();break;
            case 's':   j = static_cast<ShortString>(p).value();break;
            case 'S':   j = static_cast<LongString>(p).value();break;
            case 'A':   j = static_cast<Array>(p).Fields();break;
            case 'T':   j = static_cast<Timestamp>(p).value();break;
            case 'F':   j = static_cast<Table>(p).Fields();break;
            default:    j = json::value_t::discarded;break;
        }
    }

}
