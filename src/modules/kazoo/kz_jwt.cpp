#include "kz_jwt.h"
#include "include/date.hpp"

#include <chrono>
#include <ctime>
#include <iostream>
#include <sstream>

KZ_BEGIN_EXTERN_C

sr_xavp_t ** kz_get_keys_crt();
char *kz_tr_set_crt_buffer(void);

extern gen_lock_t * kz_keys_lock;
extern htable_api_t kz_ht_api;
extern char* last_payload_result;
static char* _kz_jwt_error = NULL;

KZ_END_EXTERN_C

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

#include "include/kz_log.hpp"
#include "include/json.hpp"
#include "include/jwt.hpp"
#include "include/uuid.hpp"

using json = nlohmann::json;

typedef std::map<std::string, std::string> keys;

struct kz_token {
	using json = nlohmann::json;
	using value_type = json;
	using object_type = json::object_t;
	using array_type = json::array_t;
	using string_type = std::string;
	using number_type = double;
	using integer_type = int64_t;
	using boolean_type = bool;

	static jwt::json::type get_type(const json &val) {
		using jwt::json::type;

		if (val.type() == json::value_t::boolean)
			return type::boolean;
		else if (val.type() == json::value_t::number_integer)
			return type::integer;
		else if (val.type() == json::value_t::number_float)
			return type::number;
		else if (val.type() == json::value_t::string)
			return type::string;
		else if (val.type() == json::value_t::array)
			return type::array;
		else if (val.type() == json::value_t::object)
			return type::object;
		else
			throw std::logic_error("invalid type");
	}

	static json::object_t as_object(const json &val) {
		if (val.type() != json::value_t::object)
			throw std::bad_cast();
		return val.get<json::object_t>();
	}

	static std::string as_string(const json &val) {
		if (val.type() != json::value_t::string)
			throw std::bad_cast();
		return val.get<std::string>();
	}

	static json::array_t as_array(const json &val) {
		if (val.type() != json::value_t::array)
			throw std::bad_cast();
		return val.get<json::array_t>();
	}

	static int64_t as_int(const json &val) {
		if (val.type() != json::value_t::number_integer && val.type() != json::value_t::number_unsigned)
			throw std::bad_cast();
		return val.get<int64_t>();
	}

	static bool as_bool(const json &val) {
		if (val.type() != json::value_t::boolean)
			throw std::bad_cast();
		return val.get<bool>();
	}

	static double as_number(const json &val) {
		if (val.type() != json::value_t::number_float)
			throw std::bad_cast();
		return val.get<double>();
	}

	static bool parse(json &val, std::string str) {
		val = json::parse(str.begin(), str.end());
		return true;
	}

	static std::string serialize(const json &val) {
		return val.dump();
	}
};

namespace kz
{
    namespace pkg
    {
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

		void set_last_json_result(json& jobj)
		{
			std::ostringstream jobjStream;
			jobjStream << jobj;
			std::string str_payload = jobjStream.str();

			set_last_result((char*) str_payload.c_str());
		}

		void reset_jwt_error()
		{

			if(_kz_jwt_error) {
				pkg_free(_kz_jwt_error);
			}
			_kz_jwt_error = NULL;
		};

		void set_jwt_error(const char *error)
		{
			reset_jwt_error();
			int len = strlen(error);
			char* value = (char*) pkg_malloc(len+1);
			if(value) {
				memcpy(value, error, len);
				value[len] = '\0';
				_kz_jwt_error = value;
			}
		}

    }

	namespace htable
	{
		std::string fetch(std::string table, std::string key)
		{
			std::string pem;
			str str_table = {(char*)table.c_str(), (int)table.length()};
			str str_key = {(char*)key.c_str(), (int)key.length()};
			ht_cell_t *cell = kz_ht_api.get_clone(&str_table, &str_key);
			if(cell != NULL) {
				pem= std::string(cell->value.s.s, cell->value.s.len);
				pkg_free(cell);
			}

			return pem;
		}
	}

