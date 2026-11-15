// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 The LineageOS Project
 *
 * The fuel gauge.  See include/mca/platform/platform_fg_ic_ops.h.
 */

#include <linux/errno.h>
#include <linux/rwsem.h>
#include <linux/string.h>
#include <mca/platform/platform_fg_ic_ops.h>
#include <linux/module.h>
#include <linux/platform_device.h>

/**
 * struct fuelguage_info - one registered gauge
 * @data: handed back to every call
 * @ops:  what the gauge driver provides
 */
struct fuelguage_info {
	void *data;
	const struct fuelguage_ic_ops *ops;
	struct fuelguage_ic_ops copy;
	const struct fuelguage_ic_ops *key;
};
static struct fuelguage_info g_fg_ic_ops[FG_IC_MAX];
static DECLARE_RWSEM(fg_ops_lock);

int platform_fg_ic_ops_register(enum fg_ic_role ic_role, void *data,
				const struct fuelguage_ic_ops *platform_fg_ops)
{
	struct fuelguage_info *p;
	int ret = 0;

	if ((unsigned int)ic_role >= FG_IC_MAX || !data || !platform_fg_ops)
		return -EINVAL;
	down_write(&fg_ops_lock);
	p = &g_fg_ic_ops[ic_role];
	if (p->ops) {
		ret = -EBUSY;
	} else {
		p->copy = *platform_fg_ops;
		p->data = data;
		p->key = platform_fg_ops;
		p->ops = &p->copy;
	}
	up_write(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ic_ops_register);

int platform_fg_ic_ops_unregister(enum fg_ic_role ic_role, void *data,
				  const struct fuelguage_ic_ops *ops)
{
	struct fuelguage_info *p;
	int ret = 0;

	if ((unsigned int)ic_role >= FG_IC_MAX || !data || !ops)
		return -EINVAL;
	down_write(&fg_ops_lock);
	p = &g_fg_ic_ops[ic_role];
	if (p->key != ops || p->data != data)
		ret = -ENOENT;
	else
		memset(p, 0, sizeof(*p));
	up_write(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(platform_fg_ic_ops_unregister);

/* Caller holds fg_ops_lock for the entire operation. */
static struct fuelguage_info *fg_ic_lookup(enum fg_ic_role ic_role)
{
	if ((unsigned int)ic_role >= FG_IC_MAX || !g_fg_ic_ops[ic_role].ops)
		return NULL;
	return &g_fg_ic_ops[ic_role];
}

int platform_fg_ops_probe_ok(enum fg_ic_role ic_role, bool *probe_ok)
{
	struct fuelguage_info *p;
	int ret;

	if (!probe_ok)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_probe_ok) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_probe_ok(p->data, probe_ok);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_probe_ok);

int platform_fg_ops_get_batt_info(enum fg_ic_role ic_role, void *batt_info)
{
	struct fuelguage_info *p;
	int ret;

	if (!batt_info)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_batt_info) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_batt_info(p->data, batt_info);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_batt_info);

int platform_fg_ops_get_soc(enum fg_ic_role ic_role)
{
	struct fuelguage_info *p;
	int ret;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_soc) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_soc(p->data);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_soc);

int platform_fg_ops_get_rsoc(enum fg_ic_role ic_role, int *rsoc)
{
	struct fuelguage_info *p;
	int ret;

	if (!rsoc)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_rsoc) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_rsoc(p->data, rsoc);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_rsoc);

int platform_fg_ops_get_curr(enum fg_ic_role ic_role, int *curr)
{
	struct fuelguage_info *p;
	int ret;

	if (!curr)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_curr) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_curr(p->data, curr);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_curr);

int platform_fg_ops_get_volt(enum fg_ic_role ic_role, int *volt)
{
	struct fuelguage_info *p;
	int ret;

	if (!volt)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_volt) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_volt(p->data, volt);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_volt);

int platform_fg_ops_set_temp(enum fg_ic_role ic_role, int temp)
{
	struct fuelguage_info *p;
	int ret;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_set_temp) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_set_temp(p->data, temp);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_set_temp);

int platform_fg_ops_get_temp(enum fg_ic_role ic_role, int *temp)
{
	struct fuelguage_info *p;
	int ret;

	if (!temp)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_temp) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_temp(p->data, temp);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_temp);

