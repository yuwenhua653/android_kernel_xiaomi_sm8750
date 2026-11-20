#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/swap.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/atomic/atomic-instrumented.h>

#include <trace/hooks/mm.h>
#include <trace/hooks/vmscan.h>

enum mi_folio_references {
	MI_FOLIOREF_RECLAIM,
	MI_FOLIOREF_RECLAIM_CLEAN,
	MI_FOLIOREF_KEEP,
	MI_FOLIOREF_ACTIVATE,
};

static unsigned int mi_mapcount_thres __read_mostly = 32;
static bool mi_rmap_efficiency_setup = true;
static struct kobject *kobj;

#define MI_TOO_MANY_SKIPPED_SHIFT 4
#define MI_SKIP_THRES_MIN 3
#define MI_SKIP_THRES_LOW 2
#define MI_SKIP_THRES_HIGH 1

static int get_dynamic_mapcount_thres(s8 priority, unsigned int threshold)
{
	if (priority <= DEF_PRIORITY / 4) {
		return max(threshold >> MI_SKIP_THRES_MIN, 2u);
	}
	else if (priority <= DEF_PRIORITY / 2) {
		return max(threshold >> MI_SKIP_THRES_LOW, 2u);
	}
	else {
		return max(threshold >> MI_SKIP_THRES_HIGH, 2u);
	}
}

static void mi_folio_check_mapcount(void *data, struct folio *folio, unsigned long nr_scanned,
	s8 priority, u64 *nr_skipped, int *control)
{
	int mapcount = 0;
	int activate_thres = 0;
	int keep_thres = 0;
	bool too_many_skipped = false;

	if (!READ_ONCE(mi_rmap_efficiency_setup) || *control) {
		return;
	}

        /*
         * Reset the skipped page counter if the number of scanned pages is less than the number of
         * skipped pages.This ensures that the skipped page count does not exceed the scanned page
         * count, which could happen due to interruptions in the scanning process(e.g., high system
         * load or priority adjustments) or potential logic errors. Resetting the counter prevents
         * incorrect behavior in subsequent logic, such as determining whether too many pages have
         * been skipped.
         */
	if (nr_scanned < *nr_skipped) {
		*nr_skipped = 0;
	}

	if ((priority < DEF_PRIORITY - 2) && (nr_scanned >> MI_TOO_MANY_SKIPPED_SHIFT > 0)) {
		too_many_skipped = *nr_skipped > (nr_scanned >> MI_TOO_MANY_SKIPPED_SHIFT);
		if (too_many_skipped) {
			*control = MI_FOLIOREF_RECLAIM;
			return;
		}
	}

	activate_thres = READ_ONCE(mi_mapcount_thres);
	if (priority != DEF_PRIORITY) {
		keep_thres = get_dynamic_mapcount_thres(priority, activate_thres);
	}

	mapcount = folio_mapcount(folio);
	if (mapcount >= activate_thres) {
		unsigned int nr_pages;

		nr_pages = folio_nr_pages(folio);
		*nr_skipped += nr_pages;
		*control = MI_FOLIOREF_ACTIVATE;

		return;
	}
	else if (keep_thres >=2 && mapcount >= keep_thres) {
		*control = MI_FOLIOREF_KEEP;
		return;
	}

	*control = MI_FOLIOREF_RECLAIM;

	return;
}

static ssize_t show_mi_mapcount_thres(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", READ_ONCE(mi_mapcount_thres));
}

static ssize_t store_mi_mapcount_thres(struct kobject *kobj, struct kobj_attribute *attr,
			     const char *buf, size_t len)
{
	unsigned int thres;

	if (kstrtouint(buf, 0, &thres))
		return -EINVAL;

	if (thres < 2 || thres > INT_MAX)
		return -EINVAL;

	WRITE_ONCE(mi_mapcount_thres, thres);

	return len;
}

static struct kobj_attribute mi_mapcount_thres_attr = __ATTR(
	mapcount_thres, 0644, show_mi_mapcount_thres, store_mi_mapcount_thres
);

static ssize_t show_mi_rmap_efficiecnt_setup(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	return sysfs_emit(buf, "%s\n", READ_ONCE(mi_rmap_efficiency_setup) ? "true" : "false");
}

static ssize_t store_mi_rmap_efficiency_setup(struct kobject *kobj, struct kobj_attribute *attr,
			     const char *buf, size_t len)
{
	ssize_t ret;
	bool enabled;

	ret = kstrtobool(buf, &enabled);
	if (ret)
		return ret;
	WRITE_ONCE(mi_rmap_efficiency_setup, enabled);
	return len;
}

static struct kobj_attribute mi_rmap_check_enable_attr = __ATTR(
	enabled, 0644, show_mi_rmap_efficiecnt_setup, store_mi_rmap_efficiency_setup
);

static struct attribute *mi_rmap_check_attrs[] = {
	&mi_mapcount_thres_attr.attr,
	&mi_rmap_check_enable_attr.attr,
	NULL
};

static struct attribute_group mi_rmap_check_attr_group = {
	.name = "mi_rmap_efficiency_check",
	.attrs = mi_rmap_check_attrs,
};

static int __init mi_rmap_efficiency_init(void)
{
	int ret;

	kobj = kobject_create_and_add("mi_rmap_efficiency", &THIS_MODULE->mkobj.kobj);
	if (!kobj)
		return -ENOMEM;
	ret = sysfs_create_group(kobj, &mi_rmap_check_attr_group);
	if (ret)
		goto put_kobj;
	ret = register_trace_android_vh_page_should_be_protected(mi_folio_check_mapcount, NULL);
	if (ret) {
		sysfs_remove_group(kobj, &mi_rmap_check_attr_group);
		goto put_kobj;
	}
	return 0;
 put_kobj:
	kobject_put(kobj);
	kobj = NULL;
	return ret;
}

static void __exit mi_rmap_efficiency_exit(void)
{
	unregister_trace_android_vh_page_should_be_protected(mi_folio_check_mapcount, NULL);
	tracepoint_synchronize_unregister();
	sysfs_remove_group(kobj, &mi_rmap_check_attr_group);
	kobject_put(kobj);
}
module_init(mi_rmap_efficiency_init);
module_exit(mi_rmap_efficiency_exit);

MODULE_AUTHOR("maminghui5 <maminghui5@xiaomi.com>");
MODULE_LICENSE("GPL");