	namespace xavp
	{
	// copied from core/xavp.c xavp_get_internal
	sr_xavp_t *get(std::string name, sr_xavp_t **list)
	{
		sr_xavp_t *avp;
		unsigned int id;
		str sname = {(char*)name.c_str(), (int)name.length()};


		id = get_hash1_raw(sname.s, sname.len);

		if(!list || !(*list)) return NULL;

		avp = *list;
		while(avp)
		{
			if(avp->id==id && avp->name.len==sname.len
					&& strncmp(avp->name.s, sname.s, sname.len)==0)
			{
				return avp;
			}
			avp = avp->next;
		}
		return NULL;
	}

	sr_xavp_t *get(std::string name)
	{
		str sname = {(char*)name.c_str(), (int)name.length()};
		return xavp_get(&sname, NULL);
	}

	sr_xavp_t *store(std::string name, sr_xval_t *val, sr_xavp_t **list)
	{
		sr_xavp_t *cur;

		if(val == NULL) return NULL;

		str sname = {(char*)name.c_str(), (int)name.length()};

		/* Find the current value */
		cur = get(name, list);
		if(cur == NULL) {
			return xavp_add_value(&sname, val, list);
		} else {
			return xavp_set_value(&sname, 0, val, list);
		}
	}

	sr_xavp_t *store(std::string name, sr_xval_t *val)
	{
		sr_xavp_t *cur;

		if(val == NULL) return NULL;

		str sname = {(char*)name.c_str(), (int)name.length()};

		/* Find the current value */
		cur = get(name);
		if(cur == NULL) {
			return xavp_add_value(&sname, val, NULL);
		} else {
			return xavp_set_value(&sname, 0, val, NULL);
		}
	}

	}

	namespace pk
	{
		void fetch(const char *KeyId)
		{
		}

		void store(const char *KeyId, const char *Pem)
		{
		}

		void create(const char *KeyId, const char *Pem)
		{
		}
	}

	namespace jwt
	{
		//using decoded_jwt = ::jwt::decoded_jwt<kz_token>;
		using rsa =  ::jwt::algorithm::rsa;
		using rs256 = ::jwt::algorithm::rs256;
		using json = nlohmann::json;
		using default_clock  = ::jwt::default_clock;
		using date = std::chrono::system_clock::time_point::clock;
		using verifier = ::jwt::verifier<default_clock, kz_token>;
		using builder = ::jwt::builder<kz_token>;
		using claim = ::jwt::basic_claim<kz_token>;

		bool is_jwt(std::string& token)
		{
			try
			{
				auto decoded = ::jwt::decode<kz_token>(token);

				for (auto &e : decoded.get_header_claims())
					AMQP_DBG << e.first << " = " << e.second.to_json();
				for (auto &e : decoded.get_payload_claims())
					AMQP_DBG << e.first << " = " << e.second.to_json();

			}
			catch(const std::invalid_argument& exc)
			{
				AMQP_DBG << "error validating token - " << exc.what() << " => " << token;
				return false;
			}
			catch(const std::runtime_error& exc)
			{
				AMQP_DBG << "error validating token - " << exc.what() << " => " << token;
				return false;
			}
			catch(const std::exception& exc)
			{
				AMQP_DBG << "error validating token - " << exc.what() << " => " << token;
				return false;
			}

			return true;
		}

