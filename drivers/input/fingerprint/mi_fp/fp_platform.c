#include "fp_driver.h"
#include <linux/hwid.h>

/* -------------------------------------------------------------------- */
/* fingerprint chip hardware configuration				*/
/* -------------------------------------------------------------------- */
#define DTS_NETLINK_NUM             "netlink-event"
#define SENSORLOCATION              "sensor-loc"
#define DTS_RST_HIGH_TIME           "xiaomi,rst_high_time"
#define DTS_RST_LOW_TIME            "xiaomi,rst_low_time"
#define DTS_IRQ_GPIO                "xiaomi,gpio_irq"
#define DTS_VENDOR_NAME             "xiaomi,vendor_names"
#define DTS_PINCTL_RESET_HIGH       "reset_high"
#define DTS_PINCTL_RESET_LOW        "reset_low"
#ifndef FP_ULTRA_QCOM_P11
#define DTS_RST_PULL_FLAG           "reset-flag"
#else
#define DTS_PINCTL_IRQ_DEFAULT      "irq_default"
#define DTS_PINCTL_IRQ_PULLDOWN     "irq_pulldown"
#define DTS_RESET_PIN_ENABLE_FLAG   "reset-pin-enable"
#define DTS_RESET_PULL_FLAG         "reset-flag"
#define DTS_INTR2_ENABLE_FLAG       "intr2-enable"
#define DTS_INTR2_SIDE_CONFIG       "intr2-side-config"
#endif

#define HW_LEVEL_P01                 1
#define HW_LEVEL_MASK               0XFF0F000F

/* Annibale-only resource acquisition. Do not guess a project or board revision. */
int fp_parse_dts(struct fp_device *fp_dev)
{
	struct device *dev = &fp_dev->driver_device->dev;
	struct device_node *node = dev->of_node;
	const char *side, *supply;
	u32 identity, hw_level;
	int ret;

	if (!node || !xiaomi_hwid_is_ready() ||
	    get_hw_version_platform() != HARDWARE_PROJECT_P11 ||
	    strcmp(product_name_get(), "annibale"))
		return -ENODEV;
	fp_dev->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR(fp_dev->pinctrl))
		return PTR_ERR(fp_dev->pinctrl);
	fp_dev->pins_int_default = pinctrl_lookup_state(fp_dev->pinctrl, DTS_PINCTL_IRQ_DEFAULT);
	if (IS_ERR(fp_dev->pins_int_default))
		return PTR_ERR(fp_dev->pins_int_default);
	/* Absent from the released Annibale DT; callers must handle EOPNOTSUPP. */
	fp_dev->pins_int_pulldown = pinctrl_lookup_state(fp_dev->pinctrl, DTS_PINCTL_IRQ_PULLDOWN);
	if (PTR_ERR_OR_ZERO(fp_dev->pins_int_pulldown) == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	ret = fp_select_state(fp_dev->pinctrl, fp_dev->pins_int_default);
	if (ret)
		return ret;
	fp_dev->irq_gpio = of_get_named_gpio(node, DTS_IRQ_GPIO, 0);
	if (fp_dev->irq_gpio < 0)
		return fp_dev->irq_gpio;
	ret = devm_gpio_request_one(dev, fp_dev->irq_gpio, GPIOF_IN, "xiaomi-fp");
	if (ret)
		return ret;
	fp_dev->irq_num = gpio_to_irq(fp_dev->irq_gpio);
	if (fp_dev->irq_num < 0)
		return fp_dev->irq_num;
	fp_dev->fw_ipc.gpio = of_get_named_gpio(node, DTS_IPC_GPIO, 0);
	if (fp_dev->fw_ipc.gpio < 0)
		return fp_dev->fw_ipc.gpio;
	if (fp_dev->fw_ipc.gpio != fp_dev->irq_gpio) {
		ret = devm_gpio_request_one(dev, fp_dev->fw_ipc.gpio, GPIOF_IN, "xiaomi-fp-ipc");
		if (ret)
			return ret;
	}
	fp_dev->fw_ipc.irq = gpio_to_irq(fp_dev->fw_ipc.gpio);
	if (fp_dev->fw_ipc.irq < 0)
		return fp_dev->fw_ipc.irq;
	ret = of_property_read_u32(node, DTS_NETLINK_NUM, &fp_dev->fp_netlink_num);
	if (ret || !fp_dev->fp_netlink_num || fp_dev->fp_netlink_num >= MAX_LINKS)
		return -EINVAL;
	ret = of_property_read_string(node, DTS_VENDOR_NAME, &fp_dev->vendor_names);
	if (ret)
		return ret;
	if (of_property_read_bool(node, DTS_INTR2_ENABLE_FLAG)) {
		ret = of_property_read_string(node, DTS_INTR2_SIDE_CONFIG, &side);
		if (ret)
			return ret;
		/* Only the published TIC-side P11 wiring is reviewed. */
		if (strcmp(side, "TIC"))
			return -EOPNOTSUPP;
		fp_dev->intr2_side_config = 2;
	}
	identity = get_hw_id_value();
	hw_level = ((identity >> 16) & 0xf) * 10 + (identity & 0xf);
	supply = hw_level == HW_LEVEL_P01 ? DTS_VOlT_REGULATER_3V1 : DTS_VOlT_REGULATER_3V3;
	fp_dev->vreg_3v3.mRegulator = devm_regulator_get(dev, supply);
	if (IS_ERR(fp_dev->vreg_3v3.mRegulator))
		return PTR_ERR(fp_dev->vreg_3v3.mRegulator);
	fp_dev->vreg_1v8.mRegulator = devm_regulator_get(dev, DTS_VOlT_REGULATER_1V8);
	if (IS_ERR(fp_dev->vreg_1v8.mRegulator))
		return PTR_ERR(fp_dev->vreg_1v8.mRegulator);
	/* P11 DT has no separate gpio_pwr or reset-pin-enable property. */
	if (of_property_read_bool(node, DTS_RESET_PIN_ENABLE_FLAG))
		return -EOPNOTSUPP;
	atomic_set(&fp_dev->ipc_available, 1);
	atomic_set(&fp_dev->wakelock_acquired, 0);
	mutex_init(&fp_dev->mutex);
	mutex_init(&fp_dev->ipc_events_mutex);
	return 0;
}

