#ifndef _DYNAMIC_LIMITS_CONFIG_H
#define _DYNAMIC_LIMITS_CONFIG_H

#include <time.h>
#include "slurm/slurm.h"
#include "src/common/list.h"

typedef struct {
	char *partition_name;
	float adjustment_threshold;
	float adjustment_rate;
	int cooldown_minutes;
	time_t last_adjustment_time;
} partition_config_t;

typedef struct {
	List partition_configs;
} dynamic_limits_config_t;

extern dynamic_limits_config_t dynamic_limits_config;

extern void dynamic_limits_config_load(void);
extern void dynamic_limits_config_destroy(void);

#endif