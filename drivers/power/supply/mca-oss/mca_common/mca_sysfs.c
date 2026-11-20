// SPDX-License-Identifier: GPL-2.0
/*
 * mca_sysfs.c
 *
 * mca sysfs driver
 *
 * Copyright (c) 2023-2023 Xiaomi Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */
#include <mca/common/mca_sysfs.h>
#include <linux/version.h>

#define MCA_DBG_ROOT_DIR "charger_debug"

/* Class nodes live until module exit; callers hold a module dependency. */
static LIST_HEAD(g_mca_sysfs_list_header);
static DEFINE_MUTEX(g_mca_sysfs_class_lock);
static const char * const mca_fixed_devices[] = {
	SYSFS_DEV_1, SYSFS_DEV_2, SYSFS_DEV_3, SYSFS_DEV_4, SYSFS_DEV_5,
};

struct mca_sysfs_owned_device {
	struct list_head node;
	struct device *dev;
	char *name;
	const struct attribute_group *group;
	const struct attribute_group *groups[2];
};

/* Caller holds g_mca_sysfs_class_lock. */
static struct mca_sysfs_class_node *
mca_sysfs_get_class_node(const char *name)
{
	struct mca_sysfs_class_node *node;

	list_for_each_entry(node, &g_mca_sysfs_list_header, node)
		if (!strcmp(node->class_name, name))
			return node;
	return NULL;
}

static struct mca_sysfs_class_node *
mca_sysfs_get_or_create_class(const char *name)
{
	struct mca_sysfs_class_node *node;
	int ret = -ENOMEM;

	if (!name || !*name || strchr(name, '/'))
		return ERR_PTR(-EINVAL);
	mutex_lock(&g_mca_sysfs_class_lock);
	node = mca_sysfs_get_class_node(name);
	if (node)
		goto out;
	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node) {
		node = ERR_PTR(-ENOMEM);
		goto out;
	}
	node->class_name = kstrdup(name, GFP_KERNEL);
	if (!node->class_name)
		goto free_node;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
	node->mca_class = class_create(THIS_MODULE, node->class_name);
#else
	node->mca_class = class_create(node->class_name);
#endif
	if (IS_ERR(node->mca_class)) {
		ret = PTR_ERR(node->mca_class);
		goto free_name;
	}
	INIT_LIST_HEAD(&node->dev_list_header);
	list_add_tail(&node->node, &g_mca_sysfs_list_header);
	goto out;
free_name:
	kfree(node->class_name);
free_node:
	kfree(node);
	node = ERR_PTR(ret);
out:
	mutex_unlock(&g_mca_sysfs_class_lock);
	return node;
}

/* Returned reference must be released by put_device(). */
static struct device *mca_sysfs_find_dev(const char *name)
{
	struct mca_sysfs_class_node *cls;
	struct mca_sysfs_owned_device *node;
	struct device *dev = NULL;

	if (!name)
		return NULL;
	mutex_lock(&g_mca_sysfs_class_lock);
	cls = mca_sysfs_get_class_node("xm_power");
	if (cls) {
		list_for_each_entry(node, &cls->dev_list_header, node) {
			if (node->dev && !strcmp(node->name, name)) {
				dev = get_device(node->dev);
				break;
			}
		}
	}
	mutex_unlock(&g_mca_sysfs_class_lock);
	return dev;
}

