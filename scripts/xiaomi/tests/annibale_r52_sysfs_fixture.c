/* SPDX-License-Identifier: GPL-2.0-only */
struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD(x) struct list_head x = { &x, &x }
#define INIT_LIST_HEAD(x) ((x)->next = (x)->prev = (x))
static void list_add_tail(struct list_head *n, struct list_head *h) {
	n->prev = h->prev; n->next = h; h->prev->next = n; h->prev = n;
}
static void list_del(struct list_head *n) { n->prev->next = n->next; n->next->prev = n->prev; }
struct class { const char *name; };
struct attribute { const char *name; };
struct device_attribute { struct attribute attr; };
struct mca_sysfs_attr_info { struct device_attribute attr; int sysfs_attr_name; };
struct attribute_group { int unused; };
struct device { int kobj, refs, registered; };
struct mca_sysfs_class_node {
	const char *class_name;
	struct class *mca_class;
	struct list_head dev_list_header, node;
};
#define NODE_ENTRY(p,l) ((void *)((char *)(l) - _Generic((p), \
	struct mca_sysfs_class_node *: offsetof(struct mca_sysfs_class_node,node), \
	struct mca_sysfs_owned_device *: offsetof(struct mca_sysfs_owned_device,node))))
#define list_for_each_entry(p,h,m) \
	for ((p) = NODE_ENTRY(p,(h)->next); &(p)->m != (h); (p) = NODE_ENTRY(p,(p)->m.next))
#define list_for_each_entry_safe(p,n,h,m) \
	for ((p) = NODE_ENTRY(p,(h)->next), (n) = NODE_ENTRY(n,(p)->m.next); \
	     &(p)->m != (h); (p) = (n), (n) = NODE_ENTRY(n,(n)->m.next))
