// SPDX-License-Identifier: GPL-2.0-only
/*
 * Annibale BQ27Z561-compatible gauge: read-only standard-command subset.
 * Register/units reference: xiaomi-dada reconstruction a7a6375b13e4,
 * cross-checked against Annibale stock bq27z561.ko (see Documentation/xiaomi).
 * No manufacturer commands, firmware update, unsealing or charge policy.
 */
#include <linux/hwid.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include <mca/platform/platform_fg_ic_ops.h>

struct annibale_fg {
	struct i2c_client *client;
	struct mutex lock;
	bool suspended;
	bool stopping;
};

/* Units are the MCA ABI, not the power_supply ABI (notably current sign). */
enum annibale_fg_value {
	FG_VOLTAGE_MV,
	FG_CURRENT_UA,
	FG_TEMP_DECIC,
	FG_INTERNAL_TEMP_DECIC,
	FG_SOC_PERCENT,
	FG_REMAINING_UAH,
	FG_FULL_UAH,
	FG_DESIGN_UAH,
	FG_CYCLES,
	FG_SOH_PERCENT,
	FG_FLAGS,
	FG_VALUE_COUNT,
};

static const u8 annibale_fg_regs[] = {
	[FG_VOLTAGE_MV] = 0x08,
	[FG_CURRENT_UA] = 0x0c,
	[FG_TEMP_DECIC] = 0x06,
	[FG_INTERNAL_TEMP_DECIC] = 0x1e,
	[FG_SOC_PERCENT] = 0x2c,
	[FG_REMAINING_UAH] = 0x10,
	[FG_FULL_UAH] = 0x12,
	[FG_DESIGN_UAH] = 0x3c,
	[FG_CYCLES] = 0x2a,
	[FG_SOH_PERCENT] = 0x2e,
	[FG_FLAGS] = 0x0a,
};

static int annibale_fg_decode(enum annibale_fg_value kind, u16 raw, int *value)
{
	int result = raw;

	switch (kind) {
	case FG_VOLTAGE_MV:
		if (!raw || raw > 5000)
			return -ERANGE;
		break;
	case FG_CURRENT_UA:
		/* Sign-extend first, including 0x8000; MCA is discharge-positive. */
		result = -(int)(s16)raw;
		if (result < -25000 || result > 6000)
			return -ERANGE;
		result *= 1000;
		break;
	case FG_TEMP_DECIC:
	case FG_INTERNAL_TEMP_DECIC:
		result -= 2730;
		if (result < -1280 || result > 1280)
			return -ERANGE;
		break;
	case FG_SOC_PERCENT:
	case FG_SOH_PERCENT:
		if (raw > 100)
			return -ERANGE;
		break;
	case FG_REMAINING_UAH:
	case FG_FULL_UAH:
	case FG_DESIGN_UAH:
		if (raw > 8000 || (!raw && kind != FG_REMAINING_UAH))
			return -ERANGE;
		result *= 1000;
		break;
	case FG_CYCLES:
	case FG_FLAGS:
		break;
	default:
		return -EINVAL;
	}
	*value = result;
	return 0;
}

static int annibale_fg_read(struct annibale_fg *fg,
			    enum annibale_fg_value kind, int *value)
{
	int ret;

	if (!fg || !value || (unsigned int)kind >= FG_VALUE_COUNT)
		return -EINVAL;
	mutex_lock(&fg->lock);
	if (fg->stopping)
		ret = -ENODEV;
	else if (fg->suspended)
		ret = -EHOSTDOWN;
	else
		ret = i2c_smbus_read_word_data(fg->client, annibale_fg_regs[kind]);
	/* SMBus word reads are little-endian; errors never replace the output. */
	if (ret >= 0)
		ret = annibale_fg_decode(kind, ret, value);
	mutex_unlock(&fg->lock);
	return ret;
}

