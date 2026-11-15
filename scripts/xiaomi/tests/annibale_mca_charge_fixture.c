/* SPDX-License-Identifier: GPL-2.0-only */
#define PAGE_SIZE 4096
#define MCA_EVENT_TYPE_BATTERY_INFO 2
#define MCA_EVENT_BATTERY_STS_CHANGE 17
static int notifications;
static void mca_event_block_notify(int type, unsigned long event, void *data)
{ assert(type == 2 && event == 17); notifications++; }
static int strscpy(char *d, const char *s, size_t n)
{ if (strlen(s) >= n) return -E2BIG; strcpy(d, s); return strlen(s); }
static int kstrtoint(const char *s, int base, int *value)
{
	char *end; errno = 0; long v = strtol(s, &end, base);
	if (errno || v < INT_MIN || v > INT_MAX) return -ERANGE;
	if (*end || end == s) return -EINVAL;
	*value = v; return 0;
}
static int kstrtou32(const char *s, int base, u32 *value)
{
	char *end; errno = 0; unsigned long v = strtoul(s, &end, base);
	if (*s == '-' || errno || v > UINT32_MAX) return -ERANGE;
	if (*end || end == s) return -EINVAL;
	*value = v; return 0;
}
static int sysfs_emit(char *buf, const char *fmt, ...)
{
	va_list ap; va_start(ap, fmt); int ret = vsnprintf(buf, PAGE_SIZE, fmt, ap); va_end(ap); return ret;
}
static int sysfs_emit_at(char *buf, int at, const char *fmt, ...)
{
	va_list ap; va_start(ap, fmt); int ret = vsnprintf(buf + at, PAGE_SIZE - at, fmt, ap); va_end(ap); return ret;
}
/* PRODUCTION */
static int calls, provider_error, get_error;
static const char *get_text = "eff_client:test 1";
static int set_suspend(const char *user, char *value, void *data)
{ assert(!strcmp(user, "tester")); calls++; return *(int *)data; }
static int get_suspend(char *out, void *data)
{ if (get_error) return get_error; strcpy(out, get_text); return 0; }
static int parse(enum mca_charge_field field, const char *text, struct mca_charge_request *req)
{ return mca_charge_parse(field, text, strlen(text), req); }
int main(void)
{
	struct mca_charge_request req = { 0 };
	int ok = 0, err = -EIO;
	char buf[PAGE_SIZE], oversized[100];
	struct mca_charge_if_ops buck = { .type_name = "buck", .data = &ok,
		.set_input_suspend = set_suspend, .get_input_suspend = get_suspend };
	struct mca_charge_if_ops quick = { .type_name = "quick", .data = &err,
		.set_input_suspend = set_suspend, .get_input_suspend = get_suspend };
	memset(oversized, 'a', 63);
	strcpy(oversized + 63, "buck 1");
	assert(parse(MCA_CHARGE_ENABLE, oversized, &req) == -E2BIG);
	assert(parse(MCA_CHARGE_ENABLE, "tester buck nonsense", &req) == -EINVAL);
	assert(parse(MCA_CHARGE_ENABLE, "tester buck 1 extra", &req) == -EINVAL);
	assert(parse(MCA_CHARGE_ENABLE, "tester buck 2", &req) == -ERANGE);
	assert(parse(MCA_POWER_LIMIT, "tester buck 4294967296", &req) == -ERANGE);
	assert(parse(MCA_POWER_LIMIT, "tester no_such_path 1", &req) == -EINVAL);
	assert(parse(MCA_IIN_LIMIT, "tester buck junk", &req) == -EINVAL);
	assert(parse(MCA_SUSPEND_STATUS, "tester buck 1", &req) == -EACCES);
	assert(mca_charge_parse(MCA_CHARGE_ENABLE, "x\0y", 3, &req) == -EINVAL);
	assert(!parse(MCA_IIN_LIMIT, "tester buck -1", &req));
	assert(!parse(MCA_SHIPMODE, "1\n", &req) && req.type == MCA_CHARGE_IF_CHG_TYPE_BUCK);
	assert(!parse(MCA_INPUT_SUSPEND, "tester buck 1\n", &req));
	assert(mca_charge_apply(MCA_INPUT_SUSPEND, &req) == -ENODEV && !calls);
	assert(mca_charge_read(MCA_SUSPEND_STATUS, buf) == -ENODATA);
	assert(mca_charge_read(MCA_SHIPMODE, buf) == -ENODEV);
	assert(!mca_charge_if_ops_register(&buck));
	assert(mca_charge_if_ops_register(&buck) == -EBUSY);
	assert(mca_charge_read(MCA_SHIPMODE, buf) == -EOPNOTSUPP);
	assert(!mca_charge_apply(MCA_INPUT_SUSPEND, &req) && calls == 1 && notifications == 1);
	assert(mca_charge_read(MCA_SUSPEND_STATUS, buf) == 2 && !strcmp(buf, "1\n"));
	get_error = -EIO;
	assert(mca_charge_read(MCA_SUSPEND_STATUS, buf) == -EIO);
	get_error = 0; get_text = "eff_client:test invalid";
	assert(mca_charge_read(MCA_SUSPEND_STATUS, buf) == -EPROTO);
	get_text = "eff_client:test 4294967297";
	assert(mca_charge_read(MCA_SUSPEND_STATUS, buf) == -EPROTO);
	get_text = "eff_client:test 0";
	assert(mca_charge_read(MCA_INPUT_SUSPEND, buf) > 0 && !strcmp(buf, "buck eff_client:test 0\n"));
	assert(!mca_charge_if_ops_register(&quick));
	assert(!parse(MCA_INPUT_SUSPEND, "tester all 1", &req));
	assert(mca_charge_apply(MCA_INPUT_SUSPEND, &req) == -EIO && calls == 3 && notifications == 2);
	assert(mca_charge_apply(MCA_POWER_LIMIT, &req) == -EOPNOTSUPP && calls == 3);
	assert(!mca_charge_if_ops_unregister(&buck));
	assert(mca_charge_if_ops_unregister(&buck) == -ENOENT);
	err = 8;
	assert(mca_charge_apply(MCA_INPUT_SUSPEND, &req) == -EIO && notifications == 2);
	assert(!mca_charge_if_ops_unregister(&quick));
	assert(mca_charge_apply(MCA_INPUT_SUSPEND, &req) == -ENODEV);
	puts("PASS: charge parse rejects malformed/overflow input; missing providers, preflight, errors, partial-all refresh and unregister");
	return 0;
}
