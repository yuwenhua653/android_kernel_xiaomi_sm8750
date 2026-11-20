#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "boot_monitor.h"
#include "../boottime/boottime.h"
#include "boot_fail.h"

static unsigned long log_count_2;
static struct log_t **bootprof_2;
static struct task_struct *monitor_main = NULL;
static int count =0;
static int ringnums = 0;
static char prof_node[RPOC_ENTRY_LINE] = {0};
static struct proc_dir_entry *bootmonitor_entry;
struct bootmonitor_context boot_cxt;
int bm_event_counts;
int first_boot = 0;
char *bm_write_buffer = NULL;

char bm_boot_mode[10] = {0};
module_param_string(bootmode, bm_boot_mode, 10, 0644);

//#define MAX_CMDLINE_PARAM_LEN 256
char build_fingerprint[MAX_CMDLINE_PARAM_LEN] = {0};
module_param_string(fingerprint, build_fingerprint, MAX_CMDLINE_PARAM_LEN,0644);

static int blackbox = 0;
module_param(blackbox, int, 0600);
MODULE_PARM_DESC(blackbox,
		"blackbox (default 0)");

struct type_map bm_boot_events[] = {
	{5,  8,   "early-init"},
	{3,  8,   "late-init"},
	{5,  8,   "late-fs"},
	{12, 20,  "zygote-start"},
	{8,  24,  "zygote_preload_end"},
	{3,  12,  "start_android"},
	{6,  12,  "pms_start"},
	{18, 24,  "pms_ready"},
	{30, 40,  "ams_ready"},
	{8,  24,  "boot_completed"},
};

static bool find_event(int nums) {
	struct log_t *p;
	char *occurrence = NULL;
	char *firstboot = NULL;
	int i;
	spin_lock(&bootprof_lock);
	log_count_2 = get_log_count();
	bootprof_2 = get_bootprof_pointer();
	for (i = ringnums; i < log_count_2; i++) {
        count ++;
		p = &bootprof_2[i / LOGS_PER_BUF][i % LOGS_PER_BUF];
		if (!p->comm_event)
			continue;
		/*first boot*/
		firstboot = strstr(p->comm_event + TASK_COMM_LEN, FIRST_BOOT);
		if (firstboot) {
			MTN_PRINT_INFO("%s-%d:first boot\n", __func__, __LINE__);
			first_boot = 1;
		}
		/*p->comm_event + TASK_COMM_LEN = event*/
        occurrence = strstr(p->comm_event + TASK_COMM_LEN, bm_boot_events[nums].name);
        if (!occurrence) {
            continue;
        } else {
            MTN_PRINT_INFO("%s done\n", bm_boot_events[nums].name);
			spin_unlock(&bootprof_lock);
			ringnums = i;
            return true;
        }
	}
	ringnums = i;
	spin_unlock(&bootprof_lock);
	return false;
}

static int write_log_to_dev(struct bootmonitor_context *cxt, char * buffer, int event) {
	int err;

	err = -1;
	if (cxt->oops_buf == NULL || buffer == NULL) {
		MTN_PRINT_ERR("%s-%d:cxt->oops_buf or buffer is NULL\n", __func__, __LINE__);
		return err;
	}
	/*erase blackbox-bootfail-log-partition*/
	err = partition_bm_write(PARTITION_KMSG_OFFSET + HEADER_SIZE_4K, PARTITION_KMSG_SIZE - HEADER_SIZE_4K, buffer);
	if (err != 0) {
		MTN_PRINT_ERR("%s-%d:partition_bm_write error\n", __func__, __LINE__);
		return err;
	}
	err = partition_bm_write(PARTITION_PMSG_OFFSET, PARTITION_PMSG_SIZE, cxt->oops_buf);
	if (err != 0) {
		MTN_PRINT_ERR("%s-%d:partition_bm_write error\n", __func__, __LINE__);
		return err;
	}
	/*get kernel log and logcat*/
	monitor_get_kmsg(buffer);
	monitor_get_pmsg(cxt);
	/*init and update header*/
	err = write_blackbox_header(cxt, buffer, event);
	if (err != 0) {
		MTN_PRINT_ERR("%s-%d:write_blackbox_header error\n", __func__, __LINE__);
		return err;
	}
	/*record log in blackbox-bootfail-partition*/
	err = partition_bm_write(PARTITION_KMSG_OFFSET + HEADER_SIZE_4K, PARTITION_KMSG_SIZE - HEADER_SIZE_4K, buffer);
	if (err != 0) {
		MTN_PRINT_ERR("%s-%d:partition_bm_write error\n", __func__, __LINE__);
		return err;
	}
	err = partition_bm_write(PARTITION_PMSG_OFFSET, PARTITION_PMSG_SIZE, cxt->oops_buf);
	if (err != 0) {
		MTN_PRINT_ERR("%s-%d:partition_bm_write error\n", __func__, __LINE__);
		return err;
	}
	return err;
}