int platform_fg_ops_get_original_temp(enum fg_ic_role ic_role,
				      int *original_temp)
{
	struct fuelguage_info *p;
	int ret;

	if (!original_temp)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_original_temp) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_original_temp(p->data, original_temp);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_original_temp);

int platform_fg_ops_set_iterm(enum fg_ic_role ic_role, int iterm)
{
	struct fuelguage_info *p;
	int ret;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_set_iterm) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_set_iterm(p->data, iterm);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_set_iterm);

int platform_fg_ops_get_charge_status(enum fg_ic_role ic_role)
{
	struct fuelguage_info *p;
	int ret;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_charge_status) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_charge_status(p->data);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_charge_status);

int platform_fg_ops_get_rm(enum fg_ic_role ic_role, int *rm)
{
	struct fuelguage_info *p;
	int ret;

	if (!rm)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_rm) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_rm(p->data, rm);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_rm);

int platform_fg_ops_get_fastcharge(enum fg_ic_role ic_role, int *ffc)
{
	struct fuelguage_info *p;
	int ret;

	if (!ffc)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_fastcharge) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_fastcharge(p->data, ffc);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_fastcharge);

int platform_fg_ops_set_fastcharge(enum fg_ic_role ic_role, bool enable)
{
	struct fuelguage_info *p;
	int ret;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_set_fastcharge) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_set_fastcharge(p->data, enable);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_set_fastcharge);

int platform_fg_ops_get_chg_vol(enum fg_ic_role ic_role, int *volt)
{
	struct fuelguage_info *p;
	int ret;

	if (!volt)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_chg_vol) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_chg_vol(p->data, volt);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_chg_vol);

int platform_fg_ops_get_chip_ok(enum fg_ic_role ic_role, int *chip_ok)
{
	struct fuelguage_info *p;
	int ret;

	if (!chip_ok)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_chip_ok) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_chip_ok(p->data, chip_ok);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_chip_ok);

int platform_fg_ops_get_cyclecount(enum fg_ic_role ic_role, int *cyclecount)
{
	struct fuelguage_info *p;
	int ret;

	if (!cyclecount)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_cyclecount) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_cyclecount(p->data, cyclecount);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_cyclecount);

int platform_fg_ops_get_tte(enum fg_ic_role ic_role, int *tte)
{
	struct fuelguage_info *p;
	int ret;

	if (!tte)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_tte) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_tte(p->data, tte);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_tte);

int platform_fg_ops_get_ttf(enum fg_ic_role ic_role, int *ttf)
{
	struct fuelguage_info *p;
	int ret;

	if (!ttf)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_ttf) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_ttf(p->data, ttf);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_ttf);

int platform_fg_ops_get_fcc(enum fg_ic_role ic_role, int *fcc)
{
	struct fuelguage_info *p;
	int ret;

	if (!fcc)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_fcc) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_fcc(p->data, fcc);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_fcc);

int platform_fg_ops_get_full_design(enum fg_ic_role ic_role, int *full_design)
{
	struct fuelguage_info *p;
	int ret;

	if (!full_design)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_full_design) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_full_design(p->data, full_design);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_full_design);

int platform_fg_ops_get_decimal_rate(enum fg_ic_role ic_role, int *decimal_rate)
{
	struct fuelguage_info *p;
	int ret;

	if (!decimal_rate)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_decimal_rate) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_decimal_rate(p->data, decimal_rate);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_decimal_rate);

int platform_fg_ops_get_decimal(enum fg_ic_role ic_role, int *decimal)
{
	struct fuelguage_info *p;
	int ret;

	if (!decimal)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_decimal) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_decimal(p->data, decimal);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_decimal);

int platform_fg_ops_get_soh(enum fg_ic_role ic_role, int *soh)
{
	struct fuelguage_info *p;
	int ret;

	if (!soh)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_soh) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_soh(p->data, soh);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_soh);

int platform_fg_ops_get_temp_max(enum fg_ic_role ic_role, int *temp_max)
{
	struct fuelguage_info *p;
	int ret;

	if (!temp_max)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_temp_max) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_temp_max(p->data, temp_max);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_temp_max);

int platform_fg_ops_get_time_ot(enum fg_ic_role ic_role, int *time_ot)
{
	struct fuelguage_info *p;
	int ret;

	if (!time_ot)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_time_ot) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_time_ot(p->data, time_ot);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_time_ot);

