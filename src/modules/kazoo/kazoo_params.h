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

#ifndef KZ_AMQP_PARAMS_H_
#define KZ_AMQP_PARAMS_H_


extern int dbk_channels;
extern str dbk_node_hostname;
extern str dbk_node_name;
extern str dbk_consumer_event_key;
extern str dbk_consumer_event_subkey;
extern int dbk_consumer_workers;
extern int dbk_consumer_processes;
extern str dbk_default_worker_name_template;
extern int dbk_use_heartbeats;
extern str kz_xavp_status;
extern int kz_enable_clock_diff_warning;
extern int dbk_connect_timeout;

struct cfg_group_kz_presence {
	int	include_entity;
	int	use_full_entity;
	int max_dialoginfo_xml_body_size;
	int max_presence_xml_body_size;
	int max_mwi_xml_body_size;
};
extern struct cfg_group_kz_presence	default_kz_presence_cfg;
extern void	*kz_presence_cfg;


struct cfg_group_kz_amqp {
	int	log_level;
};
extern struct cfg_group_kz_amqp	default_kz_amqp_cfg;
extern void	*kz_amqp_cfg;

struct cfg_group_kz_relay {
	int	active;
};
extern struct cfg_group_kz_relay	default_kz_relay_cfg;
extern void	*kz_relay_cfg;


#endif /* KZ_AMQP_H_ */

