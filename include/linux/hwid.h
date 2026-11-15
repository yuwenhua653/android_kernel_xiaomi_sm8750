/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 The LineageOS Project
 */

#ifndef __LINUX_HWID_H
#define __LINUX_HWID_H

#include <linux/types.h>

/*
 * Project numbers the bootloader passes in hwid.project. On SM8750 they
 * count haotian, dada, miro, xuanyuan, bixi, nirvana, onyx, luming, piano,
 * yupei and annibale from one.
 */
#define HARDWARE_PROJECT_UNKNOWN	0
#define HARDWARE_PROJECT_O2		1
#define HARDWARE_PROJECT_O3		2
#define HARDWARE_PROJECT_P11		11

/* Sales region, as returned by get_hw_country_version(). */
enum hw_country {
	CountryCN = 0,
	CountryGlobal = 1,
	CountryIndia = 2,
	CountryJapan = 3,
};

uint32_t get_hw_version_platform(void);
uint32_t get_hw_id_value(void);
uint32_t get_hw_country_version(void);
uint32_t get_hw_version_major(void);
uint32_t get_hw_version_minor(void);
uint32_t get_hw_version_build(void);
uint32_t get_hw_project_adc(void);
uint32_t get_hw_build_adc(void);
const char *product_name_get(void);

/* True only after real project and identity parameters were supplied. */
bool xiaomi_hwid_is_ready(void);

#endif /* __LINUX_HWID_H */
