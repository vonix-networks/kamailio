#ifndef __KZ_JWT__
#define __KZ_JWT__

#include "kz.h"

KZ_BEGIN_EXTERN_C

#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include "../../core/str_hash.hxx"
#include "../../core/sr_module.h"
#include "../../core/pvar.h"
#include "../../core/mod_fix.h"
#include "../../core/parser/msg_parser.h"
#include "../../core/str.h"
#include "../../core/dprint.h"
#include "../../core/lock_ops.h"
#include "../../core/xavp.h"
#include "../../core/hashes.h"
#include "../../core/cfg/cfg_struct.h"
#include "../../core/mem/mem.h"
#include "../../core/locking.h"
#include "../../core/mem/shm.h"

#include "../htable/ht_api.h"
#include "../htable/api.h"

#include "kazoo_params.h"
#include "kz_amqp_mem.h"

int kz_jwt_validate(sip_msg_t* msg, char* token_p);
int kz_is_jwt(sip_msg_t* msg, char* token_p);
int kz_jwt_token(sip_msg_t* msg, char* token_p);
int kz_jwt_payload(sip_msg_t* msg, char* token_p);
int kz_jwt_header(sip_msg_t* msg, char* token_p);

int kz_jwt_tr_eval(struct sip_msg *msg, tr_param_t *tp, int subtype, pv_value_t *val);
char* kz_jwt_tr_parse(str* in, trans_t *t);

int kz_pv_get_rfc3339(struct sip_msg *msg, pv_param_t *param,	pv_value_t *res);

int kz_pv_get_jwt_error(struct sip_msg *msg, pv_param_t *param, pv_value_t *res);

KZ_END_EXTERN_C

#endif