		bool is_valid(std::string& token)
		{
			pkg::reset_jwt_error();

			try
			{
				auto decoded = ::jwt::decode<kz_token>(token);
				if (!decoded.has_key_id()) {
					AMQP_DBG << "NO KID";
					pkg::set_jwt_error("no kid identifier is present");
					return false;
				}
				if (!decoded.has_algorithm()) {
					AMQP_DBG << "NO ALG";
					pkg::set_jwt_error("no alg identifier is present");
					return false;
				}
				if (!decoded.has_issuer()) {
					AMQP_DBG << "NO ISS";
					pkg::set_jwt_error("no issuer identifier is present");
					return false;
				}
				if (decoded.get_issuer().compare("kazoo") != 0) {
					AMQP_DBG << "=/ ISS => " << decoded.get_issuer();
					pkg::set_jwt_error("issuer not supported");
					return false;
				}
				if (decoded.get_algorithm().compare("RS256") != 0) {
					AMQP_DBG << "=/ ALG => " << decoded.get_algorithm();
					pkg::set_jwt_error("alg not supported");
					return false;
				}

				std::string pem = htable::fetch("jwt.keys", decoded.get_key_id());
				if (pem.empty()) {
					AMQP_DBG << "KID " << decoded.get_key_id() << " not found";
					pkg::set_jwt_error("kid not found");
					return false;
				}

				rsa key = rs256(pem);
				auto ver = verifier({})
								.with_issuer("kazoo")
								.allow_algorithm(key);

				ver.verify(decoded);

				for (auto &e : decoded.get_header_claims())
					AMQP_DBG << e.first << " = " << e.second.to_json();
				for (auto &e : decoded.get_payload_claims())
					AMQP_DBG << e.first << " = " << e.second.to_json();

			}
			catch(const std::invalid_argument& exc)
			{
				AMQP_DBG << "error validating token - " << exc.what() << " => " << token;
				pkg::set_jwt_error(exc.what());
				return false;
			}
			catch(const std::runtime_error& exc)
			{
				AMQP_DBG << "error validating token - " << exc.what() << " => " << token;
				pkg::set_jwt_error(exc.what());
				return false;
			}
			catch(const std::exception& exc)
			{
				AMQP_DBG << "error validating token - " << exc.what() << " => " << token;
				pkg::set_jwt_error(exc.what());
				return false;
			}

			return true;
		}

		json header(std::string& token)
		{

			auto _header = json::object();
			try
			{
				auto decoded = ::jwt::decode<kz_token>(token);

				for (auto &e : decoded.get_header_claims()) {
					const auto k = e.first;
					const auto v = e.second;
					_header[k] = v.to_json();
				}

			}
			catch(const std::invalid_argument& exc)
			{
				AMQP_DBG << "error validating token - " << exc.what() << " => " << token;
			}
			catch(const std::runtime_error& exc)
			{
				AMQP_DBG << "error validating token - " << exc.what() << " => " << token;
			}
			catch(const std::exception& exc)
			{
				AMQP_DBG << "error validating token - " << exc.what() << " => " << token;
			}

			return _header;
		}

		json payload(std::string& token)
		{
			auto _payload = json::object();

			try
			{
				auto decoded = ::jwt::decode<kz_token>(token);

				for (auto &e : decoded.get_payload_claims())
					_payload[e.first] = e.second.to_json();

			}
			catch(const std::invalid_argument& exc)
			{
				AMQP_DBG << "error validating token - " << exc.what() << " => " << token;
			}
			catch(const std::runtime_error& exc)
			{
				AMQP_DBG << "error validating token - " << exc.what() << " => " << token;
			}
			catch(const std::exception& exc)
			{
				AMQP_DBG << "error validating token - " << exc.what() << " => " << token;
			}

			return _payload;
		}

		/*
		void set_payload(std::string& token)
		{
			auto j = payload(token);
			pkg::set_last_json_result(j);
		}
		*/

		json token(std::string& token)
		{
			auto tok = json::object();
			auto _header = json::object();
			auto _payload = json::object();
			try
			{
				auto decoded = ::jwt::decode<kz_token>(token);

				tok["header"] = _header;
				tok["payload"] = _payload;
				tok["signature"] = decoded.get_signature();

				for (auto &e : decoded.get_header_claims())
					_header[e.first] = e.second.to_json();

				for (auto &e : decoded.get_payload_claims())
					_payload[e.first] = e.second.to_json();

			}
			catch(const std::invalid_argument& exc)
			{
				AMQP_DBG << "error validating token - " << exc.what() << " => " << token;
			}
			catch(const std::runtime_error& exc)
			{
				AMQP_DBG << "error validating token - " << exc.what() << " => " << token;
			}
			catch(const std::exception& exc)
			{
				AMQP_DBG << "error validating token - " << exc.what() << " => " << token;
			}

			return tok;
		}

		std::string signature(std::string& token)
		{
			std::string sig;

			try
			{
				auto decoded = ::jwt::decode<kz_token>(token);
				sig = decoded.get_signature();

			}
			catch(const std::invalid_argument& exc)
			{
				AMQP_DBG << "error validating token - " << exc.what() << " => " << token;
			}
			catch(const std::runtime_error& exc)
			{
				AMQP_DBG << "error validating token - " << exc.what() << " => " << token;
			}
			catch(const std::exception& exc)
			{
				AMQP_DBG << "error validating token - " << exc.what() << " => " << token;
			}

			return sig;
		}

