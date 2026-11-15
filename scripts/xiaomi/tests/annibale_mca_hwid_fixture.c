/* SPDX-License-Identifier: GPL-2.0-only */
#define __init
static bool ready;
static const char *product = "annibale";
static bool xiaomi_hwid_is_ready(void) { return ready; }
static const char *product_name_get(void) { return product; }
static u32 get_hw_version_platform(void) { return 15; }
static u32 get_hw_country_version(void) { return 2; }
static u32 get_hw_version_major(void) { return 3; }
static u32 get_hw_version_minor(void) { return 4; }
static u32 get_hw_version_build(void) { return 5; }
static u32 get_hw_project_adc(void) { return 123; }
static u32 get_hw_build_adc(void) { return 456; }
static u32 get_hw_id_value(void) { return 0xabcd; }
/* PRODUCTION */
int main(void)
{
	assert(mca_hwid_init() == -ENODEV);
	ready = true;
	product = "dada";
	assert(mca_hwid_init() == -ENODEV);
	product = "annibale";
	assert(!mca_hwid_init());
	struct mca_hwid_info *id = mca_get_hwid_info();
	assert(id == mca_get_hwid_info());
	assert(id->platform_version == 15 && id->country_version == 2);
	assert(id->major_version == 3 && id->minor_version == 4 && id->build_version == 5);
	assert(id->product_adc == 123 && id->build_adc == 456);
	assert(id->hwid_value == 0xabcd && !strcmp(id->product_name, "annibale"));
	puts("PASS: MCA HWID rejects missing/wrong board, preserves identity and ADC mapping");
	return 0;
}
