// SPDX-License-Identifier: GPL-2.0-only
/* Annibale MCA notifications; source interface from the community reconstruction. */
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/string.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_sysfs.h>

static struct blocking_notifier_head mca_chains[MCA_EVENT_TYPE_END];
static struct device *mca_event_dev;

int mca_event_block_notify_register(enum mca_event_notify_type type,
				    struct notifier_block *nb)
{
	if ((unsigned int)type >= MCA_EVENT_TYPE_END || !nb || !nb->notifier_call)
		return -EINVAL;
	return blocking_notifier_chain_register(&mca_chains[type], nb);
}
EXPORT_SYMBOL(mca_event_block_notify_register);

int mca_event_block_notify_unregister(enum mca_event_notify_type type,
				      struct notifier_block *nb)
{
	if ((unsigned int)type >= MCA_EVENT_TYPE_END || !nb)
		return -EINVAL;
	return blocking_notifier_chain_unregister(&mca_chains[type], nb);
}
EXPORT_SYMBOL(mca_event_block_notify_unregister);

void mca_event_block_notify(enum mca_event_notify_type type,
			    unsigned long event, void *data)
{
	if ((unsigned int)type >= MCA_EVENT_TYPE_END)
		return;
	/* Native chain locking permits different chains to forward events. */
	blocking_notifier_call_chain(&mca_chains[type], event, data);
}
EXPORT_SYMBOL(mca_event_block_notify);

/* One bounded KEY=value entry or legacy bare event token, no embedded NUL or line break. */
static int mca_event_format(const struct mca_event_notify_data *data, char *buf)
{
	int i, equals = -1;

	if (!data || !data->event || data->event_len <= 0 ||
	    data->event_len >= MCA_EVENT_NOTIFY_SIZE)
		return -EINVAL;
	for (i = 0; i < data->event_len; i++) {
		unsigned char c = data->event[i];

		if (!c || c == '\n' || c == '\r')
			return -EINVAL;
		if (equals < 0) {
			if (c == '=')
				equals = i;
			else if (!isalnum(c) && c != '_')
				return -EINVAL;
		}
	}
	if (equals == 0)
		return -EINVAL;
	memcpy(buf, data->event, data->event_len);
	buf[data->event_len] = '\0';
	return 0;
}

static int mca_event_send(const struct mca_event_notify_data *data)
{
	char buf[MCA_EVENT_NOTIFY_SIZE];
	char *envp[] = { buf, NULL };
	int ret;

	ret = mca_event_format(data, buf);
	if (ret)
		return ret;
	if (!mca_event_dev)
		return -ENODEV;
	return kobject_uevent_env(&mca_event_dev->kobj, KOBJ_CHANGE, envp);
}

void mca_event_report_uevent(const struct mca_event_notify_data *n_data)
{
	int ret = mca_event_send(n_data);

	if (ret)
		pr_warn_ratelimited("MCA uevent failed: %d\n", ret);
}
EXPORT_SYMBOL(mca_event_report_uevent);

static ssize_t trigger_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct mca_event_notify_data data;
	int ret;
	size_t len = count;

	if (!len || len >= MCA_EVENT_NOTIFY_SIZE)
		return -EINVAL;
	/* Accept the single newline normally appended by echo. */
	if (buf[len - 1] == '\n')
		len--;
	data.event = buf;
	data.event_len = len;
	ret = mca_event_send(&data);
	return ret ? ret : (ssize_t)count;
}
static DEVICE_ATTR(trigger, 0220, NULL, trigger_store);
static struct attribute *mca_event_attrs[] = {
	&dev_attr_trigger.attr, NULL,
};
static const struct attribute_group mca_event_group = {
	.attrs = mca_event_attrs,
};

static int __init mca_event_init(void)
{
	int i;

	for (i = 0; i < MCA_EVENT_TYPE_END; i++)
		BLOCKING_INIT_NOTIFIER_HEAD(&mca_chains[i]);
	mca_event_dev = mca_sysfs_create_group(MCA_SYSFS_CLASS, "mca_event",
					     &mca_event_group);
	return mca_event_dev ? 0 : -ENODEV;
}
static void __exit mca_event_exit(void)
{
	mca_sysfs_remove_group(MCA_SYSFS_CLASS, mca_event_dev, &mca_event_group);
	mca_event_dev = NULL;
}
module_init(mca_event_init);
module_exit(mca_event_exit);
MODULE_DESCRIPTION("Annibale MCA event notifications");
MODULE_LICENSE("GPL");