#define FG_READER(name, kind) \
static int annibale_fg_##name(void *data, int *value) \
{ \
	return annibale_fg_read(data, kind, value); \
}

FG_READER(volt, FG_VOLTAGE_MV)
FG_READER(curr, FG_CURRENT_UA)
FG_READER(temp, FG_TEMP_DECIC)
FG_READER(original_temp, FG_INTERNAL_TEMP_DECIC)
FG_READER(rsoc, FG_SOC_PERCENT)
FG_READER(rm, FG_REMAINING_UAH)
FG_READER(fcc, FG_FULL_UAH)
FG_READER(dc, FG_DESIGN_UAH)
FG_READER(cycles, FG_CYCLES)
FG_READER(soh, FG_SOH_PERCENT)

static int annibale_fg_chip_ok(void *data, int *ok)
{
	int flags, ret;

	if (!ok)
		return -EINVAL;
	ret = annibale_fg_read(data, FG_FLAGS, &flags);
	if (!ret)
		*ok = 1;
	return ret;
}

static int annibale_fg_probe_ok(void *data, bool *ok)
{
	int value, ret;

	if (!ok)
		return -EINVAL;
	ret = annibale_fg_chip_ok(data, &value);
	if (!ret)
		*ok = true;
	return ret;
}

static const struct fuelguage_ic_ops annibale_fg_ops = {
	.fg_ic_probe_ok = annibale_fg_probe_ok,
	.fg_ic_get_rsoc = annibale_fg_rsoc,
	.fg_ic_get_curr = annibale_fg_curr,
	.fg_ic_get_volt = annibale_fg_volt,
	.fg_ic_get_temp = annibale_fg_temp,
	.fg_ic_get_original_temp = annibale_fg_original_temp,
	.fg_ic_get_rm = annibale_fg_rm,
	.fg_ic_get_chip_ok = annibale_fg_chip_ok,
	.fg_ic_get_cyclecount = annibale_fg_cycles,
	.fg_ic_get_fcc = annibale_fg_fcc,
	.fg_ic_get_full_design = annibale_fg_dc,
	.fg_ic_get_soh = annibale_fg_soh,
};

struct annibale_fg_attribute {
	struct device_attribute attr;
	enum annibale_fg_value kind;
};

static ssize_t annibale_fg_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct annibale_fg_attribute *fg_attr =
		container_of(attr, struct annibale_fg_attribute, attr);
	int value, ret;

	ret = annibale_fg_read(dev_get_drvdata(dev), fg_attr->kind, &value);
	if (ret)
		return ret;
	return sysfs_emit(buf, "%d\n", value);
}

#define FG_ATTR(name, id) \
static struct annibale_fg_attribute fg_attr_##name = { \
	.attr = __ATTR(name, 0444, annibale_fg_show, NULL), .kind = id, \
}

FG_ATTR(voltage_mv, FG_VOLTAGE_MV);
FG_ATTR(discharge_current_ua, FG_CURRENT_UA);
FG_ATTR(temperature_decic, FG_TEMP_DECIC);
FG_ATTR(internal_temperature_decic, FG_INTERNAL_TEMP_DECIC);
FG_ATTR(soc_percent, FG_SOC_PERCENT);
FG_ATTR(remaining_charge_uah, FG_REMAINING_UAH);
FG_ATTR(full_charge_uah, FG_FULL_UAH);
FG_ATTR(design_charge_uah, FG_DESIGN_UAH);
FG_ATTR(cycle_count, FG_CYCLES);
FG_ATTR(soh_percent, FG_SOH_PERCENT);

static struct attribute *annibale_fg_attrs[] = {
	&fg_attr_voltage_mv.attr.attr,
	&fg_attr_discharge_current_ua.attr.attr,
	&fg_attr_temperature_decic.attr.attr,
	&fg_attr_internal_temperature_decic.attr.attr,
	&fg_attr_soc_percent.attr.attr,
	&fg_attr_remaining_charge_uah.attr.attr,
	&fg_attr_full_charge_uah.attr.attr,
	&fg_attr_design_charge_uah.attr.attr,
	&fg_attr_cycle_count.attr.attr,
	&fg_attr_soh_percent.attr.attr,
	NULL,
};

