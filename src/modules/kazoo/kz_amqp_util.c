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


#include "kz_amqp_util.h"
#include "defs.h"

extern int log_stderr;


int set_non_blocking(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL);
	if (flags < 0)
		return flags;
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0)
		return -1;

	return 0;
}

#define KEY_SAFE(C)  ((C >= 'a' && C <= 'z') || \
                      (C >= 'A' && C <= 'Z') || \
                      (C >= '0' && C <= '9') || \
                      (C == '-' || C == '~'  || C == '_'))

#define HI4(C) (C>>4)
#define LO4(C) (C & 0x0F)

#define hexint(C) (C < 10?('0' + C):('A'+ C - 10))

void kz_amqp_util_encode_string(const char * key, char *pdest) {
    char *p, *end;
	char *dest = pdest;
	int len = strlen(key);
    if ((len == 1) && (key[0] == '#' || key[0] == '*')) {
    	*dest++ = key[0];
        *dest = '\0';
    	return;
    }
    for (p = (char*)key, end = (char*)key + len; p < end && ((dest - pdest) < MAX_ROUTING_KEY_SIZE); p++) {
		if (KEY_SAFE(*p)) {
			*dest++ = *p;
		} else if (*p == '.') {
			memcpy(dest, "\%2E", 3);
			dest += 3;
		} else if (*p == ' ') {
			*dest++ = '+';
		} else {
			*dest++ = '%';
			sprintf(dest, "%c%c", hexint(HI4(*p)), hexint(LO4(*p)));
			dest += 2;
		}
    }
    *dest = '\0';
}

void kz_amqp_util_encode(const str * key, char *pdest) {
    char *p, *end;
	char *dest = pdest;
    if ((key->len == 1) && (key->s[0] == '#' || key->s[0] == '*')) {
    	*dest++ = key->s[0];
    	return;
    }
    for (p = key->s, end = key->s + key->len; p < end && ((dest - pdest) < MAX_ROUTING_KEY_SIZE); p++) {
		if (KEY_SAFE(*p)) {
			*dest++ = *p;
		} else if (*p == '.') {
			memcpy(dest, "\%2E", 3);
			dest += 3;
		} else if (*p == ' ') {
			*dest++ = '+';
		} else {
			*dest++ = '%';
			sprintf(dest, "%c%c", hexint(HI4(*p)), hexint(LO4(*p)));
			dest += 2;
		}
    }
    *dest = '\0';
}

int kz_amqp_encode_ex(str* unencoded, pv_value_p dst_val)
{
	char routing_key_buff[MAX_ROUTING_KEY_SIZE+1];
	memset(routing_key_buff,0, sizeof(routing_key_buff));
	kz_amqp_util_encode(unencoded, routing_key_buff);

	int len = strlen(routing_key_buff);
	dst_val->rs.s = pkg_malloc(len+1);
	memcpy(dst_val->rs.s, routing_key_buff, len);
	dst_val->rs.s[len] = '\0';
	dst_val->rs.len = len;
	dst_val->flags = PV_VAL_STR | PV_VAL_PKG;

	return 1;

}

int kz_amqp_encode(struct sip_msg* msg, char* unencoded, char* encoded)
{
    str unencoded_s;
	pv_spec_t *dst_pv;
	pv_value_t dst_val;
	dst_pv = (pv_spec_t *)encoded;

	if (fixup_get_svalue(msg, (gparam_p)unencoded, &unencoded_s) != 0) {
		LM_ERR("cannot get unencoded string value\n");
		return -1;
	}

	if (unencoded_s.len > MAX_ROUTING_KEY_SIZE) {
		LM_ERR("routing_key size (%d) > max %d\n", unencoded_s.len, MAX_ROUTING_KEY_SIZE);
		return -1;
	}

	kz_amqp_encode_ex(&unencoded_s, &dst_val);
	dst_pv->setf(msg, &dst_pv->pvp, (int)EQ_T, &dst_val);

	if(dst_val.flags & PV_VAL_PKG)
		pkg_free(dst_val.rs.s);
	else if(dst_val.flags & PV_VAL_SHM)
		shm_free(dst_val.rs.s);


	return 1;

}
