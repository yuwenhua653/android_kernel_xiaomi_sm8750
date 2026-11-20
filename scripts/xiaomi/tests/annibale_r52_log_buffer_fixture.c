/* SPDX-License-Identifier: GPL-2.0-only */
#define MCA_LOG_SYSFS_ATTRS_SIZE 1
struct device { int unused; };
struct attribute { const char *name; };
struct device_attribute { struct attribute attr; };
struct mca_sysfs_attr_info { int sysfs_attr_name; };
static struct mca_sysfs_attr_info mca_log_sysfs_field_tbl[1];
static struct mca_log_buf_info g_mca_log_info;
static int spin_depth, frees, snapshots, concurrent_index = -1;
#define spin_lock_irqsave(lock,flags) ((flags) = 0, spin_depth++)
#define spin_unlock_irqrestore(lock,flags) ((void)(flags), spin_depth--)
static int snapshot_index(int *index) {
	int value = *index; snapshots++;
	if (concurrent_index >= 0) *index = concurrent_index;
	return value;
}
#define READ_ONCE(x) snapshot_index(&(x))
#define WRITE_ONCE(x,v) ((x) = (v))
static void kfree(void *p) { assert(p); frees++; free(p); }
static struct mca_sysfs_attr_info *mca_sysfs_lookup_attr(const char *n, struct mca_sysfs_attr_info *t, int size) {
	(void)n; assert(size == 1); return t;
}
static int kstrtoint(const char *text, int base, int *value) {
	char *end; long v; errno = 0; v = strtol(text,&end,base);
	if (errno || end == text || (*end && strcmp(end,"\n")) || v < INT_MIN || v > INT_MAX) return -EINVAL;
	*value = (int)v; return 0;
}
static void __mca_log_info(const char *format, ...) { (void)format; }
static int mca_log_charge_log_dump_log_head(char *buf) { (void)buf; return 0; }
static int mca_log_charge_log_dump_log_context(char *buf) { (void)buf; return 0; }
/* PRODUCTION */
int main(void) {
	struct device dev = {0}; struct device_attribute attr = {{"test"}};
	struct { char buf[PAGE_SIZE]; unsigned char guard[16]; } out;
	char live[PAGE_SIZE]; int i;
	memset(&out,0xA5,sizeof(out)); memset(live,'L',sizeof(live));
	g_mca_log_info.log_buff = live;
	mca_log_sysfs_field_tbl[0].sysfs_attr_name = MCA_LOG_ATTR_LOG_INDEX;
	g_mca_log_info.index = 3;
	assert(mca_log_sysfs_store(&dev,&attr,"bad",3) == -EINVAL && g_mca_log_info.index == 3);
	assert(mca_log_sysfs_store(&dev,&attr,"21",2) < 0 && g_mca_log_info.index == 3);
	assert(mca_log_sysfs_store(&dev,&attr,"20",2) == 2 && g_mca_log_info.index == 20);
	mca_log_sysfs_field_tbl[0].sysfs_attr_name = MCA_LOG_ATTR_DUMP_BUFFER_LOG;
	assert(mca_log_sysfs_show(&dev,&attr,out.buf) == PAGE_SIZE-1);
	assert(!out.buf[PAGE_SIZE-1] && !spin_depth);
	for(i=0;i<PAGE_SIZE;i++) assert(!live[i]);
	g_mca_log_info.index = -1; assert(mca_log_sysfs_show(&dev,&attr,out.buf) == 0);
	g_mca_log_info.index = 21; assert(mca_log_sysfs_show(&dev,&attr,out.buf) == 0);
	g_mca_log_info.log_buff_cache[0] = malloc(PAGE_SIZE); assert(g_mca_log_info.log_buff_cache[0]);
	memset(g_mca_log_info.log_buff_cache[0],'C',PAGE_SIZE);
	g_mca_log_info.cache_num = 1; g_mca_log_info.index = 0;
	concurrent_index = 20; snapshots = 0;
	assert(mca_log_sysfs_show(&dev,&attr,out.buf) == PAGE_SIZE-1);
	assert(snapshots == 1 && frees == 1 && !g_mca_log_info.log_buff_cache[0]);
	assert(!g_mca_log_info.cache_num && g_mca_log_info.index == 20 && !spin_depth);
	assert(out.buf[0] == 'C' && !out.buf[PAGE_SIZE-1]);
	for(i=0;i<16;i++) assert(out.guard[i] == 0xA5);
	puts("PASS: malformed/out-of-range log index, non-NUL full buffers, one index snapshot across writer change, bounded reads and cache free");
	return 0;
}
