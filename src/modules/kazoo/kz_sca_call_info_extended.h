#ifndef KZ_SCA_CALL_INFO_EXTENDED_H
#define KZ_SCA_CALL_INFO_EXTENDED_H

#include "kz_sca.h"
#include "kz_sca_subscribe.h"
#include "kz_json.h"

typedef enum {
	KZ_PRESENCE_DIRECTION_NONE = 0,
	KZ_PRESENCE_DIRECTION_INITIATOR = 1,
	KZ_PRESENCE_DIRECTION_RECIPIENT = 2
} kz_sca_presence_direction_enum_t;
typedef uint32_t kz_sca_presence_direction_t;

void kz_sca_presence_direction_to_str(kz_sca_presence_direction_t direction, str *);
const char * kz_sca_presence_direction_to_string(kz_sca_presence_direction_t direction);
kz_sca_presence_direction_t kz_sca_presence_direction_from_string(const char * direction);
kz_sca_presence_direction_t kz_sca_presence_direction_from_json(json_t *json);

int kz_sca_extended_call_info_update(sip_msg_t *msg);

int kz_sca_pv_parse_call_info_name(pv_spec_p sp, str *in);
int kz_sca_pv_get_call_info(sip_msg_t *msg, pv_param_t *param, pv_value_t *res);

#endif // KZ_SCA_CALL_INFO_EXTENDED_H
