#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"
#include "src/common/parse_config.h"
#include "dynamic_limits_config.h"

dynamic_limits_config_t dynamic_limits_config;

static void _destroy_partition_config(void *data)
{
	partition_config_t *config = (partition_config_t *)data;
	xfree(config->partition_name);
	xfree(config);
}

void dynamic_limits_config_load(void)
{
	char *config_str, *tmp_str, *token, *save_ptr = NULL;
	partition_config_t *part_config;

	dynamic_limits_config.partition_configs = list_create(_destroy_partition_config);

	if ((config_str = slurm_get_job_submit_plugins_partition_config())) {
		tmp_str = xstrdup(config_str);
		token = strtok_r(tmp_str, ",", &save_ptr);
		while (token != NULL) {
			char *part_name, *threshold_str, *rate_str, *cooldown_str;
			part_config = xmalloc(sizeof(partition_config_t));

			if (parse_option_name_value_pair(token, ":",
							 &part_name,
							 &threshold_str) == 0) {
				part_config->partition_name = xstrdup(part_name);

				rate_str = strchr(threshold_str, ':');
				if (rate_str) {
					*rate_str = '\0';
					rate_str++;
					cooldown_str = strchr(rate_str, ':');
					if (cooldown_str) {
						*cooldown_str = '\0';
						cooldown_str++;
						part_config->cooldown_minutes = atoi(cooldown_str);
					} else {
						part_config->cooldown_minutes = 15; /* Default 15 minutes */
					}
					part_config->adjustment_rate = atof(rate_str) / 100.0;
				} else {
					part_config->adjustment_rate = 0.10; /* Default 10% */
					part_config->cooldown_minutes = 15; /* Default 15 minutes */
				}
				
				part_config->adjustment_threshold = atof(threshold_str) / 100.0;
				part_config->last_adjustment_time = 0;
			} else {
				error("Invalid partition config format: %s", token);
				xfree(part_config);
				token = strtok_r(NULL, ",", &save_ptr);
				continue;
			}

			list_append(dynamic_limits_config.partition_configs, part_config);
			token = strtok_r(NULL, ",", &save_ptr);
		}
		xfree(tmp_str);
		xfree(config_str);
	}

	/* Add default config if no partitions were specified */
	if (list_is_empty(dynamic_limits_config.partition_configs)) {
		part_config = xmalloc(sizeof(partition_config_t));
		part_config->partition_name = xstrdup("DEFAULT");
		part_config->adjustment_threshold = 0.95; /* Default 95% */
		part_config->adjustment_rate = 0.10; /* Default 10% */
		part_config->cooldown_minutes = 15; /* Default 15 minutes */
		part_config->last_adjustment_time = 0;
		list_append(dynamic_limits_config.partition_configs, part_config);
	}
}

void dynamic_limits_config_destroy(void)
{
	if (dynamic_limits_config.partition_configs) {
		list_destroy(dynamic_limits_config.partition_configs);
		dynamic_limits_config.partition_configs = NULL;
	}
}