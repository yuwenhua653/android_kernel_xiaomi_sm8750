// SPDX-License-Identifier: GPL-2.0-only
/* Annibale xm_power namespace. Interface from the GPL community reconstruction. */
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <mca/common/mca_sysfs.h>

struct mca_sysfs_node {
	struct list_head node;
	struct device *dev;
	const struct attribute_group *group; /* NULL: fixed core device */
	char *name;
	const struct attribute_group *groups[2];
};
static struct class *mca_power_class;
static LIST_HEAD(mca_devices);
static DEFINE_MUTEX(mca_sysfs_lock);
static const char * const mca_fixed_devices[] = {
	MCA_SYSFS_DEV_CHARGER, MCA_SYSFS_DEV_FUELGAUGE, MCA_SYSFS_DEV_TYPEC,
	MCA_SYSFS_DEV_BATTERY, MCA_SYSFS_DEV_HW_MONITOR,
};

/* Returned reference must be released with put_device(). */
static struct device *mca_sysfs_find_dev(const char *name)
{
	struct mca_sysfs_node *node;
	struct device *dev = NULL;

	if (!name)
		return NULL;
	mutex_lock(&mca_sysfs_lock);
	list_for_each_entry(node, &mca_devices, node) {
		if (!strcmp(node->name, name)) {
			dev = get_device(node->dev);
			break;
		}
	}
	mutex_unlock(&mca_sysfs_lock);
	return dev;
}

static struct device *mca_sysfs_add_device(const char *name,
					 const struct attribute_group *group)
{
	struct mca_sysfs_node *node, *pos;
	struct device *dev;
	int ret;

	if (!name || !*name || strchr(name, '/'))
		return ERR_PTR(-EINVAL);
	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return ERR_PTR(-ENOMEM);
	node->name = kstrdup(name, GFP_KERNEL);
	if (!node->name) {
		kfree(node);
		return ERR_PTR(-ENOMEM);
	}
	mutex_lock(&mca_sysfs_lock);
	list_for_each_entry(pos, &mca_devices, node) {
		if (!strcmp(pos->name, name)) {
			ret = -EEXIST;
			goto fail;
		}
	}
	/* Register attributes atomically with the device, avoiding a failure path
	 * that waits for active attributes while holding the registry lock.
	 */
	if (group) {
		node->groups[0] = group;

		dev = device_create_with_groups(mca_power_class, NULL, 0, NULL,
						node->groups, "%s", name);
	} else {
		dev = device_create(mca_power_class, NULL, 0, NULL, "%s", name);
	}
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto fail;
	}
	node->dev = dev;
	node->group = group;
	list_add_tail(&node->node, &mca_devices);
	mutex_unlock(&mca_sysfs_lock);
	return dev;
fail:
	mutex_unlock(&mca_sysfs_lock);
	kfree(node->name);
	kfree(node);
	return ERR_PTR(ret);
}

struct device *mca_sysfs_create_group(const char *cls_name, const char *dev_name,
				      const struct attribute_group *group)
{
	struct device *dev;

	if (!cls_name || strcmp(cls_name, MCA_SYSFS_CLASS) || !group)
		return NULL;
	dev = mca_sysfs_add_device(dev_name, group);
	return IS_ERR(dev) ? NULL : dev;
}
EXPORT_SYMBOL(mca_sysfs_create_group);

void mca_sysfs_remove_group(const char *cls_name, struct device *dev,
			    const struct attribute_group *group)
{
	struct mca_sysfs_node *node, *found = NULL;

	if (!cls_name || strcmp(cls_name, MCA_SYSFS_CLASS) || !dev || !group)
		return;
	mutex_lock(&mca_sysfs_lock);
	list_for_each_entry(node, &mca_devices, node) {
		if (node->dev == dev && node->group == group) {
			list_del(&node->node);
			found = node;
			break;
		}
	}
	mutex_unlock(&mca_sysfs_lock);
	if (!found)
		return;
	device_unregister(found->dev);
	kfree(found->name);
	kfree(found);
}
EXPORT_SYMBOL(mca_sysfs_remove_group);

int mca_sysfs_create_link_group(const char *dev_name, const char *link_name,
				struct device *target_dev,
				const struct attribute_group *group)
{
	struct device *dev;
	int ret;

	if (!dev_name || !link_name || !*link_name || strchr(link_name, '/') ||
	    !target_dev || !group)
		return -EINVAL;
	dev = mca_sysfs_find_dev(dev_name);
	if (!dev)
		return -ENODEV;
	ret = sysfs_create_group(&target_dev->kobj, group);
	if (!ret) {
		ret = sysfs_create_link(&dev->kobj, &target_dev->kobj, link_name);
		if (ret)
			sysfs_remove_group(&target_dev->kobj, group);
	}
	put_device(dev);
	return ret;
}
EXPORT_SYMBOL(mca_sysfs_create_link_group);

