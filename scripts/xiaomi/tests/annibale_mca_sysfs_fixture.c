/* SPDX-License-Identifier: GPL-2.0-only */
struct attribute { const char *name; };
struct device_attribute { struct attribute attr; };
struct mca_sysfs_attr_info { struct device_attribute attr; int sysfs_attr_name; };
struct attribute_group { int dummy; };
struct device { int kobj; };
static struct device parent, target;
static struct attribute_group group;
static int refs, group_error, link_error, file_error_at = -1, create_calls;
static int groups_created, groups_removed, links_removed, files_removed[4];
static bool exists = true;
static struct device *mca_sysfs_find_dev(const char *name)
{ if (!exists) return NULL; refs++; return &parent; }
static void put_device(struct device *dev) { refs--; }
static int sysfs_create_group(int *kobj, const struct attribute_group *g)
{ groups_created++; return group_error; }
static int sysfs_create_link(int *src, int *dst, const char *name) { return link_error; }
static void sysfs_remove_group(int *kobj, const struct attribute_group *g) { groups_removed++; }
static void sysfs_remove_link(int *kobj, const char *name) { links_removed++; }
static int sysfs_create_file(int *kobj, struct attribute *a)
{ int i = create_calls++; return i == file_error_at ? -EEXIST : 0; }
static void sysfs_remove_file(int *kobj, struct attribute *a)
{ files_removed[a->name[0] - '0']++; }
/* PRODUCTION */
int main(void)
{
	struct mca_sysfs_attr_info attrs[3] = { {{{"0"}}, 10}, {{{"1"}}, 11}, {{{"2"}}, 12} };
	struct attribute *array[4];
	assert(mca_sysfs_create_link_group(NULL, "x", &target, &group) == -EINVAL);
	assert(mca_sysfs_create_link_group("charger", "a/b", &target, &group) == -EINVAL);
	group_error = -ENOMEM;
	assert(mca_sysfs_create_link_group("charger", "x", &target, &group) == -ENOMEM);
	assert(!refs && !groups_removed);
	group_error = 0; link_error = -EEXIST;
	assert(mca_sysfs_create_link_group("charger", "x", &target, &group) == -EEXIST);
	assert(!refs && groups_removed == 1);
	link_error = 0;
	assert(!mca_sysfs_create_link_group("charger", "x", &target, &group));
	assert(!refs);
	mca_sysfs_remove_link_group("charger", "x", &target, &group);
	assert(!refs && links_removed == 1 && groups_removed == 2);
	exists = false;
	mca_sysfs_remove_link_group("charger", "x", &target, &group);
	assert(groups_removed == 3 && links_removed == 1);
	assert(mca_sysfs_create_files("charger", attrs, 3) == -ENODEV);
	exists = true;
	assert(mca_sysfs_create_files("charger", attrs, 0) == -EINVAL);
	file_error_at = 1;
	assert(mca_sysfs_create_files("charger", attrs, 3) == -EEXIST);
	assert(files_removed[0] == 1 && files_removed[1] == 0 && files_removed[2] == 0);
	assert(!refs);
	create_calls = 0; file_error_at = -1;
	assert(!mca_sysfs_create_files("charger", attrs, 3));
	mca_sysfs_remove_files("charger", attrs, 3);
	assert(!refs && files_removed[0] == 2 && files_removed[1] == 1 && files_removed[2] == 1);
	mca_sysfs_init_attrs(array, attrs, 3);
	assert(array[0] == &attrs[0].attr.attr && !array[3]);
	assert(mca_sysfs_lookup_attr("1", attrs, 3) == &attrs[1]);
	assert(!mca_sysfs_lookup_attr("1", attrs, -1));
	puts("PASS: sysfs link rollback, file ownership on EEXIST, removal with missing parent and balanced references");
	return 0;
}
