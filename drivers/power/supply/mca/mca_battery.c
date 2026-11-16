// SPDX-License-Identifier: GPL-2.0-only
/*
 * Annibale read-only battery reporting through the MCA gauge registry.
 * This is measurement plumbing, not a charger/thermal/SOC policy engine.
 */
#include <linux/hwid.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include <mca/platform/platform_fg_ic_ops.h>

#define ANNIBALE_BATTERY_POLL_MS 30000

/* Keep measurements first: polling compares their values and individual errors. */
static const enum power_supply_property annibale_battery_props[] = {
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
};

#define ANNIBALE_BATTERY_MEASUREMENTS 8

struct annibale_battery_sample {
	int value[ANNIBALE_BATTERY_MEASUREMENTS];
	int error[ANNIBALE_BATTERY_MEASUREMENTS];
};

struct annibale_battery {
	struct power_supply *psy;
	/* Serializes reads, polling and lifecycle state transitions. */
	struct mutex lock;
	struct delayed_work poll;
	struct annibale_battery_sample last_sample;
	bool have_sample;
	bool force_notify;
	bool suspended;
	bool stopping;
};

static int annibale_battery_read(enum power_supply_property prop, int *out)
{
	int value, ret;

	switch (prop) {
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = platform_fg_ops_get_rsoc(FG_IC_MASTER, &value);
		if (!ret && (value < 0 || value > 100))
			ret = -ERANGE;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = platform_fg_ops_get_volt(FG_IC_MASTER, &value);
		if (!ret) {
			if (value <= 0 || value > INT_MAX / 1000)
				return -ERANGE;
			value *= 1000; /* MCA mV -> power_supply uV. */
		}
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = platform_fg_ops_get_curr(FG_IC_MASTER, &value);
		if (!ret) {
			if (value == INT_MIN)
				return -ERANGE;
			value = -value; /* MCA is discharge-positive; ABI is charge-positive. */
		}
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = platform_fg_ops_get_temp(FG_IC_MASTER, &value);
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		ret = platform_fg_ops_get_rm(FG_IC_MASTER, &value);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		ret = platform_fg_ops_get_fcc(FG_IC_MASTER, &value);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		ret = platform_fg_ops_get_full_design(FG_IC_MASTER, &value);
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		ret = platform_fg_ops_get_cyclecount(FG_IC_MASTER, &value);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		/* Gauge current/SOC alone cannot establish charger state or completion. */
		*out = POWER_SUPPLY_STATUS_UNKNOWN;
		return 0;
	case POWER_SUPPLY_PROP_HEALTH:
		/* Pack faults, authentication and thermal policy are not integrated. */
		*out = POWER_SUPPLY_HEALTH_UNKNOWN;
		return 0;
	default:
		return -EINVAL;
	}
	if (ret)
		return ret;
	*out = value;
	return 0;
}

static int annibale_battery_get_property(struct power_supply *psy,
					 enum power_supply_property prop,
					 union power_supply_propval *val)
{
	struct annibale_battery *battery = power_supply_get_drvdata(psy);
	int ret, value;

	if (!battery || !val)
		return -EINVAL;
	mutex_lock(&battery->lock);
	if (battery->stopping)
		ret = -ENODEV;
	else if (battery->suspended)
		ret = -ENODATA;
	else
		ret = annibale_battery_read(prop, &value);
	if (!ret)
		val->intval = value;
	mutex_unlock(&battery->lock);
	/* The core skips unavailable fields in uevents instead of aborting them. */
	return ret && ret != -EINVAL && ret != -ENODEV ? -ENODATA : ret;
}

static void annibale_battery_poll(struct work_struct *work)
{
	struct annibale_battery *battery = container_of(to_delayed_work(work),
						struct annibale_battery, poll);
	struct annibale_battery_sample sample = {};
	int i;

	mutex_lock(&battery->lock);
	if (battery->stopping || battery->suspended)
		goto out;
	for (i = 0; i < ANNIBALE_BATTERY_MEASUREMENTS; i++)
		sample.error[i] = annibale_battery_read(annibale_battery_props[i],
							&sample.value[i]);
	if (!battery->have_sample || battery->force_notify ||
	    memcmp(&sample, &battery->last_sample, sizeof(sample)))
		power_supply_changed(battery->psy);
	battery->last_sample = sample;
	battery->have_sample = true;
	battery->force_notify = false;
	/* Deferrable and freezable: this timer must not wake an idle/suspended CPU. */
	queue_delayed_work(system_freezable_wq, &battery->poll,
			   msecs_to_jiffies(ANNIBALE_BATTERY_POLL_MS));
out:
	mutex_unlock(&battery->lock);
}

