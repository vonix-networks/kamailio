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
#ifndef KZ_SCA_DIALOG_H
#define KZ_SCA_DIALOG_H

struct _kz_sca_dialog
{
	str id; // call-id + from-tag + to-tag
	str call_id;
	str from_tag;
	str to_tag;

	int notify_cseq;
	int subscribe_cseq;
};
typedef struct _kz_sca_dialog kz_sca_dialog;

#define KZ_SCA_DIALOG_EMPTY(d)                                                   \
	((d) == NULL                                                              \
			|| (KZ_SCA_STR_EMPTY(&(d)->call_id) && KZ_SCA_STR_EMPTY(&(d)->from_tag) \
					   && KZ_SCA_STR_EMPTY(&(d)->to_tag)))

int kz_sca_dialog_build_from_tags(kz_sca_dialog *, int, str *, str *, str *);
int kz_sca_dialog_create_replaces_header(kz_sca_dialog *, str *);

#endif // KZ_SCA_DIALOG_H
