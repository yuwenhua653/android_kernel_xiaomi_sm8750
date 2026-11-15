// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2025 The LineageOS Project (original path table/interface). */
/* Annibale charging dispatcher; source interface from the community reconstruction. */
#include <linux/errno.h>
#include <linux/hwid.h>
#include <linux/kstrtox.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rwsem.h>
#include <linux/string.h>
#include <mca/common/mca_charge_interface.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_sysfs.h>

static const char * const mca_path_names[MCA_CHARGE_IF_CHG_TYPE_END] = {
	[MCA_CHARGE_IF_CHG_TYPE_BUCK]			= "buck",
	[MCA_CHARGE_IF_CHG_TYPE_MAIN_BUCK]		= "main_buck",
	[MCA_CHARGE_IF_CHG_TYPE_AUX_BUCK]		= "aux_buck",
	[MCA_CHARGE_IF_CHG_TYPE_QC]			= "quick",
	[MCA_CHARGE_IF_CHG_TYPE_QC_MAIN_PATH]		= "quick_main",
	[MCA_CHARGE_IF_CHG_TYPE_QC_AUX_PATH]		= "quick_aux",
	[MCA_CHARGE_IF_CHG_TYPE_QC_DIV1]		= "div1",
	[MCA_CHARGE_IF_CHG_TYPE_QC_DIV2]		= "div2",
	[MCA_CHARGE_IF_CHG_TYPE_QC_DIV4]		= "div4",
	[MCA_CHARGE_IF_CHG_TYPE_WL_BUCK]		= "wl_buck",
	[MCA_CHARGE_IF_CHG_TYPE_WL_MAIN_BUCK]		= "wl_main_buck",
	[MCA_CHARGE_IF_CHG_TYPE_WL_AUX_BUCK]		= "wl_aux_buck",
	[MCA_CHARGE_IF_CHG_TYPE_WL_QC]			= "wl_quick",
	[MCA_CHARGE_IF_CHG_TYPE_WL_QC_MAIN_PATH]	= "wl_quick_main",
	[MCA_CHARGE_IF_CHG_TYPE_WL_QC_AUX_PATH]		= "wl_quick_aux",
	[MCA_CHARGE_IF_CHG_TYPE_WL_QC_DIV1]		= "wl_div1",
	[MCA_CHARGE_IF_CHG_TYPE_WL_QC_DIV2]		= "wl_div2",
	[MCA_CHARGE_IF_CHG_TYPE_WL_QC_DIV4]		= "wl_div4",
	[MCA_CHARGE_IF_CHG_TYPE_ALL]			= "all",
};

enum mca_charge_field {
	MCA_INPUT_SUSPEND, MCA_CHARGE_ENABLE, MCA_IIN_LIMIT, MCA_ICHG_LIMIT,
	MCA_POWER_LIMIT, MCA_SUSPEND_STATUS, MCA_SHIPMODE,
};
struct mca_charge_provider {
	const struct mca_charge_if_ops *key;
	struct mca_charge_if_ops ops;
};
static struct mca_charge_provider mca_providers[MCA_CHARGE_IF_CHG_TYPE_ALL];
static DECLARE_RWSEM(mca_charge_lock);

static int mca_path_index(const char *name)
{
	int i;

	if (!name)
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(mca_path_names); i++)
		if (!strcmp(name, mca_path_names[i]))
			return i;
	return -EINVAL;
}

int mca_charge_if_ops_register(struct mca_charge_if_ops *ops)
{
	int type, ret = 0;

	if (!ops || !ops->data)
		return -EINVAL;
	type = mca_path_index(ops->type_name);
	if (type < 0 || type == MCA_CHARGE_IF_CHG_TYPE_ALL)
		return -EINVAL;
	down_write(&mca_charge_lock);
	if (mca_providers[type].key) {
		ret = -EBUSY;
	} else {
		mca_providers[type].ops = *ops;
		mca_providers[type].key = ops;
	}
	up_write(&mca_charge_lock);
	return ret;
}
EXPORT_SYMBOL(mca_charge_if_ops_register);

