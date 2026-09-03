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
#include "kz_sca_common.h"

#include "kz_sca_event.h"

str KZ_SCA_EVENT_NAME_CALL_INFO = STR_STATIC_INIT("call-info");
str KZ_SCA_EVENT_NAME_LINE_SEIZE = STR_STATIC_INIT("line-seize");

int kz_sca_event_from_str(str *event_str)
{
	int event = KZ_SCA_EVENT_TYPE_UNKNOWN;

	if(event_str == NULL || event_str->s == NULL) {
		return (KZ_SCA_EVENT_TYPE_UNKNOWN);
	}

	if(STR_EQ(*event_str, KZ_SCA_EVENT_NAME_CALL_INFO)) {
		event = KZ_SCA_EVENT_TYPE_CALL_INFO;
	} else if(STR_EQ(*event_str, KZ_SCA_EVENT_NAME_LINE_SEIZE)) {
		event = KZ_SCA_EVENT_TYPE_LINE_SEIZE;
	}

	return (event);
}

char *kz_sca_event_name_from_type(int event_type)
{
	switch(event_type) {
		case KZ_SCA_EVENT_TYPE_CALL_INFO:
			return (KZ_SCA_EVENT_NAME_CALL_INFO.s);

		case KZ_SCA_EVENT_TYPE_LINE_SEIZE:
			return (KZ_SCA_EVENT_NAME_LINE_SEIZE.s);

		default:
			break;
	}

	return ("unknown");
}

int kz_sca_event_append_header_for_type(int event_type, char *hdrbuf, int maxlen)
{
	int len;

	len = snprintf(hdrbuf, maxlen, "Event: %s%s",
			kz_sca_event_name_from_type(event_type), CRLF);
	if(len >= maxlen) {
		LM_ERR("%s Event header too long\n",
				kz_sca_event_name_from_type(event_type));
		return (-1);
	}

	return (len);
}