static int monitor_main_thread_body(void *data) {
	bool ret = false;
	int k;
	int err;
	int counts;
	int boottime;
	char bootwarn[RPOC_ENTRY_LINE] = "warning:";
	char booterror[RPOC_ENTRY_LINE] = "error:";
	struct bootmonitor_context *cxt;

	cxt = &boot_cxt;
	boottime = DEFAULT_TIMEOUT - 1;
	MTN_PRINT_START();
	while (!kthread_should_stop()) {
		for(bm_event_counts = 0; bm_event_counts < countof(bm_boot_events); bm_event_counts ++) {
			counts = bm_boot_events[bm_event_counts].warntime + 1;
			MTN_PRINT_INFO("counts = bm_boot_events[%d].warntime = %d\n", bm_event_counts, bm_boot_events[bm_event_counts].warntime);
			for(k = 0; k < counts; k ++) {
				if (kthread_should_stop())
					return 0;
				set_current_state(TASK_INTERRUPTIBLE);
				ret = find_event(bm_event_counts);
				/*wait 1s*/
				if(!ret && k != (counts - 1)) {
					schedule_timeout(msecs_to_jiffies(1000));
					boottime ++;
					continue;
				} else if (!ret && k == bm_boot_events[bm_event_counts].warntime) {
					strncat(bootwarn, bm_boot_events[bm_event_counts].name, sizeof(bm_boot_events[bm_event_counts].name));
					//bm_sendnlmsg(bootwarn);
					/*reset bootwarn*/
					memset(bootwarn, 0, sizeof(bootwarn));
					snprintf(bootwarn, sizeof(bootwarn), "warning:");
					/*set counts to error time*/
					counts = bm_boot_events[bm_event_counts].errortimes + 1;
					MTN_PRINT_INFO("counts = bm_boot_events[%d].errortimes = %d\n", bm_event_counts, bm_boot_events[bm_event_counts].errortimes);
					/*if first boot, ams ready time to long*/
					if (first_boot == 1 && bm_event_counts == AMS_READY) {
						counts = AMS_READY_TIME;
						MTN_PRINT_INFO("first_boot:counts = %d\n", counts);
					}
					schedule_timeout(msecs_to_jiffies(1000));
					boottime ++;
					continue;
				} else if (boottime == DEFAULT_BOOT_TIME) {
					MTN_PRINT_INFO("%s timeout:boottime is %d line=%d\n", __func__, boottime,  __LINE__);
					break;
				} else {
					break;
				}
			}
			MTN_PRINT_INFO("%s-%d:set task to running\n", __func__,  __LINE__);
			set_current_state(TASK_RUNNING);
			if((k == bm_boot_events[bm_event_counts].errortimes || boottime == DEFAULT_BOOT_TIME) && !ret) {
				strncat(booterror, bm_boot_events[bm_event_counts].name, sizeof(bm_boot_events[bm_event_counts].name));
				strncpy(prof_node, booterror, sizeof(booterror));
				//bm_sendnlmsg(booterror);
				if(!get_bm_devices()) {
					err = write_log_to_dev(cxt, bm_write_buffer, bm_event_counts);
				}
				MTN_PRINT_INFO("error write end\n");
				return 0;
			}
		}
		strncpy(prof_node, BOOT_OK, sizeof(BOOT_OK));
		MTN_PRINT_END();
		return 0;
	}
	MTN_PRINT_END();
	return 0;
}