static struct device *mca_sysfs_add_device(const char *cls_name,
		const char *name, const struct attribute_group *group)
{
	struct mca_sysfs_class_node *cls;
	struct mca_sysfs_owned_device *node, *pos;
	struct device *dev;
	int ret;

	if (!name || !*name || strchr(name, '/'))
		return ERR_PTR(-EINVAL);
	cls = mca_sysfs_get_or_create_class(cls_name);
	if (IS_ERR(cls))
		return ERR_PTR(PTR_ERR(cls));
	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return ERR_PTR(-ENOMEM);
	node->name = kstrdup(name, GFP_KERNEL);
	if (!node->name) {
		kfree(node);
		return ERR_PTR(-ENOMEM);
	}
	mutex_lock(&g_mca_sysfs_class_lock);
	list_for_each_entry(pos, &cls->dev_list_header, node) {
		if (!strcmp(pos->name, name)) {
			ret = -EEXIST;
			goto fail;
		}
	}
	/* Reserve the name, but do not expose a device until registration succeeds.
	 * Device creation can unwind active sysfs files, whose callbacks may look
	 * up another MCA device. Never hold the registry mutex across that wait.
	 */
	node->group = group;
	list_add_tail(&node->node, &cls->dev_list_header);
	mutex_unlock(&g_mca_sysfs_class_lock);
	if (group) {
		node->groups[0] = group;
		dev = device_create_with_groups(cls->mca_class, NULL, 0, NULL,
						node->groups, "%s", name);
	} else {
		dev = device_create(cls->mca_class, NULL, 0, NULL, "%s", name);
	}
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		mutex_lock(&g_mca_sysfs_class_lock);
		list_del(&node->node);
		goto fail;
	}
	mutex_lock(&g_mca_sysfs_class_lock);
	node->dev = dev;
	mutex_unlock(&g_mca_sysfs_class_lock);
	return dev;
fail:
	mutex_unlock(&g_mca_sysfs_class_lock);
	kfree(node->name);
	kfree(node);
	return ERR_PTR(ret);
}

struct device *mca_sysfs_create_group(const char *cls_name, const char *dev_name,
				      const struct attribute_group *group)
{
	struct device *dev;

	if (!cls_name || !*cls_name || strchr(cls_name, '/') || !group)
		return NULL;
	dev = mca_sysfs_add_device(cls_name, dev_name, group);
	return IS_ERR(dev) ? NULL : dev;
}
EXPORT_SYMBOL(mca_sysfs_create_group);

void mca_sysfs_remove_group(const char *cls_name, struct device *dev,
			    const struct attribute_group *group)
{
	struct mca_sysfs_class_node *cls;
	struct mca_sysfs_owned_device *node, *found = NULL;

	if (!cls_name || IS_ERR_OR_NULL(dev) || !group)
		return;
	mutex_lock(&g_mca_sysfs_class_lock);
	cls = mca_sysfs_get_class_node(cls_name);
	if (cls) {
		list_for_each_entry(node, &cls->dev_list_header, node) {
			if (node->dev == dev && node->group == group) {
				list_del(&node->node);
				found = node;
				break;
			}
		}
	}
	mutex_unlock(&g_mca_sysfs_class_lock);
	if (!found)
		return;
	/* device_unregister() already drops the registration reference. */
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
	struct mca_sysfs_class_node *cls, *next_cls;
	struct mca_sysfs_owned_device *node, *next;

	/* Init failure/module exit only; dependent clients must already stop. */
	list_for_each_entry_safe(cls, next_cls, &g_mca_sysfs_list_header, node) {
		list_for_each_entry_safe(node, next, &cls->dev_list_header, node) {
			list_del(&node->node);
			device_unregister(node->dev);
			kfree(node->name);
			kfree(node);
		}
		list_del(&cls->node);
		class_destroy(cls->mca_class);
		kfree(cls->class_name);
		kfree(cls);
	}
}

static int mca_sysfs_init(void)
{
	struct device *dev;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(mca_fixed_devices); i++) {
		dev = mca_sysfs_add_device("xm_power", mca_fixed_devices[i], NULL);
		if (IS_ERR(dev)) {
			ret = PTR_ERR(dev);
			mca_sysfs_cleanup();
			return ret;
		}
	}
	return 0;
}

static struct dentry *g_mca_dbg_dir;
static DEFINE_MUTEX(g_mca_debugfs_dir_lock);

#ifdef CONFIG_DEBUG_FS
static int mca_debugfs_template_show(struct seq_file *s, void *d)
{
	struct mca_debugfs_attr_data *pattr = s->private;
	char *buf = NULL;
	int ret;

	if (!pattr || !pattr->attr_info->show) {
		pr_err("invalid show\n");
		return -EINVAL;
	}

	buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = pattr->attr_info->show(pattr, buf);
	seq_write(s, buf, ret);
	kfree(buf);

	pr_err("show:%s ret=%d\n", pattr->attr_info->file, ret);
	return 0;
}

