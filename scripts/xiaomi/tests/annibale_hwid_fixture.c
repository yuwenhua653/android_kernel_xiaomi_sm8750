/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
typedef unsigned int uint;
#define GENMASK(h, l) (((~0U) >> (31 - (h))) & ((~0U) << (l)))
#define FIELD_GET(m, v) (((v) & (m)) >> __builtin_ctz(m))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
struct kernel_param { void *arg; };
struct kernel_param_ops {
 int (*set)(const char *, const struct kernel_param *);
 int (*get)(char *, const struct kernel_param *);
};
static uint mock_soc = 618, next_value;
static int set_error;
static uint32_t socinfo_get_id(void) { return mock_soc; }
static int param_set_uint(const char *s, const struct kernel_param *p)
{
 if (set_error) return set_error;
 *(uint *)p->arg = next_value;
 return 0;
}
static int param_get_uint(char *s, const struct kernel_param *p) { return 0; }
/* PRODUCTION */
int main(void)
{
 struct kernel_param p = { &project }, h = { &hwid_value };
 assert(!xiaomi_hwid_is_ready());
 next_value = 11; set_error = -EINVAL;
 assert(hwid_identity_ops.set("bad", &p) == -EINVAL);
 assert(!project_supplied && !xiaomi_hwid_is_ready());
 set_error = 0; assert(!hwid_identity_ops.set("11", &p));
 assert(!strcmp(product_name_get(), "annibale"));
 assert(!xiaomi_hwid_is_ready());
 set_error = -ERANGE; assert(hwid_identity_ops.set("bad", &h) == -ERANGE);
 assert(!identity_supplied && !xiaomi_hwid_is_ready());
 set_error = 0; next_value = 0; assert(!hwid_identity_ops.set("0", &h));
 assert(xiaomi_hwid_is_ready()); /* A supplied zero is valid; an absent one is not. */
 assert(get_hw_country_version() == 0);
 next_value = 0x12345678; assert(!hwid_identity_ops.set("0x12345678", &h));
 assert(get_hw_id_value() == 0x12345678 && get_hw_country_version() == 0x123);
 assert(get_hw_version_major() == 0x1234 && get_hw_version_minor() == 0x5678);
 assert(get_hw_version_build() == 4 && get_hw_version_platform() == 11);
 build_adc = 456; project_adc = 789;
 assert(get_hw_build_adc() == 456 && get_hw_project_adc() == 789);
 project = 0; assert(!xiaomi_hwid_is_ready());
 project = UINT32_MAX; assert(!strcmp(product_name_get(), "unknown"));
 project = 12; assert(!strcmp(product_name_get(), "unknown"));
 project = 11; mock_soc = 999; assert(!xiaomi_hwid_is_ready());
 mock_soc = 618; assert(xiaomi_hwid_is_ready());
 puts("PASS: HWID missing/invalid/explicit-zero identity, K90 mapping and stock bit vectors");
 return 0;
}