		std::string create(std::string payload)
		{
			using sec = std::chrono::seconds;
			using min = std::chrono::minutes;

			json jobj;

			std::string kid, priv_pem, pub_pem, issuer, aud, scope;
			std::string token;

			try
			{
				jobj = json::parse(payload.c_str());

				kid = htable::fetch("jwt.keys", "jwt.id");
				priv_pem = htable::fetch("jwt.keys", "jwt.private");
				pub_pem = htable::fetch("jwt.keys", "jwt.public");
				issuer = htable::fetch("jwt.keys", "jwt.issuer");
				aud = htable::fetch("jwt.keys", "jwt.aud");
				scope = htable::fetch("jwt.keys", "jwt.scope");

				if (kid.empty()) {
					AMQP_ERR << "no key identifier";
					return token;
				}

				if (priv_pem.empty()) {
					AMQP_ERR << "no private key";
					return token;
				}

				if (issuer.empty()) {
					AMQP_DBG << "default issuer";
					issuer = "kazoo";
				}


				rsa key = rs256(pub_pem, priv_pem);

				const auto time = date::now();
				builder build;

				for(auto& item : jobj.items())
					build.set_payload_claim(item.key(), item.value());

				build.set_type("JWT")
					 .set_key_id(kid)
					 .set_issuer(issuer)
					 .set_issued_at(time);

				if (!aud.empty()) build.set_audience(aud);
				if (!scope.empty()) build.set_payload_claim("scope", claim(scope));

				token = build.sign(key);
			}
			catch(const std::exception& exc)
			{
				AMQP_ERR << "error parsing body - " << exc.what() << " => " << payload;
				return token;
			}

			return token;

		}

		void sign(const char *KeyId, const char *Pem)
		{
		}

	}

	namespace stirshaken
	{
		using json = nlohmann::json;
		using ecdsa = ::jwt::algorithm::ecdsa;
		using es256 = ::jwt::algorithm::es256;
		using rsa =  ::jwt::algorithm::rsa;
		using rs256 = ::jwt::algorithm::rs256;
		using builder = ::jwt::builder<kz_token>;
		using claim = ::jwt::basic_claim<kz_token>;
		using default_clock  = ::jwt::default_clock;
		using date = std::chrono::system_clock::time_point::clock;

		std::string create(std::string payload)
		{
			using sec = std::chrono::seconds;
			using min = std::chrono::minutes;

			json jobj;

			std::string kid, priv_pem, alg, x5u;
			std::string token;

			try
			{
				jobj = json::parse(payload.c_str());
				sr_xavp_t *options = xavp::get("stirshaken.options");
				sr_xavp_t *option;

				if (options == NULL || options->val.type != SR_XTYPE_XAVP || options->val.v.xavp == NULL) {
					AMQP_ERR << "no stirshaken.options xavp";
					return token;
				}

				options = options->val.v.xavp;

				if ((option = xavp::get("kid", &options)) != NULL && option->val.type == SR_XTYPE_STR) {
					kid = std::string(option->val.v.s.s, option->val.v.s.len);
				}

				if (kid.empty()) {
					AMQP_ERR << "no key identifier";
					return token;
				}

				priv_pem = htable::fetch("jwt.keys", kid + ".key");

				if (priv_pem.empty()) {
					AMQP_ERR << "no private key";
					return token;
				}

				alg = htable::fetch("jwt.keys", kid + ".alg");

				if (alg.empty()) {

					if ((option = xavp::get("alg", &options)) != NULL && option->val.type == SR_XTYPE_STR) {
						alg = std::string(option->val.v.s.s, option->val.v.s.len);
					}

					if (alg.empty()) {
						AMQP_ERR << "no alg";
						return token;
					}
				}

				if ((option = xavp::get("x5u", &options)) != NULL && option->val.type == SR_XTYPE_STR) {
					x5u = std::string(option->val.v.s.s, option->val.v.s.len);
				}

				if (x5u.empty()) {
					AMQP_ERR << "no x5u";
					return token;
				}

				const auto time = date::now();
				builder build;

				for(auto& item : jobj.items())
					build.set_payload_claim(item.key(), item.value());

				build.set_type("passport")
					 .set_header_claim("x5u", x5u)
					 .set_header_claim("ppt", "shaken")
					 .set_algorithm(alg)
					 .set_issued_at(time);

				if (alg.compare("ES256") == 0) {
					ecdsa key = es256("", priv_pem);
					token = build.sign(key);
				} else if (alg.compare("RS256") == 0) {
					rsa key = rs256("", priv_pem);
					token = build.sign(key);
				}
			}
			catch(const std::exception& exc)
			{
				AMQP_ERR << "error parsing body - " << exc.what() << " => " << payload;
				return token;
			}

			return token;

		}

	}
}