int mca_charge_if_ops_unregister(struct mca_charge_if_ops *ops)
{
	int i, ret = -ENOENT;

	if (!ops)
		return -EINVAL;
	down_write(&mca_charge_lock);
	for (i = 0; i < ARRAY_SIZE(mca_providers); i++) {
		if (mca_providers[i].key == ops) {
			memset(&mca_providers[i], 0, sizeof(mca_providers[i]));
			ret = 0;
			break;
		}
	}
	up_write(&mca_charge_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(mca_charge_if_ops_unregister);

struct mca_charge_request {
	char user[64], path[32], value[128];
	u32 number;
	int type;
};

/* Return 1 for a complete token, 0 at end; never split an oversized token. */
static int mca_charge_next_token(char **input, char *out, size_t size)
{
	char *token;

	while ((token = strsep(input, " \t\n\r\f\v"))) {
		if (!*token)
			continue;
		if (strscpy(out, token, size) < 0)
			return -E2BIG;
		return 1;
	}
	return 0;
}

static int mca_charge_parse(enum mca_charge_field field, const char *buf,
			    size_t count, struct mca_charge_request *req)
{
	char input[256], extra[2], *cursor = input;
	int signed_value, ret;

	if (!count || count >= sizeof(input) || memchr(buf, '\0', count))
		return -EINVAL;
	memcpy(input, buf, count);
	input[count] = '\0';
	if (field == MCA_SUSPEND_STATUS)
		return -EACCES;
	if (field == MCA_SHIPMODE) {
		strscpy(req->user, "micharger", sizeof(req->user));
		strscpy(req->path, "buck", sizeof(req->path));
		ret = mca_charge_next_token(&cursor, req->value, sizeof(req->value));
		if (ret != 1)
			return ret < 0 ? ret : -EINVAL;
	} else {
		ret = mca_charge_next_token(&cursor, req->user, sizeof(req->user));
		if (ret != 1)
			return ret < 0 ? ret : -EINVAL;
		ret = mca_charge_next_token(&cursor, req->path, sizeof(req->path));
		if (ret != 1)
			return ret < 0 ? ret : -EINVAL;
		ret = mca_charge_next_token(&cursor, req->value, sizeof(req->value));
		if (ret != 1)
			return ret < 0 ? ret : -EINVAL;
	}
	if (mca_charge_next_token(&cursor, extra, sizeof(extra)))
		return -EINVAL;
	req->type = mca_path_index(req->path);
	if (req->type < 0)
		return req->type;
	switch (field) {
	case MCA_INPUT_SUSPEND:
	case MCA_IIN_LIMIT:
	case MCA_ICHG_LIMIT:
		return kstrtoint(req->value, 0, &signed_value);
	case MCA_CHARGE_ENABLE:
	case MCA_POWER_LIMIT:
	case MCA_SHIPMODE:
		ret = kstrtou32(req->value, field == MCA_SHIPMODE ? 10 : 0, &req->number);
		if (ret)
			return ret;
		if (field != MCA_POWER_LIMIT && req->number > 1)
			return -ERANGE;
		return 0;
	default:
		return -EINVAL;
	}
}

static bool mca_charge_has_setter(const struct mca_charge_if_ops *ops,
				 enum mca_charge_field field)
{
	switch (field) {
	case MCA_INPUT_SUSPEND: return !!ops->set_input_suspend;
	case MCA_CHARGE_ENABLE: return !!ops->set_charge_enable;
	case MCA_IIN_LIMIT: return !!ops->set_input_current_limit;
	case MCA_ICHG_LIMIT: return !!ops->set_charge_current_limit;
	case MCA_POWER_LIMIT: return !!ops->set_charge_power_limit;
	case MCA_SHIPMODE: return !!ops->set_ship_mode_en;
	default: return false;
	}
}

static int mca_charge_set(struct mca_charge_if_ops *ops, enum mca_charge_field field,
			  struct mca_charge_request *req)
{
	switch (field) {
	case MCA_INPUT_SUSPEND:
		return ops->set_input_suspend(req->user, req->value, ops->data);
	case MCA_CHARGE_ENABLE:
		return ops->set_charge_enable(req->user, req->number, ops->data);
	case MCA_IIN_LIMIT:
		return ops->set_input_current_limit(req->user, req->value, ops->data);
	case MCA_ICHG_LIMIT:
		return ops->set_charge_current_limit(req->user, req->value, ops->data);
	case MCA_POWER_LIMIT:
		return ops->set_charge_power_limit(req->user, req->number, ops->data);
	case MCA_SHIPMODE:
		return ops->set_ship_mode_en(req->user, req->number, ops->data);
	default:
		return -EOPNOTSUPP;
	}
}

static int mca_charge_apply(enum mca_charge_field field, struct mca_charge_request *req)
{
	int first = req->type, last = first + 1, i, found = 0, ret = 0;
	bool changed = false;

	if (first < 0 || first > MCA_CHARGE_IF_CHG_TYPE_ALL)
		return -EINVAL;
	if (first == MCA_CHARGE_IF_CHG_TYPE_ALL) {
		first = 0;
		last = MCA_CHARGE_IF_CHG_TYPE_ALL;
	}
	down_read(&mca_charge_lock);
	/* Check availability before any hardware side effect. */
	for (i = first; i < last; i++) {
		if (!mca_providers[i].key)
			continue;
		found++;
		if (!mca_charge_has_setter(&mca_providers[i].ops, field)) {
			ret = -EOPNOTSUPP;
			goto out;
		}
	}
	if (!found) {
		ret = -ENODEV;
		goto out;
	}
	for (i = first; i < last; i++) {
		if (!mca_providers[i].key)
			continue;
		ret = mca_charge_set(&mca_providers[i].ops, field, req);
		if (ret) {
			if (ret > 0)
				ret = -EIO;
			break;
		}
		changed = true;
	}
out:
	up_read(&mca_charge_lock);
	/* Also refresh after partial "all" success; there is no hardware rollback. */
	if (changed && field == MCA_INPUT_SUSPEND)
		mca_event_block_notify(MCA_EVENT_TYPE_BATTERY_INFO,
				       MCA_EVENT_BATTERY_STS_CHANGE, NULL);
	return ret;
}

static int mca_charge_get(struct mca_charge_if_ops *ops, enum mca_charge_field field,
			  char *value)
{
	int (*get)(char *, void *) = NULL;

	switch (field) {
	case MCA_INPUT_SUSPEND:
	case MCA_SUSPEND_STATUS: get = ops->get_input_suspend; break;
	case MCA_CHARGE_ENABLE: get = ops->get_charge_enable; break;
	case MCA_IIN_LIMIT: get = ops->get_input_current_limit; break;
	case MCA_ICHG_LIMIT: get = ops->get_charge_current_limit; break;
	case MCA_POWER_LIMIT: get = ops->get_charge_power_limit; break;
	default: break;
	}
	return get ? get(value, ops->data) : -EOPNOTSUPP;
}

static int mca_charge_suspend_value(const char *value, bool *suspended)
{
	char input[MCA_CHARGE_IF_MAX_VALUE_BUFF], client[MCA_CHARGE_IF_MAX_VALUE_BUFF];
	char number[32], extra[2], *cursor = input;
	int state;

	if (strscpy(input, value, sizeof(input)) < 0 ||
	    mca_charge_next_token(&cursor, client, sizeof(client)) != 1 ||
	    strncmp(client, "eff_client:", 11) || !client[11] ||
	    mca_charge_next_token(&cursor, number, sizeof(number)) != 1 ||
	    mca_charge_next_token(&cursor, extra, sizeof(extra)) ||
	    kstrtoint(number, 10, &state) || (state != 0 && state != 1))
		return -EPROTO;
	*suspended = state;
	return 0;
}

static ssize_t mca_charge_read(enum mca_charge_field field, char *buf)
{
	char value[MCA_CHARGE_IF_MAX_VALUE_BUFF];
	bool suspended = false, status, path_suspended;
	int i, ret = -ENODEV, found = 0;
	ssize_t used = 0;

	down_read(&mca_charge_lock);
	if (field == MCA_SHIPMODE) {
		struct mca_charge_provider *provider = &mca_providers[MCA_CHARGE_IF_CHG_TYPE_BUCK];

		if (!provider->key)
			goto out;
		if (!provider->ops.get_ship_mode_status) {
			ret = -EOPNOTSUPP;
			goto out;
		}
		ret = provider->ops.get_ship_mode_status(&status, provider->ops.data);
		if (!ret)
			used = sysfs_emit(buf, "%d\n", status);
		goto out;
	}
	for (i = 0; i < ARRAY_SIZE(mca_providers); i++) {
		if (!mca_providers[i].key)
			continue;
		memset(value, 0, sizeof(value));
		ret = mca_charge_get(&mca_providers[i].ops, field, value);
		if (ret == -EOPNOTSUPP)
			continue;
		if (ret)
			goto out;
		if (!memchr(value, '\0', sizeof(value))) {
			ret = -EOVERFLOW;
			goto out;
		}
		found++;
		if (field == MCA_SUSPEND_STATUS) {
			ret = mca_charge_suspend_value(value, &path_suspended);
			if (ret)
				goto out;
			suspended |= path_suspended;
		} else {
			if (used + strlen(value) + strlen(mca_path_names[i]) + 2 >= PAGE_SIZE) {
				ret = -EOVERFLOW;
				goto out;
			}
			used += sysfs_emit_at(buf, used, "%s %s\n", mca_path_names[i], value);
		}
	}
	ret = found ? 0 : -ENODATA;
	if (!ret && field == MCA_SUSPEND_STATUS)
		used = sysfs_emit(buf, "%d\n", suspended);
out:
	up_read(&mca_charge_lock);
	return ret ? (ret < 0 ? ret : -EIO) : used;
}

static ssize_t mca_charge_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t mca_charge_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count);
static struct mca_sysfs_attr_info mca_charge_fields[] = {
	mca_sysfs_attr_rw(mca_charge, 0640, MCA_INPUT_SUSPEND, input_suspend),
	mca_sysfs_attr_rw(mca_charge, 0640, MCA_CHARGE_ENABLE, charge_enable),
	mca_sysfs_attr_rw(mca_charge, 0640, MCA_IIN_LIMIT, iin_limit),
	mca_sysfs_attr_rw(mca_charge, 0640, MCA_ICHG_LIMIT, ichg_limit),
	mca_sysfs_attr_rw(mca_charge, 0640, MCA_POWER_LIMIT, power_limit),
	mca_sysfs_attr_ro(mca_charge, 0440, MCA_SUSPEND_STATUS, suspend_status),
	mca_sysfs_attr_rw(mca_charge, 0640, MCA_SHIPMODE, shipmode_count_reset),
};

static ssize_t mca_charge_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *field = mca_sysfs_lookup_attr(attr->attr.name,
		mca_charge_fields, ARRAY_SIZE(mca_charge_fields));

	return field ? mca_charge_read(field->sysfs_attr_name, buf) : -EINVAL;
}
static ssize_t mca_charge_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct mca_charge_request req = { 0 };
	struct mca_sysfs_attr_info *field = mca_sysfs_lookup_attr(attr->attr.name,
		mca_charge_fields, ARRAY_SIZE(mca_charge_fields));
	int ret;

	if (!field)
		return -EINVAL;
	ret = mca_charge_parse(field->sysfs_attr_name, buf, count, &req);
	if (!ret)
		ret = mca_charge_apply(field->sysfs_attr_name, &req);
	return ret ? ret : (ssize_t)count;
}
static struct attribute *mca_charge_attrs[ARRAY_SIZE(mca_charge_fields) + 1];
static const struct attribute_group mca_charge_group = { .attrs = mca_charge_attrs };

