#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>
#include "src/common/node_select.h"
#include "src/slurmctld/slurmctld.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/slurmctld/acct_policy.h"
#include "src/common/xstring.h"
#include "dynamic_limits_config.h"

const char plugin_name[] = "Dynamic limits job submit plugin";
const char plugin_type[] = "job_submit/dynamic_limits";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static double _get_partition_idle_resource_ratio(struct part_record *part_ptr)
{
	uint64_t total_cpus = 0, idle_cpus = 0;
	struct node_record *node_ptr;
	int i;

	for (i = 0; i < node_record_count; i++) {
		node_ptr = &node_record_table_ptr[i];
		if (!bit_test(part_ptr->node_bitmap, i))
			continue;
		if (IS_NODE_DOWN(node_ptr))
			continue;
		
		total_cpus += node_ptr->config_ptr->cpus;
		if (IS_NODE_IDLE(node_ptr)) {
			idle_cpus += node_ptr->config_ptr->cpus;
		} else {
			idle_cpus += node_ptr->config_ptr->cpus - node_ptr->cpus_alloc;
		}
	}

	return (double)idle_cpus / total_cpus;
}

static partition_config_t *_get_partition_config(char *partition_name)
{
	ListIterator iter;
	partition_config_t *part_config;

	iter = list_iterator_create(dynamic_limits_config.partition_configs);
	while ((part_config = list_next(iter))) {
		if (xstrcmp(part_config->partition_name, partition_name) == 0) {
			list_iterator_destroy(iter);
			return part_config;
		}
	}
	list_iterator_destroy(iter);

	/* Return default config if no match found */
	iter = list_iterator_create(dynamic_limits_config.partition_configs);
	while ((part_config = list_next(iter))) {
		if (xstrcmp(part_config->partition_name, "DEFAULT") == 0) {
			list_iterator_destroy(iter);
			return part_config;
		}
	}
	list_iterator_destroy(iter);

	return NULL;
}

static void _adjust_account_qos_limits(struct part_record *part_ptr,
				       bool increase, float adjustment_rate)
{
	ListIterator itr;
	slurmdb_assoc_rec_t *assoc;
	slurmdb_qos_rec_t *qos;
	float adjustment_factor = increase ? 1.0 + adjustment_rate :
					     1.0 - adjustment_rate;

	assoc_mgr_lock_t locks = { .assoc = WRITE_LOCK, .qos = WRITE_LOCK };
	assoc_mgr_lock(&locks);

	itr = list_iterator_create(assoc_mgr_assoc_list);
	while ((assoc = list_next(itr))) {
		if (assoc->partition &&
		    xstrcmp(assoc->partition, part_ptr->name) != 0)
			continue;
		if (assoc->grp_tres_ctld) {
			int64_t *cpu_limit = &assoc->grp_tres_ctld[TRES_ARRAY_CPU];
			if (*cpu_limit == INFINITE64)
				continue;

			*cpu_limit = (int64_t)(*cpu_limit * adjustment_factor);
			if (*cpu_limit < 1)
				*cpu_limit = 1;
		}
	}
	list_iterator_destroy(itr);

	itr = list_iterator_create(assoc_mgr_qos_list);
	while ((qos = list_next(itr))) {
		if (qos->grp_tres_ctld) {
			int64_t *cpu_limit = &qos->grp_tres_ctld[TRES_ARRAY_CPU];
			if (*cpu_limit == INFINITE64)
				continue;

			*cpu_limit = (int64_t)(*cpu_limit * adjustment_factor);
			if (*cpu_limit < 1)
				*cpu_limit = 1;
		}
	}
	list_iterator_destroy(itr);

	assoc_mgr_unlock(&locks);

	acct_storage_g_reconfig(acct_db_conn, 0);
}

static void _adjust_limits_based_on_idle_resources(struct part_record *part_ptr)
{
	double idle_ratio = _get_partition_idle_resource_ratio(part_ptr);
	float utilization = 1.0 - idle_ratio;
	partition_config_t *part_config = _get_partition_config(part_ptr->name);
	time_t current_time = time(NULL);

	if (!part_config) {
		error("No configuration found for partition %s", part_ptr->name);
		return;
	}

	/* Check if we're still in the cooldown period */
	if (difftime(current_time, part_config->last_adjustment_time) 
	    part_config->cooldown_minutes * 60) {
		debug3("Skipping limit adjustment for partition %s due to cooldown period",
		       part_ptr->name);
		return;
	}

	if (utilization > part_config->adjustment_threshold) {
		_adjust_account_qos_limits(part_ptr, false,
					   part_config->adjustment_rate);
		part_config->last_adjustment_time = current_time;
	} else if (utilization < (part_config->adjustment_threshold - 0.05)) {
		_adjust_account_qos_limits(part_ptr, true,
					   part_config->adjustment_rate);
		part_config->last_adjustment_time = current_time;
	}
}

extern int init(void)
{
	dynamic_limits_config_load();
	return SLURM_SUCCESS;
}

extern int fini(void)
{
	dynamic_limits_config_destroy();
	return SLURM_SUCCESS;
}

extern int job_submit(struct job_descriptor *job_desc, uint32_t submit_uid,
		      char **err_msg)
{
	struct part_record *part_ptr;

	if (job_desc->partition) {
		part_ptr = find_part_record(job_desc->partition);
		if (part_ptr) {
			_adjust_limits_based_on_idle_resources(part_ptr);
		}
	} else {
		ListIterator iter = list_iterator_create(part_list);
		while ((part_ptr = list_next(iter))) {
			_adjust_limits_based_on_idle_resources(part_ptr);
		}
		list_iterator_destroy(iter);
	}

	return SLURM_SUCCESS;
}

extern int job_modify(struct job_descriptor *job_desc,
		      struct job_record *job_ptr, uint32_t submit_uid)
{
	return SLURM_SUCCESS;
}