void kz_jwt_free_pv_value(pv_value_t *val )
{
	if(val->flags & PV_VAL_PKG)
		pkg_free(val->rs.s);
	else if(val->flags & PV_VAL_SHM)
		shm_free(val->rs.s);
}

void kz_jwt_destroy_pv_value(pv_value_t *val)
{
	kz_jwt_free_pv_value(val);
	pkg_free(val);
}

pv_value_t* kz_jwt_alloc_pv_value()
{
	pv_value_t* v = (pv_value_t*) pkg_malloc(sizeof(pv_value_t));
	if(v != NULL) {
		memset(v, 0, sizeof(pv_value_t));
	}
	return v;
}


KZ_EXTERN_C int kz_jwt_validate(sip_msg_t* msg, char* token_p)
{
	str token_s = STR_NULL;
	if (fixup_get_svalue(msg, (gparam_p)token_p, &token_s) != 0) {
		LM_ERR("cannot get payload value\n");
		return -1;
	}

	std::string token(token_s.s, token_s.len);
	return kz::jwt::is_valid(token) ? 1 : -1;

}

KZ_EXTERN_C int kz_is_jwt(sip_msg_t* msg, char* token_p)
{
	str token_s = STR_NULL;
	if (fixup_get_svalue(msg, (gparam_p)token_p, &token_s) != 0) {
		LM_ERR("cannot get payload value\n");
		return -1;
	}

	std::string token(token_s.s, token_s.len);
	return kz::jwt::is_jwt(token) ? 1 : -1;

}

KZ_EXTERN_C int kz_jwt_payload(sip_msg_t* msg, char* token_p)
{
	str token_s = STR_NULL;
	if (fixup_get_svalue(msg, (gparam_p)token_p, &token_s) != 0) {
		LM_ERR("cannot get payload value\n");
		return -1;
	}

	std::string token(token_s.s, token_s.len);

	if (!kz::jwt::is_jwt(token)) return -1;

	json jobj = kz::jwt::payload(token);
	kz::pkg::set_last_json_result(jobj);

	return 1;

}

KZ_EXTERN_C int kz_jwt_token(sip_msg_t* msg, char* token_p)
{
	str token_s = STR_NULL;
	if (fixup_get_svalue(msg, (gparam_p)token_p, &token_s) != 0) {
		LM_ERR("cannot get payload value\n");
		return -1;
	}

	std::string token(token_s.s, token_s.len);

	if (!kz::jwt::is_jwt(token)) return -1;

	json jobj = kz::jwt::token(token);
	kz::pkg::set_last_json_result(jobj);

	return 1;

}

KZ_EXTERN_C int kz_jwt_header(sip_msg_t* msg, char* token_p)
{
	str token_s = STR_NULL;
	if (fixup_get_svalue(msg, (gparam_p)token_p, &token_s) != 0) {
		LM_ERR("cannot get payload value\n");
		return -1;
	}

	std::string token(token_s.s, token_s.len);

	if (!kz::jwt::is_jwt(token)) return -1;

	json jobj = kz::jwt::header(token);
	kz::pkg::set_last_json_result(jobj);

	return 1;

}

enum _kz_tr_jwt_type { KZ_TR_NONE=0, KZ_TR_JWT };
enum _kz_tr_jwt_subtype { KZ_TR_JWT_NONE=0, KZ_TR_JWT_CREATE, KZ_TR_JWT_PAYLOAD, KZ_TR_JWT_HEADER, KZ_TR_JWT_STIRSHAKEN };