int platform_fg_ops_get_batt_cell_info(enum fg_ic_role ic_role,
				       const char **batt_cell_info)
{
	struct fuelguage_info *p;
	int ret;

	if (!batt_cell_info)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_batt_cell_info) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_batt_cell_info(p->data, batt_cell_info);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_batt_cell_info);

int platform_fg_ops_set_verify_digest(enum fg_ic_role ic_role,
				      char *verify_digest)
{
	struct fuelguage_info *p;
	int ret;

	if (!verify_digest)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_set_verify_digest) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_set_verify_digest(p->data, verify_digest);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_set_verify_digest);

int platform_fg_ops_get_verify_digest(enum fg_ic_role ic_role,
				      char *verify_digest)
{
	struct fuelguage_info *p;
	int ret;

	if (!verify_digest)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_verify_digest) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_verify_digest(p->data, verify_digest);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_verify_digest);

int platform_fg_ops_set_authentic(enum fg_ic_role ic_role, int authentic)
{
	struct fuelguage_info *p;
	int ret;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_set_authentic) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_set_authentic(p->data, authentic);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_set_authentic);

int platform_fg_ops_get_authentic(enum fg_ic_role ic_role, int *authentic)
{
	struct fuelguage_info *p;
	int ret;

	if (!authentic)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_authentic) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_authentic(p->data, authentic);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_authentic);

int platform_fg_ops_get_error_state(enum fg_ic_role ic_role, bool *error_state)
{
	struct fuelguage_info *p;
	int ret;

	if (!error_state)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_error_state) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_error_state(p->data, error_state);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_error_state);

int platform_fg_ops_get_cutoff_voltage(enum fg_ic_role ic_role, int *volt)
{
	struct fuelguage_info *p;
	int ret;

	if (!volt)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_cutoff_voltage) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_cutoff_voltage(p->data, volt);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_cutoff_voltage);

int platform_fg_ops_set_cutoff_voltage(enum fg_ic_role ic_role,
				       int cutoff_voltage)
{
	struct fuelguage_info *p;
	int ret;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_set_cutoff_voltage) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_set_cutoff_voltage(p->data, cutoff_voltage);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_set_cutoff_voltage);

int platform_fg_ops_get_dod_count(enum fg_ic_role ic_role)
{
	struct fuelguage_info *p;
	int ret;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_dod_count) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_dod_count(p->data);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_dod_count);

int platform_fg_ops_get_count_level1(enum fg_ic_role ic_role, int *count_level1)
{
	struct fuelguage_info *p;
	int ret;

	if (!count_level1)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_count_level1) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_count_level1(p->data, count_level1);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_count_level1);

int platform_fg_ops_get_count_level2(enum fg_ic_role ic_role, int *count_level2)
{
	struct fuelguage_info *p;
	int ret;

	if (!count_level2)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_count_level2) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_count_level2(p->data, count_level2);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_count_level2);

int platform_fg_ops_get_count_level3(enum fg_ic_role ic_role, int *count_level3)
{
	struct fuelguage_info *p;
	int ret;

	if (!count_level3)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_count_level3) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_count_level3(p->data, count_level3);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_count_level3);

int platform_fg_ops_get_count_lowtemp(enum fg_ic_role ic_role,
				      int *count_lowtemp)
{
	struct fuelguage_info *p;
	int ret;

	if (!count_lowtemp)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_count_lowtemp) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_count_lowtemp(p->data, count_lowtemp);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_count_lowtemp);

int platform_fg_ops_set_clear_count_data(enum fg_ic_role ic_role)
{
	struct fuelguage_info *p;
	int ret;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_set_clear_count_data) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_set_clear_count_data(p->data);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_set_clear_count_data);

int platform_fg_ops_get_adapt_power(enum fg_ic_role ic_role, int *adapt_power)
{
	struct fuelguage_info *p;
	int ret;

	if (!adapt_power)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_adapt_power) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_adapt_power(p->data, adapt_power);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_adapt_power);

int platform_fg_ops_get_aged_flag(enum fg_ic_role ic_role, int *aged_flag)
{
	struct fuelguage_info *p;
	int ret;

	if (!aged_flag)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_aged_flag) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_aged_flag(p->data, aged_flag);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_aged_flag);

int platform_fg_ops_get_isc_alert_level(enum fg_ic_role ic_role,
					int *isc_alert_level)
{
	struct fuelguage_info *p;
	int ret;

	if (!isc_alert_level)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_isc_alert_level) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_isc_alert_level(p->data, isc_alert_level);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_isc_alert_level);