static ssize_t mca_debugfs_template_write(struct file *file,
					  const char __user *data, size_t size,
					  loff_t *ppos)
{
	struct mca_debugfs_attr_data *pattr = NULL;
	char *buf = NULL;
	int ret;

	pattr = ((struct seq_file *)file->private_data)->private;
	if (!pattr || !pattr->attr_info->store) {
		pr_err("invalid store\n");
		return -EINVAL;
	}

	buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (size >= PAGE_SIZE) {
		pr_err("input too long\n");
		kfree(buf);
		return -ENOMEM;
	}

	if (copy_from_user(buf, data, size)) {
		pr_err("can not copy data form user to kernel\n");
		kfree(buf);
		return -ENOSPC;
	}
	buf[size] = '\0';

	ret = pattr->attr_info->store(pattr, buf, size);
	kfree(buf);

	pr_err("store: %s ret=%d\n", pattr->attr_info->file, ret);
	return ret;
}

static int mca_debugfs_template_open(struct inode *inode, struct file *file)
{
	return single_open(file, mca_debugfs_template_show, inode->i_private);
}

static const struct file_operations mca_debugfs_template_fops = {
	.open = mca_debugfs_template_open,
	.read = seq_read,
	.write = mca_debugfs_template_write,
	.release = single_release,
};

static struct dentry *mca_debugfs_get_or_create_dir(const char *dir_name)
{
	struct dentry *dir = NULL;

	if (!g_mca_dbg_dir) {
		pr_err("root directory is null");
		return NULL;
	}

	mutex_lock(&g_mca_debugfs_dir_lock);
	dir = debugfs_lookup(dir_name, g_mca_dbg_dir);
	if (IS_ERR_OR_NULL(dir))
		dir = debugfs_create_dir(dir_name, g_mca_dbg_dir);
	if (IS_ERR_OR_NULL(dir)) {
		pr_err("%s dir create fail", dir_name);
		dir = NULL;
	}
	mutex_unlock(&g_mca_debugfs_dir_lock);
	return dir;
}

int mca_debugfs_create_group(const char *dir_name,
			     struct mca_debugfs_attr_info *attr_info,
			     const int attr_size, void *dev_data)
{
	struct dentry *dir = NULL;
	struct dentry *file = NULL;
	struct mca_debugfs_attr_data *attr_data = NULL;

	dir = mca_debugfs_get_or_create_dir(dir_name);
	if (!dir)
		return -1;

	attr_data = kcalloc(attr_size, sizeof(*attr_data), GFP_KERNEL);
	if (!attr_data)
		return -1;

	for (int i = 0; i < attr_size; i++) {
		attr_data[i].attr_info = &attr_info[i];
		attr_data[i].private = dev_data;
		file = debugfs_create_file(attr_info[i].file, attr_info[i].mode,
					   dir, &attr_data[i],
					   &mca_debugfs_template_fops);
		if (file == NULL) {
			pr_err("%s file create fail", attr_info[i].file);
			kfree(attr_data);
			return -1;
		}
	}
	return 0;
}
EXPORT_SYMBOL(mca_debugfs_create_group);

static int mca_debugfs_init(void)
{
	int rc = 0;

	g_mca_dbg_dir = debugfs_create_dir(MCA_DBG_ROOT_DIR, NULL);
	if (IS_ERR(g_mca_dbg_dir)) {
		rc = PTR_ERR(g_mca_dbg_dir);
		pr_err("Failed to create charger debugfs root directory: %d\n",
		       rc);
	}
	return rc;
}
#else
int mca_debugfs_create_group(const char *dir_name,
			     struct mca_debugfs_attr_info *attr_info,
			     const int attr_size, void *dev_data)
{
	return 0;
}
EXPORT_SYMBOL(mca_debugfs_create_group);

static int mca_debugfs_init(void)
{
	return 0;
}
#endif

static int __init mca_fs_init(void)
{
	int ret = mca_sysfs_init();

	if (ret)
		return ret;
	/* debugfs is optional, including kernels booted with debugfs disabled. */
	mca_debugfs_init();
	return 0;
}

static void __exit mca_fs_exit(void)
{
	debugfs_remove_recursive(g_mca_dbg_dir);
	mca_sysfs_cleanup();
}

module_init(mca_fs_init);
module_exit(mca_fs_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("sysfs for xiaomi power driver");
MODULE_AUTHOR("liyuze1@xiaomi.com");