static const struct attribute_group annibale_fg_group = {
	.name = "telemetry",
	.attrs = annibale_fg_attrs,
};

static void annibale_fg_unpublish(struct annibale_fg *fg)
{
	mutex_lock(&fg->lock);
	fg->stopping = true;
	mutex_unlock(&fg->lock);
	/* Do not hold the bus lock while waiting for registry readers. */
	platform_fg_ic_ops_unregister(FG_IC_MASTER, fg, &annibale_fg_ops);
}

static int annibale_fg_probe(struct i2c_client *client)
{
	struct annibale_fg *fg;
	u32 role;
	int ret, value;

	if (!xiaomi_hwid_is_ready() || strcmp(product_name_get(), "annibale"))
		return -ENODEV;
	if (!client->dev.of_node || client->addr != 0x55 ||
	    (client->flags & I2C_CLIENT_TEN))
		return -ENODEV;
	ret = of_property_read_u32(client->dev.of_node, "ic_role", &role);
	if (ret || role != FG_IC_MASTER)
		return -EINVAL;
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -EOPNOTSUPP;
	fg = devm_kzalloc(&client->dev, sizeof(*fg), GFP_KERNEL);
	if (!fg)
		return -ENOMEM;
	fg->client = client;
	mutex_init(&fg->lock);
	i2c_set_clientdata(client, fg);
	/* Check useful live data before publishing either interface. */
	ret = annibale_fg_read(fg, FG_VOLTAGE_MV, &value);
	if (ret)
		return dev_err_probe(&client->dev, ret, "gauge voltage unavailable\n");
	ret = annibale_fg_read(fg, FG_SOC_PERCENT, &value);
	if (ret)
		return dev_err_probe(&client->dev, ret, "gauge SOC unavailable\n");
	ret = platform_fg_ic_ops_register(FG_IC_MASTER, fg, &annibale_fg_ops);
	if (ret)
		return ret;
	ret = sysfs_create_group(&client->dev.kobj, &annibale_fg_group);
	if (ret)
		annibale_fg_unpublish(fg);
	return ret;
}

static void annibale_fg_remove(struct i2c_client *client)
{
	struct annibale_fg *fg = i2c_get_clientdata(client);

	annibale_fg_unpublish(fg);
	sysfs_remove_group(&client->dev.kobj, &annibale_fg_group);
}

static int annibale_fg_suspend(struct device *dev)
{
	struct annibale_fg *fg = dev_get_drvdata(dev);

	mutex_lock(&fg->lock);
	fg->suspended = true;
	mutex_unlock(&fg->lock);
	return 0;
}

static int annibale_fg_resume(struct device *dev)
{
	struct annibale_fg *fg = dev_get_drvdata(dev);

	mutex_lock(&fg->lock);
	fg->suspended = false;
	mutex_unlock(&fg->lock);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(annibale_fg_pm_ops,
			       annibale_fg_suspend, annibale_fg_resume);

static const struct of_device_id annibale_fg_match[] = {
	{ .compatible = "ti,bq27z561_master" },
	{ }
};
MODULE_DEVICE_TABLE(of, annibale_fg_match);

static struct i2c_driver annibale_fg_driver = {
	.driver = {
		.name = "annibale-bq27z561",
		.of_match_table = annibale_fg_match,
		.pm = pm_sleep_ptr(&annibale_fg_pm_ops),
	},
	.probe = annibale_fg_probe,
	.remove = annibale_fg_remove,
};
module_i2c_driver(annibale_fg_driver);

MODULE_DESCRIPTION("Annibale read-only BQ27Z561 MCA telemetry");
MODULE_LICENSE("GPL");
