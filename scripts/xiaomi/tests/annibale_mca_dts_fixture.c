/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdlib.h>
struct device_node { int present; };
static int calls, count = 4, fail_read;
static const char *values[] = {"10", "0x20", "-3", "bad"};
static int of_property_read_variable_u8_array(struct device_node *np, const char *prop,
					     u8 *out, size_t min, size_t max)
{
	calls++;
	if (fail_read) return -EINVAL;
	memset(out, 9, min);
	return min;
}
static int of_property_read_variable_u32_array(struct device_node *np, const char *prop,
					      u32 *out, size_t min, size_t max)
{
	calls++;
	if (fail_read) return -EINVAL;
	for (size_t i = 0; i < min; i++) out[i] = 42;
	return min;
}
static int of_property_count_elems_of_size(struct device_node *np, const char *prop, int size)
{ calls++; return count; }
static int of_property_read_u32_index(struct device_node *np, const char *prop, int i, u32 *val)
{ calls++; *val = 42; return 0; }
static int of_property_read_string(struct device_node *np, const char *prop, const char **str)
{ calls++; *str = values[0]; return 0; }
static int of_property_read_string_helper(struct device_node *np, const char *prop,
					 const char **out, int n, int skip)
{
	calls++;
	if (!out) return count;
	assert(skip >= 0 && skip < 4);
	*out = values[skip];
	return 1;
}
static int kstrtoint(const char *s, int base, int *val)
{
	char *end;
	long v = strtol(s, &end, base);
	if (*end || end == s) return -EINVAL;
	*val = v;
	return 0;
}
/* PRODUCTION */
int main(void)
{
	struct device_node np = { 1 };
	u8 bytes[2] = {1, 0xa5};
	u32 words[2] = {1, 0xdeadbeef};
	int nums[4] = {0};
	const char *str;
	assert(mca_parse_dts_count_valid(12, 4, 3));
	assert(mca_parse_dts_count_valid(12, UINT32_MAX, 3));
	assert(!mca_parse_dts_count_valid(12, 3, 3));
	assert(!mca_parse_dts_count_valid(11, 4, 3));
	assert(!mca_parse_dts_count_valid(0, 4, 3));
	assert(!mca_parse_dts_count_valid(-1, 4, 3));
	assert(!mca_parse_dts_count_valid(12, 4, 0));
	assert(!mca_parse_dts_count_valid(12, 0, 3));
	assert(mca_parse_dts_u32_index(&np, "x", -1, words) == -EINVAL);
	assert(mca_parse_dts_string_index(&np, "x", -1, &str) == -EINVAL);
	assert(mca_parse_dts_string_array(&np, "x", NULL, 4, 1) == -EINVAL);
	assert(mca_parse_dts_u8_array(&np, "x", bytes, 0) == -EINVAL);
	assert(mca_parse_dts_u32_array(&np, "x", words, 0) == -EINVAL);
	assert(mca_parse_dts_u32(NULL, "x", words, 7) == -EINVAL);
	assert(!calls);
	assert(!mca_parse_dts_u8(&np, "x", bytes, 7) && bytes[0] == 9 && bytes[1] == 0xa5);
	assert(!mca_parse_dts_u32(&np, "x", words, 7) && words[0] == 42 && words[1] == 0xdeadbeef);
	fail_read = 1;
	assert(mca_parse_dts_u8(&np, "x", bytes, 7) == -EINVAL && bytes[0] == 7);
	assert(mca_parse_dts_u32(&np, "x", words, 7) == -EINVAL && words[0] == 7);
	count = 3;
	assert(mca_parse_dts_string_array(&np, "x", nums, 1, 3) == 3);
	assert(nums[0] == 10 && nums[1] == 32 && nums[2] == -3);
	count = 4;
	assert(mca_parse_dts_string_array(&np, "x", nums, 4, 1) == -EINVAL);
	assert(mca_parse_dts_u32_count(&np, "x", 1, 3) == -EINVAL);
	count = -EINVAL;
	assert(mca_parse_dts_u8_count(&np, "x", 4, 1) == -EINVAL);
	puts("PASS: MCA DT counts, overflow-safe rows, null/negative/zero arguments, defaults and invalid strings");
	return 0;
}
