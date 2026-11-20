// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 The LineageOS Project
 *
 * Board identification, as the charging drivers want it.  See
 * include/mca/common/mca_hwid.h.
 */

#include <linux/module.h>
#include <linux/string.h>

#include <mca/common/mca_hwid.h>

#include <linux/hwid.h>

/* Immutable after module init; dependent modules own the provider reference. */
static struct mca_hwid info;

const struct mca_hwid *mca_get_hwid_info(void)
{
	return &info;
}
EXPORT_SYMBOL(mca_get_hwid_info);

static int __init mca_hwid_init(void)
{
	if (!xiaomi_hwid_is_ready() || strcmp(product_name_get(), "annibale"))
		return -ENODEV;
	info.platform_version = get_hw_version_platform();
	info.country_version = get_hw_country_version();
	info.major_version = get_hw_version_major();
	info.minor_version = get_hw_version_minor();
	info.build_version = get_hw_version_build();
	info.product_adc = get_hw_project_adc();
	info.build_adc = get_hw_build_adc();
	info.hwid_value = get_hw_id_value();
	info.product_name = product_name_get();
	return 0;
}
module_init(mca_hwid_init);

MODULE_DESCRIPTION("Annibale MCA hardware identity");
MODULE_LICENSE("GPL");