static void annibale_battery_external_changed(struct power_supply *psy)
{
	struct annibale_battery *battery = power_supply_get_drvdata(psy);

	mutex_lock(&battery->lock);
	if (!battery->stopping && !battery->suspended) {
		battery->force_notify = true;
		mod_delayed_work(system_freezable_wq, &battery->poll, 0);
	}
	mutex_unlock(&battery->lock);
}

static const struct power_supply_desc annibale_battery_desc = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = annibale_battery_props,
	.num_properties = ARRAY_SIZE(annibale_battery_props),
	.get_property = annibale_battery_get_property,
	.external_power_changed = annibale_battery_external_changed,
};

static int annibale_battery_probe(struct platform_device *pdev)
{
	struct power_supply_config config = {};
	struct annibale_battery *battery;
	struct power_supply *existing;
	bool ready = false;
	int ret;

	if (!xiaomi_hwid_is_ready() || strcmp(product_name_get(), "annibale"))
		return -ENODEV;
	/* A missing provider is a real probe dependency, never a fake battery. */
	ret = platform_fg_ops_probe_ok(FG_IC_MASTER, &ready);
	if (ret == -EOPNOTSUPP || (!ret && !ready))
		return -EPROBE_DEFER;
	if (ret)
		return ret;
	existing = power_supply_get_by_name("battery");
	if (existing) {
		power_supply_put(existing);
		return -EBUSY;
	}
	battery = devm_kzalloc(&pdev->dev, sizeof(*battery), GFP_KERNEL);
	if (!battery)
		return -ENOMEM;
	mutex_init(&battery->lock);
	INIT_DEFERRABLE_WORK(&battery->poll, annibale_battery_poll);
	platform_set_drvdata(pdev, battery);
	config.drv_data = battery;
	config.of_node = pdev->dev.of_node;
	battery->psy = power_supply_register(&pdev->dev, &annibale_battery_desc, &config);
	if (IS_ERR(battery->psy))
		return PTR_ERR(battery->psy);
	queue_delayed_work(system_freezable_wq, &battery->poll, 0);
	return 0;
}

static void annibale_battery_remove(struct platform_device *pdev)
{
	struct annibale_battery *battery = platform_get_drvdata(pdev);

	mutex_lock(&battery->lock);
	battery->stopping = true;
	mutex_unlock(&battery->lock);
	cancel_delayed_work_sync(&battery->poll);
	power_supply_unregister(battery->psy);
}

static int annibale_battery_suspend(struct device *dev)
{
	struct annibale_battery *battery = dev_get_drvdata(dev);

	mutex_lock(&battery->lock);
	battery->suspended = true;
	mutex_unlock(&battery->lock);
	cancel_delayed_work_sync(&battery->poll);
	return 0;
}

static int annibale_battery_resume(struct device *dev)
{
	struct annibale_battery *battery = dev_get_drvdata(dev);

	mutex_lock(&battery->lock);
	battery->suspended = false;
	if (!battery->stopping) {
		battery->force_notify = true;
		mod_delayed_work(system_freezable_wq, &battery->poll, 0);
	}
	mutex_unlock(&battery->lock);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(annibale_battery_pm_ops,
			       annibale_battery_suspend, annibale_battery_resume);

static const struct of_device_id annibale_battery_match[] = {
	{ .compatible = "mca,business_battery" },
	{ }
};
MODULE_DEVICE_TABLE(of, annibale_battery_match);

static struct platform_driver annibale_battery_driver = {
	.probe = annibale_battery_probe,
	.remove_new = annibale_battery_remove,
	.driver = {
		.name = "annibale-battery",
		.of_match_table = annibale_battery_match,
		.pm = pm_sleep_ptr(&annibale_battery_pm_ops),
	},
};
module_platform_driver(annibale_battery_driver);

MODULE_DESCRIPTION("Annibale read-only MCA battery power supply");
MODULE_SOFTDEP("pre: mca_bq27z561");
MODULE_LICENSE("GPL");