void fp_power_config(struct fp_device *fp_dev)
{
	fp_dev->vreg_3v3.enable = false;
	fp_dev->vreg_1v8.enable = false;
	fp_dev->vreg_gpio.enable = false;

	fp_dev->vreg_3v3.IsGpio = false;
	fp_dev->vreg_1v8.IsGpio = false;
	fp_dev->vreg_gpio.IsGpio = true;

	fp_dev->vreg_3v3.mRegulator = NULL;
	fp_dev->vreg_1v8.mRegulator = NULL;
	fp_dev->vreg_gpio.mPwrGpio = -EINVAL;
}

int fp_power_on(struct fp_vreg *vreg)
{
	int ret, voltage;

	if (vreg->IsGpio || IS_ERR_OR_NULL(vreg->mRegulator))
		return -ENODEV;
	if (vreg->enable)
		return 0;
	ret = regulator_enable(vreg->mRegulator);
	if (ret)
		return ret;
	vreg->enable = true;
	usleep_range(1000, 1500);
	voltage = regulator_get_voltage(vreg->mRegulator);
	if (voltage < 0) {
		ret = voltage;
		goto rollback;
	}
	/* Preserve the donor's P01 voltage transition, with one enable reference. */
	if (voltage == 3100000) {
		ret = regulator_set_voltage(vreg->mRegulator, 3300000, 3300000);
		if (ret)
			goto rollback;
		usleep_range(1000, 1500);
	}
	return 0;
rollback:
	if (!regulator_disable(vreg->mRegulator))
		vreg->enable = false;
	return ret;
}

int fp_power_off(struct fp_vreg *vreg)
{
	int ret;

	if (vreg->IsGpio || IS_ERR_OR_NULL(vreg->mRegulator))
		return -ENODEV;
	if (!vreg->enable)
		return 0;
	ret = regulator_disable(vreg->mRegulator);
	if (!ret) {
		vreg->enable = false;
		usleep_range(1000, 1500);
	}
	return ret;
}

void fp_hw_reset(struct fp_device *fp_dev)
{
	fp_select_state(fp_dev->pinctrl, fp_dev->pins_reset_low);
	mdelay(fp_dev->rst_low_delay);
	fp_select_state(fp_dev->pinctrl, fp_dev->pins_reset_high);
	mdelay(fp_dev->rst_high_delay);
}

