/* SPDX-License-Identifier: GPL-2.0-only */
struct notifier_block { int (*notifier_call)(void); };
struct blocking_notifier_head { int dummy; };
struct device { int kobj; };
struct device_attribute { int dummy; };
#define KOBJ_CHANGE 1
#define pr_warn_ratelimited(...) ((void)0)
static int registrations, unregistrations, notifications, sends, send_error;
static char sent[128];
static int blocking_notifier_chain_register(struct blocking_notifier_head *head, struct notifier_block *nb)
{ registrations++; return 0; }
static int blocking_notifier_chain_unregister(struct blocking_notifier_head *head, struct notifier_block *nb)
{ unregistrations++; return 0; }
static void blocking_notifier_call_chain(struct blocking_notifier_head *head, unsigned long event, void *data)
{ notifications++; }
static int kobject_uevent_env(int *kobj, int event, char **env)
{ sends++; strcpy(sent, env[0]); return send_error; }
static int callback(void) { return 0; }
/* PRODUCTION */
int main(void)
{
	struct device dev = { 0 };
	struct notifier_block nb = { .notifier_call = callback };
	struct mca_event_notify_data data = { .event = "K=1", .event_len = 3 };
	char out[128], long_event[128];
	assert(mca_event_block_notify_register((enum mca_event_notify_type)-1, &nb) == -EINVAL);
	assert(mca_event_block_notify_register(MCA_EVENT_TYPE_END, &nb) == -EINVAL);
	assert(mca_event_block_notify_register(MCA_EVENT_TYPE_BATTERY_INFO, NULL) == -EINVAL);
	assert(!registrations);
	assert(!mca_event_block_notify_register(MCA_EVENT_TYPE_BATTERY_INFO, &nb) && registrations == 1);
	mca_event_block_notify((enum mca_event_notify_type)-1, 17, NULL);
	assert(!notifications);
	mca_event_block_notify(MCA_EVENT_TYPE_BATTERY_INFO, 17, NULL);
	assert(notifications == 1);
	assert(!mca_event_block_notify_unregister(MCA_EVENT_TYPE_BATTERY_INFO, &nb));
	assert(unregistrations == 1);
	assert(mca_event_block_notify_unregister((enum mca_event_notify_type)-1, &nb) == -EINVAL);
	assert(!mca_event_format(&data, out) && !strcmp(out, "K=1"));
	assert(mca_event_send(&data) == -ENODEV);
	mca_event_dev = &dev;
	assert(!mca_event_send(&data) && sends == 1);
	data.event_len = -1;
	assert(mca_event_format(&data, out) == -EINVAL);
	data.event = "K\0X"; data.event_len = 3;
	assert(mca_event_format(&data, out) == -EINVAL);
	data.event = "=X"; data.event_len = 2;
	assert(mca_event_format(&data, out) == -EINVAL);
	memset(long_event, 'X', sizeof(long_event)); data.event = long_event;
	data.event_len = 127;
	assert(!mca_event_format(&data, out) && out[127] == 0);
	data.event_len = 128;
	assert(mca_event_format(&data, out) == -EINVAL);
	assert(trigger_store(&dev, NULL, "K=1\n", 4) == 4 && !strcmp(sent, "K=1"));
	assert(trigger_store(&dev, NULL, "MCA_LOG_FULL_EVENT\n", sizeof("MCA_LOG_FULL_EVENT\n")) == -EINVAL); /* count includes NUL */
	assert(trigger_store(&dev, NULL, "MCA_LOG_FULL_EVENT\n", strlen("MCA_LOG_FULL_EVENT\n")) == (ssize_t)strlen("MCA_LOG_FULL_EVENT\n"));
	assert(trigger_store(&dev, NULL, "K=1\nX=2", 7) == -EINVAL);
	send_error = -ENOMEM;
	assert(trigger_store(&dev, NULL, "K=1", 3) == -ENOMEM);
	mca_event_report_uevent(&data); /* legacy void API logs, never reports success */
	puts("PASS: event chain bounds, bare/KEY=value tokens, embedded NUL/newline limits and propagated uevent failures");
	return 0;
}