static ssize_t
boot_monitor_write(struct file *filp, const char *ubuf, size_t cnt, loff_t *data)
{
	char buf[BOOT_MONITOR_SIZE];
	size_t copy_size = cnt;

	if (cnt >= sizeof(buf))
		copy_size = BOOT_MONITOR_SIZE - 1;

	if (copy_from_user(&buf, ubuf, copy_size))
		return -EFAULT;

	buf[copy_size] = 0;
	memset(prof_node, 0, sizeof(prof_node));
	strncpy(prof_node, buf, sizeof(buf));

	return cnt;
}

static int boot_monitor_show(struct seq_file *m, void *v)
{
	if (!m) {
		MTN_PRINT_ERR("seq_file is Null.\n");
		return 0;
	}
	seq_printf(m, "%s\n", prof_node);

	return 0;
}

static int boot_monitor_open(struct inode *inode, struct file *file)
{
	return single_open(file, boot_monitor_show, inode->i_private);
}

static const struct proc_ops boot_monitor_fops = {
	.proc_open = boot_monitor_open,
	.proc_write = boot_monitor_write,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static bool monitor_registered;

static void boot_monitor_release(void)
{
	if (monitor_main) {
		kthread_stop(monitor_main);
		put_task_struct(monitor_main);
		monitor_main = NULL;
	}
	proc_remove(bootmonitor_entry);
	bootmonitor_entry = NULL;
	if (monitor_registered) {
		platform_driver_unregister(&boot_monitor_driver);
		monitor_registered = false;
	}
	if (!IS_ERR_OR_NULL(bdev)) {
		sync_blockdev(bdev);
		invalidate_mapping_pages(bdev->bd_inode->i_mapping, 0, -1);
		blkdev_put(bdev, THIS_MODULE);
	}
	bdev = NULL;
	vfree(bm_write_buffer);
	bm_write_buffer = NULL;
	vfree(boot_cxt.oops_buf);
	boot_cxt.oops_buf = NULL;
}

static int __init boot_monitor_init(void)
{
	int ret;

	if (boottime_ok != 1)
		return -ENODEV;
	/* Preserve OEM opt-in and boot-mode policy. */
	if (blackbox != BLACKBOX_VERSION ||
	    strcmp(BOOT_MODE_NORMAL, bm_boot_mode))
		return 0;

	bm_write_buffer = vzalloc(PARTITION_KMSG_SIZE - HEADER_SIZE_4K);
	boot_cxt.oops_buf = vzalloc(PARTITION_PMSG_SIZE);
	if (!bm_write_buffer || !boot_cxt.oops_buf) {
		ret = -ENOMEM;
		goto fail;
	}
	ret = platform_driver_register(&boot_monitor_driver);
	if (ret)
		goto fail;
	monitor_registered = true;
	bootmonitor_entry = proc_create("boot_monitor", 0664, NULL, &boot_monitor_fops);
	if (!bootmonitor_entry) {
		ret = -ENOMEM;
		goto fail;
	}
	monitor_main = kthread_create(monitor_main_thread_body, NULL, "monitor_main");
	if (IS_ERR(monitor_main)) {
		ret = PTR_ERR(monitor_main);
		monitor_main = NULL;
		goto fail;
	}
	/* Retain the task if the finite boot-monitor worker exits first. */
	get_task_struct(monitor_main);
	wake_up_process(monitor_main);
	return 0;
fail:
	boot_monitor_release();
	return ret;
}

static void __exit boot_monitor_exit(void)
{
	boot_monitor_release();
}

module_init(boot_monitor_init);
module_exit(boot_monitor_exit);
MODULE_LICENSE("GPL v2");