int platform_fg_ops_get_soa_alert_level(enum fg_ic_role ic_role,
					int *soa_alert_level)
{
	struct fuelguage_info *p;
	int ret;

	if (!soa_alert_level)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_soa_alert_level) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_soa_alert_level(p->data, soa_alert_level);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_soa_alert_level);

int platform_fg_ops_get_raw_soc(enum fg_ic_role ic_role, int *raw_soc)
{
	struct fuelguage_info *p;
	int ret;

	if (!raw_soc)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_raw_soc) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_raw_soc(p->data, raw_soc);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_raw_soc);

void platform_fg_ops_update_fw(enum fg_ic_role ic_role)
{
	struct fuelguage_info *p;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_update_fw)
		goto out;

	p->ops->fg_ic_update_fw(p->data);
out:
	up_read(&fg_ops_lock);
}
EXPORT_SYMBOL(platform_fg_ops_update_fw);

int platform_fg_ops_get_device_name(enum fg_ic_role ic_role,
				    const char **device_name)
{
	struct fuelguage_info *p;
	int ret;

	if (!device_name)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_device_name) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_device_name(p->data, device_name);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_device_name);

int platform_fg_ops_get_temp_min(enum fg_ic_role ic_role, int *temp_min)
{
	struct fuelguage_info *p;
	int ret;

	if (!temp_min)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_temp_min) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_temp_min(p->data, temp_min);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_temp_min);

void platform_fg_ops_set_force_report_full(enum fg_ic_role ic_role)
{
	struct fuelguage_info *p;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_set_force_report_full)
		goto out;

	p->ops->fg_ic_set_force_report_full(p->data);
out:
	up_read(&fg_ops_lock);
}
EXPORT_SYMBOL(platform_fg_ops_set_force_report_full);

int platform_fg_ops_get_fc(enum fg_ic_role ic_role, bool *fc)
{
	struct fuelguage_info *p;
	int ret;

	if (!fc)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_fc) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_fc(p->data, fc);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_fc);

int platform_fg_ops_set_co(enum fg_ic_role ic_role, bool enable)
{
	struct fuelguage_info *p;
	int ret;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_set_co) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_set_co(p->data, enable);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_set_co);

int platform_fg_ops_get_calibration_ffc_iterm(enum fg_ic_role ic_role,
					      int *calibration_ffc_iterm)
{
	struct fuelguage_info *p;
	int ret;

	if (!calibration_ffc_iterm)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_calibration_ffc_iterm) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_calibration_ffc_iterm(p->data, calibration_ffc_iterm);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_calibration_ffc_iterm);

int platform_fg_ops_get_real_supplement_energy(enum fg_ic_role ic_role,
					       int *supplement_energy)
{
	struct fuelguage_info *p;
	int ret;

	if (!supplement_energy)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_real_supplement_energy) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_real_supplement_energy(p->data, supplement_energy);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_real_supplement_energy);

int platform_fg_ops_get_calibration_charge_energy(enum fg_ic_role ic_role,
						  int *charge_energy)
{
	struct fuelguage_info *p;
	int ret;

	if (!charge_energy)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_calibration_charge_energy) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_calibration_charge_energy(p->data, charge_energy);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_calibration_charge_energy);

void platform_fg_ops_fl4p0_enable_check(enum fg_ic_role ic_role)
{
	struct fuelguage_info *p;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_fl4p0_enable_check)
		goto out;

	p->ops->fg_ic_fl4p0_enable_check(p->data);
out:
	up_read(&fg_ops_lock);
}
EXPORT_SYMBOL(platform_fg_ops_fl4p0_enable_check);

void platform_fg_ops_get_ui_soh(enum fg_ic_role ic_role, int *ui_soh)
{
	struct fuelguage_info *p;

	if (!ui_soh)
		return;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_ui_soh)
		goto out;

	p->ops->fg_ic_get_ui_soh(p->data, ui_soh);
out:
	up_read(&fg_ops_lock);
}
EXPORT_SYMBOL(platform_fg_ops_get_ui_soh);

void platform_fg_ops_qbg_send_chg_data(enum fg_ic_role ic_role, int chg_status,
				       int ibat_max)
{
	struct fuelguage_info *p;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_qbg_send_chg_data)
		goto out;

	p->ops->fg_ic_qbg_send_chg_data(p->data, chg_status, ibat_max);
