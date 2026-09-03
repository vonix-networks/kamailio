#ifndef __KZ_LOG_H
#define __KZ_LOG_H

namespace kz
{

enum AMQPLogLevel : int {
	ALERT = L_ALERT
	,BUG = L_BUG
	,CRIT2 = L_CRIT2
	,CRIT = L_CRIT
	,ERR = L_ERR
	,WARN = L_WARN
	,NOTICE = L_NOTICE
	,INFO = L_INFO
	,DBG = L_DBG
	,TRACE = L_DBG + 1
	,VERBOSE = TRACE + 1
};

class AMQPLog
{
public:
	AMQPLog() :
		messageLevel(AMQPLogLevel::INFO)
		{};
	virtual ~AMQPLog();
	std::ostringstream& Get(AMQPLogLevel level = AMQPLogLevel::INFO, const char * loc = LOC_INFO, const char * fun = _FUNC_NAME_);

protected:
   std::ostringstream os;
private:
   AMQPLog(const AMQPLog&);
   AMQPLog& operator =(const AMQPLog&);
   static std::mutex mtx;
private:
   AMQPLogLevel messageLevel;
};

}


#define AMQP_LOG(level) \
if (level > cfg_get(kz_amqp, kz_amqp_cfg, log_level)) ; \
else kz::AMQPLog().Get(level, LOC_INFO, _FUNC_NAME_)

#define AMQP_VERBOSE AMQP_LOG(kz::AMQPLogLevel::VERBOSE)
#define AMQP_TRACE AMQP_LOG(kz::AMQPLogLevel::TRACE)
#define AMQP_DBG AMQP_LOG(kz::AMQPLogLevel::DBG)
#define AMQP_INFO AMQP_LOG(kz::AMQPLogLevel::INFO)
#define AMQP_NOTICE AMQP_LOG(kz::AMQPLogLevel::NOTICE)
#define AMQP_WARN AMQP_LOG(kz::AMQPLogLevel::WARN)
#define AMQP_ERR AMQP_LOG(kz::AMQPLogLevel::ERR)
#define AMQP_CRIT AMQP_LOG(kz::AMQPLogLevel::CRIT)
#define AMQP_CRIT2 AMQP_LOG(kz::AMQPLogLevel::CRIT2)
#define AMQP_BUG AMQP_LOG(kz::AMQPLogLevel::BUG)
#define AMQP_ALERT AMQP_LOG(kz::AMQPLogLevel::ALERT)

#		define LOG___(facility, level, lname, prefix, fmt, args...) \
			do { \
				if (DPRINT_NON_CRIT) { \
					int __llevel; \
					__llevel = ((level)<L_ALERT)?L_ALERT:(((level)>L_DBG)?L_DBG:level); \
					DPRINT_CRIT_ENTER; \
					if (unlikely(log_stderr)) { \
						if (unlikely(log_color)) dprint_color(__llevel); \
						if(unlikely(log_prefix_val)) { \
							fprintf(stderr, "%2d(%d) %s: %.*s%s" fmt, \
								process_no, my_pid(), \
								(lname)?(lname):LOG_LEVEL2NAME(__llevel), \
								log_prefix_val->len, log_prefix_val->s, \
								(prefix) , ## args);\
						} else { \
							fprintf(stderr, "%2d(%d) %s: %s" fmt, \
								process_no, my_pid(), \
								(lname)?(lname):LOG_LEVEL2NAME(__llevel), \
								(prefix) , ## args);\
						} \
						if (unlikely(log_color)) dprint_color_reset(); \
					} else { \
						if(unlikely(log_prefix_val)) { \
							_km_log_func(LOG2SYSLOG_LEVEL(__llevel) |\
							   (((facility) != DEFAULT_FACILITY) ? \
								(facility) : \
								get_debug_facility(LOG_MNAME, LOG_MNAME_LEN)), \
								"%s: %.*s%s" fmt,\
								(lname)?(lname):LOG_LEVEL2NAME(__llevel),\
								log_prefix_val->len, log_prefix_val->s, \
								(prefix) , ## args); \
						} else { \
							_km_log_func(LOG2SYSLOG_LEVEL(__llevel) |\
							   (((facility) != DEFAULT_FACILITY) ? \
								(facility) : \
								get_debug_facility(LOG_MNAME, LOG_MNAME_LEN)), \
								"%s: %s" fmt,\
								(lname)?(lname):LOG_LEVEL2NAME(__llevel),\
								(prefix) , ## args); \
						} \
					} \
					DPRINT_CRIT_EXIT; \
				} \
			} while(0)


#		define KZ_LOG_FX(facility, level, lname, prefix, funcname, fmt, args...) \
			do { \
				if (DPRINT_NON_CRIT) { \
					int __llevel; \
					__llevel = ((level)<L_ALERT)?L_ALERT:(((level)>L_DBG)?L_DBG:level); \
					DPRINT_CRIT_ENTER; \
					if (_ksr_slog_func) { /* structured logging */ \
						ksr_logdata_t __kld = {0}; \
						__kld.v_facility = LOG2SYSLOG_LEVEL(__llevel) | \
							   (((facility) != DEFAULT_FACILITY) ? \
								(facility) : \
								get_debug_facility(LOG_MNAME, LOG_MNAME_LEN)); \
						__kld.v_level = __llevel; \
						__kld.v_lname = (lname)?(lname):LOG_LEVEL2NAME(__llevel); \
						__kld.v_fname = __FILE__; \
						__kld.v_fline = __LINE__; \
						__kld.v_mname = LOG_MNAME; \
						__kld.v_func = LOGV_FUNCNAME_STR(funcname); \
						__kld.v_locinfo = prefix; \
						_ksr_slog_func(&__kld, fmt, ## args); \
					} else { /* classic logging */ \
						if (unlikely(log_stderr)) { \
							if (unlikely(log_color)) dprint_color(__llevel); \
							fprintf(stderr, "%2d(%d) %s: %.*s%s%s%s" fmt, \
								process_no, my_pid(), \
								(lname)?(lname):LOG_LEVEL2NAME(__llevel), \
								LOGV_PREFIX_LEN, LOGV_PREFIX_STR, \
								(prefix), LOGV_FUNCNAME_STR(funcname), \
								LOGV_FUNCSUFFIX_STR(funcname), ## args); \
							if (unlikely(log_color)) dprint_color_reset(); \
						} else { \
							_km_log_func(LOG2SYSLOG_LEVEL(__llevel) | \
							   (((facility) != DEFAULT_FACILITY) ? \
								(facility) : \
								get_debug_facility(LOG_MNAME, LOG_MNAME_LEN)), \
								"%s: %.*s%s%s%s" fmt, \
								(lname)?(lname):LOG_LEVEL2NAME(__llevel), \
								LOGV_PREFIX_LEN, LOGV_PREFIX_STR, \
								(prefix), LOGV_FUNCNAME_STR(funcname), \
								LOGV_FUNCSUFFIX_STR(funcname), ## args); \
						} \
					} \
					DPRINT_CRIT_EXIT; \
				} \
			} while(0)

#endif
