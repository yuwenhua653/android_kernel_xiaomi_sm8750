// SPDX-License-Identifier: GPL-2.0
/*
 * WIFI GPIO/SAR interface for Xiaomi phones.
 * Copyright (c) 2023 xiaomi inc.
 * Annibale integration: retain the MiCode netlink ABI, serialize publication,
 * and stop IRQ producers before cancelling work or releasing the device.
 */
#include <linux/gpio.h>
#include <linux/hwid.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/pm_wakeup.h>
#include <linux/workqueue.h>
#include <net/genetlink.h>

#define SAR_DEFAULT_DEBOUNCE_MS 50
#define SAR_MAX_DEBOUNCE_MS 1000

enum { SAR_ATTR_UNSPEC, SAR_ATTR_STATUS, SAR_ATTR_MAX = SAR_ATTR_STATUS };
enum { SAR_CMD_UNSPEC, SAR_CMD_WPAS_READY, SAR_CMD_GPIO_STATUS };
enum { SAR_MCGRP_STATUS };

struct wifi_gpio_data {
	struct device *dev;
	int gpio;
	int irq;
	u32 debounce_ms;
	struct delayed_work work;
	spinlock_t event_lock;
	unsigned long generation;
};

/* The ABI has one family/group, hence at most one active board instance. */
static DEFINE_MUTEX(sar_lock);
static struct wifi_gpio_data *sar_device;
static struct genl_family sar_family;

static int sar_send_status(u32 status)
{
	struct sk_buff *msg;
	void *hdr;
	int ret;

	msg = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	hdr = genlmsg_put(msg, 0, 0, &sar_family, 0, SAR_CMD_GPIO_STATUS);
	if (!hdr)
		goto free;
	if (nla_put_u32(msg, SAR_ATTR_STATUS, status)) {
		genlmsg_cancel(msg, hdr);
		goto free;
	}
	genlmsg_end(msg, hdr);
	ret = genlmsg_multicast(&sar_family, msg, 0, SAR_MCGRP_STATUS, GFP_KERNEL);
	return ret == -ESRCH ? 0 : ret;
free:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

static int sar_publish(struct wifi_gpio_data *data)
{
	int status = gpio_get_value_cansleep(data->gpio);

	if (status < 0)
		return status;
	return sar_send_status(!!status);
}

static int sar_wpas_ready(struct sk_buff *skb, struct genl_info *info)
{
	int ret = -ENODEV;

	mutex_lock(&sar_lock);
	if (sar_device)
		ret = sar_publish(sar_device);
	mutex_unlock(&sar_lock);
	return ret;
}

static const struct nla_policy sar_policy[SAR_ATTR_MAX + 1] = {
	[SAR_ATTR_STATUS] = { .type = NLA_U32 },
};
static const struct genl_ops sar_ops[] = {
	{
		.cmd = SAR_CMD_WPAS_READY,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = sar_wpas_ready,
		.flags = GENL_ADMIN_PERM,
	},
};
static const struct genl_multicast_group sar_mcgrps[] = {
	[SAR_MCGRP_STATUS] = { .name = "sar_event" },
};
static struct genl_family sar_family __ro_after_init = {
	.name = "wifi_sar",
	.version = 1,
	.maxattr = SAR_ATTR_MAX,
	.policy = sar_policy,
	.module = THIS_MODULE,
	.ops = sar_ops,
	.n_ops = ARRAY_SIZE(sar_ops),
	.resv_start_op = SAR_CMD_GPIO_STATUS + 1,
	.mcgrps = sar_mcgrps,
	.n_mcgrps = ARRAY_SIZE(sar_mcgrps),
};

static void wifi_gpio_work(struct work_struct *work)
{
	struct wifi_gpio_data *data = container_of(to_delayed_work(work),
						 struct wifi_gpio_data, work);
	unsigned long flags, generation;

	spin_lock_irqsave(&data->event_lock, flags);
	generation = data->generation;
	spin_unlock_irqrestore(&data->event_lock, flags);
	mutex_lock(&sar_lock);
	if (sar_device == data)
		sar_publish(data);
	mutex_unlock(&sar_lock);
	spin_lock_irqsave(&data->event_lock, flags);
	/* A newer edge owns the wake reference until its queued work runs. */
	if (generation == data->generation && !delayed_work_pending(&data->work))
		pm_relax(data->dev);
	spin_unlock_irqrestore(&data->event_lock, flags);
}

static irqreturn_t wifi_gpio_irq(int irq, void *ptr)
{
	struct wifi_gpio_data *data = ptr;
	unsigned long flags;

	spin_lock_irqsave(&data->event_lock, flags);
	data->generation++;
	pm_stay_awake(data->dev);
	mod_delayed_work(system_wq, &data->work,
			 msecs_to_jiffies(data->debounce_ms));
	spin_unlock_irqrestore(&data->event_lock, flags);
	return IRQ_HANDLED;
}

static int wifi_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct wifi_gpio_data *data;
	int ret;

	if (!xiaomi_hwid_is_ready() || get_hw_country_version() != CountryCN)
		return -ENODEV;
	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->dev = dev;
	data->debounce_ms = SAR_DEFAULT_DEBOUNCE_MS;
	if (of_find_property(dev->of_node, "debounce-time", NULL)) {
		ret = of_property_read_u32(dev->of_node, "debounce-time", &data->debounce_ms);
		if (ret)
			return ret;
	}
	if (data->debounce_ms > SAR_MAX_DEBOUNCE_MS)
		return -ERANGE;
	spin_lock_init(&data->event_lock);
	INIT_DELAYED_WORK(&data->work, wifi_gpio_work);
	data->gpio = of_get_named_gpio(dev->of_node, "gpio", 0);
	if (data->gpio < 0)
		return dev_err_probe(dev, data->gpio, "missing SAR GPIO\n");
	ret = devm_gpio_request_one(dev, data->gpio, GPIOF_IN, "xiaomi-wifi-sar");
	if (ret)
		return ret;
	data->irq = gpio_to_irq(data->gpio);
	if (data->irq < 0)
		return data->irq;

	/* Keep duplicate probes and netlink requests out until setup finishes. */
	mutex_lock(&sar_lock);
	if (sar_device) {
		ret = -EBUSY;
		goto unlock;
	}
	ret = device_init_wakeup(dev, true);
	if (ret)
		goto unlock;
	ret = devm_request_threaded_irq(dev, data->irq, NULL, wifi_gpio_irq,
			IRQF_ONESHOT | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"xiaomi_wifi_gpio", data);
	if (ret)
		goto disable_wakeup;
	ret = dev_pm_set_wake_irq(dev, data->irq);
	if (ret) {
		devm_free_irq(dev, data->irq, data);
		/* Work can be waiting for sar_lock: never cancel it under this lock. */
		mutex_unlock(&sar_lock);
		cancel_delayed_work_sync(&data->work);
		pm_relax(dev);
		device_init_wakeup(dev, false);
		return ret;
	}
	sar_device = data;
	platform_set_drvdata(pdev, data);
	mutex_unlock(&sar_lock);
	return 0;
disable_wakeup:
	device_init_wakeup(dev, false);
unlock:
	mutex_unlock(&sar_lock);
	return ret;
}