void mca_sysfs_remove_link_group(const char *dev_name, const char *link_name,
				 struct device *target_dev,
				 const struct attribute_group *group)
{
	struct device *dev;

	if (!dev_name || !link_name || !target_dev || !group)
		return;
	dev = mca_sysfs_find_dev(dev_name);
	if (dev) {
		sysfs_remove_link(&dev->kobj, link_name);
		put_device(dev);
	}
	/* Target attributes must go even if the class device was removed first. */
	sysfs_remove_group(&target_dev->kobj, group);
}
EXPORT_SYMBOL(mca_sysfs_remove_link_group);

int mca_sysfs_create_files(const char *dev_name,
			   struct mca_sysfs_attr_info *attr_info, int size)
{
	struct device *dev;
	int i, ret = 0;

	if (!dev_name || !attr_info || size <= 0)
		return -EINVAL;
	dev = mca_sysfs_find_dev(dev_name);
	if (!dev)
		return -ENODEV;
	for (i = 0; i < size; i++) {
		ret = sysfs_create_file(&dev->kobj, &attr_info[i].attr.attr);
		if (ret) {
			/* Do not remove the failing file: it may belong to someone else. */
			while (i--)
				sysfs_remove_file(&dev->kobj, &attr_info[i].attr.attr);
			break;
		}
	}
	put_device(dev);
	return ret;
}
EXPORT_SYMBOL(mca_sysfs_create_files);

void mca_sysfs_remove_files(const char *dev_name,
			    struct mca_sysfs_attr_info *attr_info, int size)
{
	struct device *dev;
	int i;

	if (!dev_name || !attr_info || size <= 0)
		return;
	dev = mca_sysfs_find_dev(dev_name);
	if (!dev)
		return;
	for (i = 0; i < size; i++)
		sysfs_remove_file(&dev->kobj, &attr_info[i].attr.attr);
	put_device(dev);
}
EXPORT_SYMBOL_GPL(mca_sysfs_remove_files);

void mca_sysfs_init_attrs(struct attribute **attrs,
			  struct mca_sysfs_attr_info *attr_info, int size)
{
	int i;

	if (!attrs || !attr_info || size < 0)
		return;
	for (i = 0; i < size; i++)
		attrs[i] = &attr_info[i].attr.attr;
	attrs[size] = NULL;
}
EXPORT_SYMBOL(mca_sysfs_init_attrs);

struct mca_sysfs_attr_info *mca_sysfs_lookup_attr(const char *name,
						struct mca_sysfs_attr_info *attrs,
						int size)
{
	int i;

	if (!name || !attrs)
		return NULL;
	for (i = 0; i < size; i++)
		if (attrs[i].attr.attr.name && !strcmp(name, attrs[i].attr.attr.name))
			return &attrs[i];
	return NULL;
}
EXPORT_SYMBOL(mca_sysfs_lookup_attr);

static void mca_sysfs_cleanup(void)
{
	struct mca_sysfs_node *node, *tmp;

	/* Init failure or module exit only; dependent clients must already stop. */
	list_for_each_entry_safe(node, tmp, &mca_devices, node) {
		list_del(&node->node);
		device_unregister(node->dev);
		kfree(node->name);
		kfree(node);
	}
	class_destroy(mca_power_class);
}

static int __init mca_sysfs_init(void)
{
	struct device *dev;
	int i;

	mca_power_class = class_create(MCA_SYSFS_CLASS);
	if (IS_ERR(mca_power_class))
		return PTR_ERR(mca_power_class);
	for (i = 0; i < ARRAY_SIZE(mca_fixed_devices); i++) {
		dev = mca_sysfs_add_device(mca_fixed_devices[i], NULL);
		if (IS_ERR(dev)) {
			mca_sysfs_cleanup();
			return PTR_ERR(dev);
		}
	}
	return 0;
}
static void __exit mca_sysfs_exit(void)
{
	mca_sysfs_cleanup();
}
module_init(mca_sysfs_init);
module_exit(mca_sysfs_exit);
MODULE_DESCRIPTION("Annibale MCA sysfs namespace");
MODULE_LICENSE("GPL");