KZ_EXTERN_C int kz_jwt_tr_eval(struct sip_msg *msg, tr_param_t *tp, int subtype, pv_value_t *val)
{
	if(val==NULL || (val->flags&PV_VAL_NULL)) return -1;

	if (subtype == KZ_TR_JWT_CREATE) {

		if(!(val->flags&PV_VAL_STR))return -1;

		std::string claims(val->rs.s, val->rs.len);
		std::string token = kz::jwt::create(claims);

		if(token.empty()) {
			AMQP_ERR << "error creating token";
			return -1;
		}

		char * buffer = kz_tr_set_crt_buffer();
		strncpy(buffer, (char *) token.c_str(), (int) token.length());
		buffer[token.length()] = '\0';

		val->flags = PV_VAL_STR;
		val->ri = 0;
		val->rs.s = buffer;
		val->rs.len = token.length();

	} else if (subtype == KZ_TR_JWT_PAYLOAD) {

		std::string token(val->rs.s, val->rs.len);

		if (!kz::jwt::is_jwt(token)) {
			AMQP_ERR << "not a token";
			return -1;
		}

		json jobj = kz::jwt::payload(token);

		std::ostringstream jobjStream;
		jobjStream << jobj;

		std::string payload = jobjStream.str();

		char * buffer = kz_tr_set_crt_buffer();
		strncpy(buffer, (char *) payload.c_str(), (int) payload.length());
		buffer[payload.length()] = '\0';

		val->flags = PV_VAL_STR;
		val->ri = 0;
		val->rs.s = buffer;
		val->rs.len = token.length();

	} else if (subtype == KZ_TR_JWT_STIRSHAKEN) {

		if(!(val->flags&PV_VAL_STR))return -1;

		std::string payload(val->rs.s, val->rs.len);
		std::string token = kz::stirshaken::create(payload);

		if(token.empty()) {
			AMQP_ERR << "error creating token";
			return -1;
		}

		char * buffer = kz_tr_set_crt_buffer();
		strncpy(buffer, (char *) token.c_str(), (int) token.length());
		buffer[token.length()] = '\0';

		val->flags = PV_VAL_STR;
		val->ri = 0;
		val->rs.s = buffer;
		val->rs.len = token.length();

	}

	return 0;
}

KZ_EXTERN_C char* kz_jwt_tr_parse(str* in, trans_t *t)
{
	char *p;
	str name;

	if(in==NULL || t==NULL)
		return NULL;

	p = in->s;
	name.s = in->s;

	/* find next token */
	while(is_in_str(p, in) && *p!=TR_PARAM_MARKER && *p!=TR_RBRACKET) p++;
	if(*p=='\0')
	{
		LM_ERR("invalid transformation: %.*s\n", in->len, in->s);
		return NULL;
	}

	name.s = in->s;
	name.len = p - name.s;

	t->type = KZ_TR_JWT;
	t->trf = kz_jwt_tr_eval;
	t->name = name;

	if(name.len==6 && strncasecmp(name.s, "create", 6)==0) {
		t->subtype = KZ_TR_JWT_CREATE;
	} else if(name.len==7 && strncasecmp(name.s, "payload", 7)==0) {
		t->subtype = KZ_TR_JWT_PAYLOAD;
	} else if(name.len==10 && strncasecmp(name.s, "stirshaken", 10)==0) {
		t->subtype = KZ_TR_JWT_STIRSHAKEN;
	} else {
		return NULL;
	}


	return p;
}

KZ_EXTERN_C int kz_pv_get_rfc3339(struct sip_msg *msg, pv_param_t *param,	pv_value_t *res)
{
    using namespace date;
	using namespace std::chrono;

	const auto current_time = std::chrono::high_resolution_clock::now();
	std::string rfc3339 = format("%FT%TZ", time_point_cast<nanoseconds>(current_time));

	char * buffer = kz_tr_set_crt_buffer();
	strncpy(buffer, (char *) rfc3339.c_str(), (int) rfc3339.length());
	buffer[rfc3339.length()] = '\0';

	return pv_get_strzval(msg, param, res, buffer);
}

KZ_EXTERN_C int kz_pv_get_jwt_error(struct sip_msg *msg, pv_param_t *param,	pv_value_t *res)
{
	return pv_get_strzval(msg, param, res, _kz_jwt_error);
}