static int wifi_gpio_remove(struct platform_device *pdev)
{
	struct wifi_gpio_data *data = platform_get_drvdata(pdev);

	dev_pm_clear_wake_irq(data->dev);
	devm_free_irq(data->dev, data->irq, data);
	mutex_lock(&sar_lock);
	if (sar_device == data)
		sar_device = NULL;
	mutex_unlock(&sar_lock);
	cancel_delayed_work_sync(&data->work);
	pm_relax(data->dev);
	device_init_wakeup(data->dev, false);
	return 0;
}

static const struct of_device_id wifi_gpio_of_match[] = {
	{ .compatible = "xiaomi,xiaomi-wifi" },
	{ }
};
MODULE_DEVICE_TABLE(of, wifi_gpio_of_match);
static struct platform_driver wifi_gpio_driver = {
	.driver = {
		.name = "xiaomi_wifi_gpio",
		.of_match_table = wifi_gpio_of_match,
	},
	.probe = wifi_gpio_probe,
	.remove = wifi_gpio_remove,
};

static int __init wifi_gpio_init(void)
{
	int ret;

	if (!xiaomi_hwid_is_ready() || get_hw_country_version() != CountryCN)
		return -ENODEV;
	ret = genl_register_family(&sar_family);
	if (ret)
		return ret;
	ret = platform_driver_register(&wifi_gpio_driver);
	if (ret)
		genl_unregister_family(&sar_family);
	return ret;
}
module_init(wifi_gpio_init);

static void __exit wifi_gpio_exit(void)
{
	platform_driver_unregister(&wifi_gpio_driver);
	genl_unregister_family(&sar_family);
}
module_exit(wifi_gpio_exit);
MODULE_AUTHOR("songgang3@xiaomi.com");
MODULE_DESCRIPTION("Xiaomi Wi-Fi SAR GPIO reporting");
MODULE_LICENSE("GPL v2");