#define SYSFS_DEV_1 "charger"
#define SYSFS_DEV_2 "fuelgauge"
#define SYSFS_DEV_3 "typec"
#define SYSFS_DEV_4 "battery"
#define SYSFS_DEV_5 "hw_monitor"
static int alloc_calls, fail_alloc, live_alloc, live_classes, live_devices;
static int class_error, device_error_at, device_calls, unregisters, removes;
static void (*during_create)(void);
static void *kzalloc(size_t size, int flags) {
	void *p; (void)flags; alloc_calls++;
	if (alloc_calls == fail_alloc) return NULL;
	p = calloc(1, size); assert(p); live_alloc++; return p;
}
static char *kstrdup(const char *s, int flags) {
	char *p = kzalloc(strlen(s)+1, flags); if (p) strcpy(p,s); return p;
}
static void kfree(const void *p) { if (p) { assert(live_alloc > 0); live_alloc--; free((void *)p); } }
static struct class *class_create(const char *name) {
	struct class *c; (void)name;
	if (class_error) return ERR_PTR(class_error);
	c = calloc(1,sizeof(*c)); assert(c); c->name = name; live_classes++; return c;
}
static void class_destroy(struct class *c) { assert(c && !IS_ERR(c)); assert(strcmp(c->name,"xxxxx")); assert(live_classes > 0); live_classes--; free(c); }
static struct device *create_device(void) {
	struct device *d; assert(!mutex_depth); device_calls++;
	if (during_create) during_create();
	if (device_error_at == device_calls) return ERR_PTR(-EIO);
	d = calloc(1,sizeof(*d)); assert(d); d->refs = 1; d->registered = 1; live_devices++; return d;
}
static struct device *device_create(struct class *c, void *parent, int number, void *data, const char *format, ...) {
	(void)c; (void)parent; (void)number; (void)data; (void)format; return create_device();
}
static struct device *device_create_with_groups(struct class *c, void *parent, int number, void *data,
		const struct attribute_group **groups, const char *format, ...) {
	(void)c; (void)parent; (void)number; (void)data; (void)format;
	assert(groups[0] && !groups[1]); return create_device();
}
static struct device *get_device(struct device *d) { assert(d->refs > 0); d->refs++; return d; }
static void put_device(struct device *d) {
	assert(d && d->refs > 0);
	if (!--d->refs) { assert(!d->registered); live_devices--; free(d); }
}
static void device_unregister(struct device *d) {
	assert(!mutex_depth); assert(d && d->registered); unregisters++;
	d->registered = 0; put_device(d);
}
static int sysfs_create_group(int *k, const struct attribute_group *g) { (void)k; (void)g; return 0; }
static int sysfs_create_link(int *k, int *t, const char *n) { (void)k; (void)t; (void)n; return 0; }
static void sysfs_remove_group(int *k, const struct attribute_group *g) { (void)k; (void)g; removes++; }
static void sysfs_remove_link(int *k, const char *n) { (void)k; (void)n; }
static int sysfs_create_file(int *k, const struct attribute *a) { (void)k; (void)a; return 0; }
static void sysfs_remove_file(int *k, const struct attribute *a) { (void)k; (void)a; }
/* PRODUCTION */
static void check_pending_reservation(void) {
	struct device *held;
	assert(!mca_sysfs_find_dev("pending"));
	assert(PTR_ERR(mca_sysfs_add_device("xm_power", "pending", NULL)) == -EEXIST);
	held = mca_sysfs_find_dev("charger"); assert(held); put_device(held);
}
int main(void) {
	int n, ret, before, allocations;
	struct device *d, *held;
	struct attribute_group group = {0}, wrong = {0};
	assert(!mca_sysfs_create_group(NULL, "a", &group));
	assert(!mca_sysfs_create_group("a/b", "a", &group));
	assert(!mca_sysfs_create_group("xm_power", "a/b", &group));
	class_error = -EIO;
	assert(mca_sysfs_init() == -EIO);
	assert(!live_alloc && !live_classes && !live_devices);
	class_error = 0;
	alloc_calls = 0;
	assert(!mca_sysfs_init()); allocations = alloc_calls;
	assert(live_classes == 1 && live_devices == 5);
	mca_sysfs_cleanup(); assert(!live_alloc && !live_classes && !live_devices);
	for (n = 1; n <= allocations; n++) {
		alloc_calls = 0; fail_alloc = n; ret = mca_sysfs_init();
		assert(ret == -ENOMEM); assert(!live_alloc && !live_classes && !live_devices);
	}
	fail_alloc = 0;
	for (n = 1; n <= 5; n++) {
		device_calls = 0; device_error_at = n;
		assert(mca_sysfs_init() == -EIO);
		assert(!live_alloc && !live_classes && !live_devices);
	}
	device_error_at = 0; assert(!mca_sysfs_init());
	during_create = check_pending_reservation;
	d = mca_sysfs_create_group("xm_power", "pending", &group); assert(d);
	during_create = NULL;
	mca_sysfs_remove_group("xm_power", d, &group);
	before = live_alloc;
	d = mca_sysfs_create_group("xm_power", "test", &group); assert(d);
	assert(!mca_sysfs_create_group("xm_power", "test", &wrong));
	held = mca_sysfs_find_dev("test"); assert(held == d && d->refs == 2);
	n = unregisters;
	mca_sysfs_remove_group("missing", d, &group);
	mca_sysfs_remove_group("xm_power", d, &wrong); assert(unregisters == n);
	mca_sysfs_remove_group("xm_power", d, &group);
	assert(unregisters == n+1 && held->refs == 1 && !held->registered);
	assert(!mca_sysfs_find_dev("test"));
	mca_sysfs_remove_group("xm_power", d, &group); assert(unregisters == n+1);
	assert(live_alloc == before); put_device(held);
	/* Class and device names must outlive caller-owned strings. */
	{
		char cls[] = "other", name[] = "named";
		d = mca_sysfs_create_group(cls, name, &group); assert(d);
		strcpy(cls,"xxxxx"); strcpy(name,"xxxxx");
		mca_sysfs_remove_group("other", d, &group);
		assert(live_classes == 2 && live_devices == 5);
	}
	mca_sysfs_cleanup(); assert(!live_alloc && !live_classes && !live_devices && !mutex_depth);
	puts("PASS: every init allocation/device failure; ERR_PTR; duplicate and wrong-owner removal; retained device refs; owned names; complete sysfs cleanup");
	return 0;
}
