/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 The LineageOS Project
 *
 * Where the charging stack shows itself to userspace.
 *
 * The stack is many modules that each own a few attributes, and userspace
 * wants them gathered by what they describe rather than by which driver
 * happens to provide them.  So a class is created on first use and devices
 * are added to it by name -- "charger", "battery", "typec" -- and any module
 * can add attributes to a device another module created.
 */

#ifndef __MCA_SYSFS_H
#define __MCA_SYSFS_H

#include <linux/device.h>
#include <linux/sysfs.h>

/* The class the whole stack lives under. */
#define MCA_SYSFS_CLASS			"xm_power"

/* The devices attributes are gathered under. */
#define MCA_SYSFS_DEV_CHARGER		"charger"
#define MCA_SYSFS_DEV_FUELGAUGE		"fuelgauge"
#define MCA_SYSFS_DEV_TYPEC		"typec"
#define MCA_SYSFS_DEV_BATTERY		"battery"
#define MCA_SYSFS_DEV_HW_MONITOR	"hw_monitor"

/*
 * The same five, numbered.  Xiaomi's own sources refer to them this way and
 * the numbering is theirs; both spellings name the same device.
 */
#define SYSFS_DEV_1			MCA_SYSFS_DEV_CHARGER
#define SYSFS_DEV_2			MCA_SYSFS_DEV_FUELGAUGE
#define SYSFS_DEV_3			MCA_SYSFS_DEV_TYPEC
#define SYSFS_DEV_4			MCA_SYSFS_DEV_BATTERY
#define SYSFS_DEV_5			MCA_SYSFS_DEV_HW_MONITOR

/**
 * struct mca_sysfs_attr_info - one attribute in a driver's table
 * @attr:		the attribute as sysfs sees it
 * @sysfs_attr_name:	the caller's own identifier for it, which its show and
 *			store handlers switch on
 */
struct mca_sysfs_attr_info {
	struct device_attribute	attr;
	int			sysfs_attr_name;
};

/* A read-only attribute served by <prefix>_show(). */
#define mca_sysfs_attr_ro(prefix, mode, id, name)			\
	{								\
		.attr = __ATTR(name, mode, prefix##_show, NULL),		\
		.sysfs_attr_name = (id),				\
	}

/* A write-only attribute served by <prefix>_store(). */
#define mca_sysfs_attr_wo(prefix, mode, id, name)			\
	{								\
		.attr = __ATTR(name, mode, NULL, prefix##_store),	\
		.sysfs_attr_name = (id),				\
	}

/* A read/write attribute served by <prefix>_show() and <prefix>_store(). */
#define mca_sysfs_attr_rw(prefix, mode, id, name)			\
	{								\
		.attr = __ATTR(name, mode, prefix##_show,		\
			       prefix##_store),				\
		.sysfs_attr_name = (id),				\
	}

struct device *mca_sysfs_create_group(const char *cls_name,
				      const char *dev_name,
				      const struct attribute_group *group);
void mca_sysfs_remove_group(const char *cls_name, struct device *dev,
			    const struct attribute_group *group);

int mca_sysfs_create_link_group(const char *dev_name, const char *link_name,
				struct device *target_dev,
				const struct attribute_group *group);
void mca_sysfs_remove_link_group(const char *dev_name, const char *link_name,
				 struct device *target_dev,
				 const struct attribute_group *group);

int mca_sysfs_create_files(const char *dev_name,
			   struct mca_sysfs_attr_info *attr_info, int size);

void mca_sysfs_init_attrs(struct attribute **attrs,
			  struct mca_sysfs_attr_info *attr_info, int size);
struct mca_sysfs_attr_info *mca_sysfs_lookup_attr(const char *name,
						  struct mca_sysfs_attr_info *attr_info,
						  int size);

/* Paired teardown; call before freeing attributes or unloading their owner. */
void mca_sysfs_remove_files(const char *dev_name,
			    struct mca_sysfs_attr_info *attr_info, int size);
/* Only xm_power is supported. Group management is for probe/remove context,
 * never from one of the attributes being created/removed. No debugfs API yet.
 */
#endif /* __MCA_SYSFS_H */