out:
	up_read(&fg_ops_lock);
}
EXPORT_SYMBOL(platform_fg_ops_qbg_send_chg_data);

void platform_fg_ops_get_pack_vendor(enum fg_ic_role ic_role, int *pack_vendor)
{
	struct fuelguage_info *p;

	if (!pack_vendor)
		return;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_pack_vendor)
		goto out;

	p->ops->fg_ic_get_pack_vendor(p->data, pack_vendor);
out:
	up_read(&fg_ops_lock);
}
EXPORT_SYMBOL(platform_fg_ops_get_pack_vendor);

unsigned long platform_fg_ops_get_calc_rvalue(enum fg_ic_role ic_role)
{
	struct fuelguage_info *p;
	unsigned long ret;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_calc_rvalue) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_calc_rvalue(p->data);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_calc_rvalue);

void platform_fg_ops_get_ota_update_flag(enum fg_ic_role ic_role, u32 *flag)
{
	struct fuelguage_info *p;

	if (!flag)
		return;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_ota_update_flag)
		goto out;

	p->ops->fg_ic_get_ota_update_flag(p->data, flag);
out:
	up_read(&fg_ops_lock);
}
EXPORT_SYMBOL(platform_fg_ops_get_ota_update_flag);

void platform_fg_ops_ota_update_check(enum fg_ic_role ic_role)
{
	struct fuelguage_info *p;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_ota_update_check)
		goto out;

	p->ops->fg_ic_ota_update_check(p->data);
out:
	up_read(&fg_ops_lock);
}
EXPORT_SYMBOL(platform_fg_ops_ota_update_check);

void platform_fg_ops_get_average_current(enum fg_ic_role ic_role,
					 int *average_current)
{
	struct fuelguage_info *p;

	if (!average_current)
		return;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_average_current)
		goto out;

	p->ops->fg_ic_get_average_current(p->data, average_current);
out:
	up_read(&fg_ops_lock);
}
EXPORT_SYMBOL(platform_fg_ops_get_average_current);

void platform_fg_ops_get_batt_abnormal_info(enum fg_ic_role ic_role,
					    void *info)
{
	struct fuelguage_info *p;

	if (!info)
		return;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_batt_abnormal_info)
		goto out;

	p->ops->fg_ic_get_batt_abnormal_info(p->data, info);
out:
	up_read(&fg_ops_lock);
}
EXPORT_SYMBOL(platform_fg_ops_get_batt_abnormal_info);

int platform_fg_ops_get_manufacturing_date(enum fg_ic_role ic_role,
					   u8 *manufacturing_date)
{
	struct fuelguage_info *p;
	int ret;

	if (!manufacturing_date)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_manufacturing_date) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_manufacturing_date(p->data, manufacturing_date);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_manufacturing_date);

void platform_fg_ops_set_first_usage_date(enum fg_ic_role ic_role,
					  const char *date)
{
	struct fuelguage_info *p;

	if (!date)
		return;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_set_first_usage_date)
		goto out;

	p->ops->fg_ic_set_first_usage_date(p->data, date);
out:
	up_read(&fg_ops_lock);
}
EXPORT_SYMBOL(platform_fg_ops_set_first_usage_date);

int platform_fg_ops_get_first_usage_date(enum fg_ic_role ic_role,
					 u8 *first_usage_date)
{
	struct fuelguage_info *p;
	int ret;

	if (!first_usage_date)
		return -EINVAL;

	down_read(&fg_ops_lock);
	p = fg_ic_lookup(ic_role);

	if (!p || !p->ops->fg_ic_get_first_usage_date) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = p->ops->fg_ic_get_first_usage_date(p->data, first_usage_date);
out:
	up_read(&fg_ops_lock);
	return ret;
}
EXPORT_SYMBOL(platform_fg_ops_get_first_usage_date);
/*
 * The registry above is reached by exported symbol rather than through a
 * device, so this driver matches nothing and has no probe; registering it
 * only leaves an entry under /sys/bus/platform/drivers.
 */
static struct platform_driver platform_fg_ops_driver = {
	.driver = {
		.name = "platform_fg_ops",
	},
};
module_platform_driver(platform_fg_ops_driver);

MODULE_DESCRIPTION("platform fg ops");
MODULE_AUTHOR("liyuze1@xiaomi.com");
MODULE_LICENSE("GPL v2");
