#ifndef KZ_SCA_PARAMS_H_
#define KZ_SCA_PARAMS_H_

struct cfg_group_kz_sca {
	int db_update_interval;
	int hash_table_size;
	int call_info_max_expires;
	int line_seize_max_expires;
	int purge_expired_interval;
	str subs_table;
	str state_table;
	str outbound_proxy;
	str server_address;
	str endpoint_spec;
	str aliases_view;
	str aliases_path;
	str realm_path;
	int process_no_subscribers;
	int	log_level;
	int use_next_available;
};
extern struct cfg_group_kz_sca	default_kz_sca_cfg;
extern void	*kz_sca_cfg;


#endif /* KZ_SCA_PARAMS_H_ */