static int mca_charge_probe(struct platform_device *pdev)
{
	if (!xiaomi_hwid_is_ready() || strcmp(product_name_get(), "annibale"))
		return -ENODEV;
	mca_sysfs_init_attrs(mca_charge_attrs, mca_charge_fields, ARRAY_SIZE(mca_charge_fields));
	return mca_sysfs_create_link_group(MCA_SYSFS_DEV_CHARGER, "charge_interface",
					   &pdev->dev, &mca_charge_group);
}
static int mca_charge_remove(struct platform_device *pdev)
{
	mca_sysfs_remove_link_group(MCA_SYSFS_DEV_CHARGER, "charge_interface",
				    &pdev->dev, &mca_charge_group);
	return 0;
}
static const struct of_device_id mca_charge_match[] = {
	{ .compatible = "mca,charge_interface" }, { }
};
MODULE_DEVICE_TABLE(of, mca_charge_match);
static struct platform_driver mca_charge_driver = {
	.probe = mca_charge_probe,
	.remove = mca_charge_remove,
	.driver = { .name = "mca_charge_interface", .of_match_table = mca_charge_match },
};
module_platform_driver(mca_charge_driver);
MODULE_DESCRIPTION("Annibale MCA charging control interface");
MODULE_LICENSE("GPL");