/* Delay adjustable time before and after reset */
void fp_hw_reset_delay(struct fp_device *fp_dev, u_int32_t rst_low_delay, u_int32_t rst_high_delay)
{
	pr_debug( "rst_low time = %u, rst_high time = %u.\n", rst_low_delay, rst_high_delay);
	fp_select_state(fp_dev->pinctrl, fp_dev->pins_reset_low);
	mdelay(rst_low_delay);
	fp_select_state(fp_dev->pinctrl, fp_dev->pins_reset_high);
	mdelay(rst_high_delay);
}
void fp_acquire_stay_wake_time_ms(struct fp_device *fp_dev, unsigned int msec)
{
	if (fp_dev->wakesrc == NULL) {
		pr_err("fp wakeup source time is NULL\n");
		return;
	}
	__pm_wakeup_event(fp_dev->wakesrc, msec);
}

void fp_acquire_stay_wake(struct fp_device *fp_dev)
{
	if (fp_dev->wakesrc == NULL) {
	pr_err("fp wakeup source is NULL\n");
	return;
	}
	__pm_stay_awake(fp_dev->wakesrc);
}

void fp_release_stay_wake(struct fp_device *fp_dev)
{
	if (fp_dev->wakesrc == NULL) {
	pr_err("fp wakeup source is NULL\n");
	return;
	}
	__pm_relax(fp_dev->wakesrc);
}
void fp_enable_irq(struct fp_device *fp_dev)
{
	if (!fp_dev->irq_request)
		return;
	if (1 == fp_dev->irq_enabled) {
		pr_debug( "irq already enabled\n");
	} else {
		enable_irq(fp_dev->irq_num);
		fp_dev->irq_enabled = 1;
		pr_debug( "enable irq!\n");
	}
}

void fp_disable_irq(struct fp_device *fp_dev)
{
	if (!fp_dev->irq_request)
		return;
	if (0 == fp_dev->irq_enabled) {
		pr_debug( "irq already disabled\n");
	} else {
		disable_irq(fp_dev->irq_num);
		fp_dev->irq_enabled = 0;
		pr_debug("disable irq!\n");
	}
}

void fp_kernel_key_input(struct fp_device *fp_dev, struct fp_key *fp_key)
{
	uint32_t key_input = 0;

	if (FP_KEY_HOME == fp_key->key) {
		key_input = FP_KEY_INPUT_HOME;
	} else if (FP_KEY_HOME_DOUBLE_CLICK == fp_key->key) {
                key_input = FP_KEY_DOUBLE_CLICK;
        } else if (FP_KEY_POWER == fp_key->key) {
		key_input = FP_KEY_INPUT_POWER;
	} else if (FP_KEY_CAMERA == fp_key->key) {
		key_input = FP_KEY_INPUT_CAMERA;
	} else {
		/* add special key define */
		key_input = fp_key->key;
	}

	pr_debug("received key event[%d], key=%d, value=%d\n",
		 key_input, fp_key->key, fp_key->value);

	if ((FP_KEY_POWER == fp_key->key || FP_KEY_CAMERA == fp_key->key)
		&& (fp_key->value == 1)) {
		input_report_key(fp_dev->input, key_input, 1);
		input_sync(fp_dev->input);
		input_report_key(fp_dev->input, key_input, 0);
		input_sync(fp_dev->input);
	}

	if (FP_KEY_HOME_DOUBLE_CLICK == fp_key->key) {
                pr_debug("input report key event double click");
		input_report_key(fp_dev->input, key_input, fp_key->value);
		input_sync(fp_dev->input);
	}
}

void fp_local_time_printk(const char *format, ...)
{
	struct timespec64 tv;
	struct rtc_time tm;
	unsigned long local_time;
	struct va_format vaf;
	va_list args;

	ktime_get_real_ts64(&tv);
	/* Convert rtc to local time */
	local_time = (u32)(tv.tv_sec - (sys_tz.tz_minuteswest * 60));
	rtc_time64_to_tm(local_time, &tm);

	va_start(args, format);
	vaf.fmt = format;
	vaf.va = &args;

	printk("xiaomi-fp [%d-%02d-%02d %02d:%02d:%02d.%06lu] %pV",
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_nsec / 1000,
			&vaf);

	va_end(args);
}
