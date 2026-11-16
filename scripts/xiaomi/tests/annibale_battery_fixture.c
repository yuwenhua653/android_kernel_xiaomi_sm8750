/* SPDX-License-Identifier: GPL-2.0-only */
/* Host models only: this does not emulate the Linux workqueue or device core. */
#define EPROBE_DEFER 517
#define GFP_KERNEL 0
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
struct mutex { pthread_mutex_t m; };
static void mutex_init(struct mutex *m) { assert(!pthread_mutex_init(&m->m, NULL)); }
static void mutex_lock(struct mutex *m) { assert(!pthread_mutex_lock(&m->m)); }
static void mutex_unlock(struct mutex *m) { assert(!pthread_mutex_unlock(&m->m)); }
struct device { void *data, *of_node; };
struct platform_device { struct device dev; };
static void platform_set_drvdata(struct platform_device *p, void *v) { p->dev.data = v; }
static void *platform_get_drvdata(struct platform_device *p) { return p->dev.data; }
static void *dev_get_drvdata(struct device *d) { return d->data; }
static bool hwid_ready = true, alloc_fail;
static const char *product = "annibale";
static void *allocation;
static bool xiaomi_hwid_is_ready(void) { return hwid_ready; }
static const char *product_name_get(void) { return product; }
static void *devm_kzalloc(struct device *d, size_t n, int flags)
{
    if (alloc_fail) return NULL;
    assert(!allocation);
    return allocation = calloc(1, n);
}
struct work_struct { void (*fn)(struct work_struct *); };
struct delayed_work { struct work_struct work; bool pending, deferrable; };
#define to_delayed_work(w) container_of(w, struct delayed_work, work)
#define INIT_DEFERRABLE_WORK(w, f) do { (w)->work.fn = (f); (w)->deferrable = true; } while (0)
static int queue_count, cancel_count, sequence, cancelled_at, unregistered_at;
static int system_freezable_wq;
static unsigned long last_delay;
static unsigned long msecs_to_jiffies(unsigned long n) { return n; }
static bool queue_delayed_work(int q, struct delayed_work *w, unsigned long n)
{
    assert(w->deferrable);
    queue_count++; last_delay = n; w->pending = true;
    return true;
}
static bool mod_delayed_work(int q, struct delayed_work *w, unsigned long n)
{
    return queue_delayed_work(q, w, n);
}
static bool cancel_delayed_work_sync(struct delayed_work *w)
{
    cancel_count++; cancelled_at = ++sequence; w->pending = false;
    return true;
}
enum power_supply_property {
    POWER_SUPPLY_PROP_CAPACITY, POWER_SUPPLY_PROP_VOLTAGE_NOW,
    POWER_SUPPLY_PROP_CURRENT_NOW, POWER_SUPPLY_PROP_TEMP,
    POWER_SUPPLY_PROP_CHARGE_COUNTER, POWER_SUPPLY_PROP_CHARGE_FULL,
    POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, POWER_SUPPLY_PROP_CYCLE_COUNT,
    POWER_SUPPLY_PROP_STATUS, POWER_SUPPLY_PROP_HEALTH, POWER_SUPPLY_PROP_PRESENT
};
#define POWER_SUPPLY_STATUS_UNKNOWN 0
#define POWER_SUPPLY_HEALTH_UNKNOWN 0
#define POWER_SUPPLY_TYPE_BATTERY 1
union power_supply_propval { int intval; };
struct power_supply { void *data; };
struct power_supply_desc {
    const char *name;
    int type, num_properties;
    const enum power_supply_property *properties;
    int (*get_property)(struct power_supply *, enum power_supply_property,
                        union power_supply_propval *);
    void (*external_power_changed)(struct power_supply *);
};
struct power_supply_config { void *drv_data, *of_node; };
static struct power_supply supply;
static int changed_count, refs, register_error, registered;
static bool existing;
static void *power_supply_get_drvdata(struct power_supply *p) { return p->data; }
static struct power_supply *power_supply_get_by_name(const char *name)
{
    assert(!strcmp(name, "battery"));
    if (!existing) return NULL;
    refs++; return &supply;
}
static void power_supply_put(struct power_supply *p) { assert(refs > 0); refs--; }
static struct power_supply *power_supply_register(struct device *d,
        const struct power_supply_desc *desc, struct power_supply_config *cfg)
{
    assert(!strcmp(desc->name, "battery") && desc->type == POWER_SUPPLY_TYPE_BATTERY);
    assert(desc->num_properties == 10 && cfg->of_node == d->of_node);
    if (register_error) return (void *)(intptr_t)register_error;
    registered++; supply.data = cfg->drv_data; return &supply;
}
static void power_supply_changed(struct power_supply *p) { assert(registered == 1); changed_count++; }
static void power_supply_unregister(struct power_supply *p)
{
    assert(registered == 1 && cancelled_at > unregistered_at);
    unregistered_at = ++sequence; registered--;
}
static int values[8] = {50, 4000, 500000, 250, 3500000, 7000000, 7040000, 17};
static int errors[8], read_count, probe_error;
static bool provider_ready = true;
static pthread_mutex_t bus_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bus_cond = PTHREAD_COND_INITIALIZER;
static bool blocked, entered;
static int provider_read(int index, int *out)
{
    pthread_mutex_lock(&bus_lock);
    read_count++; entered = true;
    pthread_cond_broadcast(&bus_cond);
    while (blocked) pthread_cond_wait(&bus_cond, &bus_lock);
    int ret = errors[index];
    if (!ret) *out = values[index];
    pthread_mutex_unlock(&bus_lock);
    return ret;
}
#define GETTER(name, idx) static int name(void *data, int *out) { return provider_read(idx, out); }
GETTER(get_soc, 0)
GETTER(get_voltage, 1)
GETTER(get_current, 2)
GETTER(get_temp, 3)
GETTER(get_rm, 4)
GETTER(get_fcc, 5)
GETTER(get_design, 6)
GETTER(get_cycles, 7)
static int probe_ok(void *data, bool *ok)
{
    if (!probe_error) *ok = provider_ready;
    return probe_error;
}
static const struct fuelguage_ic_ops ops = {
    .fg_ic_probe_ok = probe_ok, .fg_ic_get_rsoc = get_soc,
    .fg_ic_get_volt = get_voltage, .fg_ic_get_curr = get_current,
    .fg_ic_get_temp = get_temp, .fg_ic_get_rm = get_rm,
    .fg_ic_get_fcc = get_fcc, .fg_ic_get_full_design = get_design,
    .fg_ic_get_cyclecount = get_cycles,
};

/* PRODUCTION */

static void release_devres(struct platform_device *p)
{
    if (allocation) {
        struct annibale_battery *b = allocation;
        assert(!pthread_mutex_destroy(&b->lock.m));
        free(allocation); allocation = NULL; p->dev.data = NULL;
    }
}
static void poll_once(struct annibale_battery *b)
{
    b->poll.pending = false;
    b->poll.work.fn(&b->poll.work);
}
static void expect_property(enum power_supply_property prop, int want)
{
    union power_supply_propval v = {.intval = -777};
    assert(!annibale_battery_get_property(&supply, prop, &v));
    assert(v.intval == want);
}
static void expect_error(enum power_supply_property prop, int want)
{
    union power_supply_propval v = {.intval = -777};
    assert(annibale_battery_get_property(&supply, prop, &v) == want);
    assert(v.intval == -777);
}
static atomic_bool transition_started, transition_done;
static bool removing;
static void *reader(void *p)
{
    expect_property(POWER_SUPPLY_PROP_VOLTAGE_NOW, 4000000);
    return NULL;
}
static void *transition(void *p)
{
    struct platform_device *dev = p;
    atomic_store(&transition_started, true);
    if (removing) annibale_battery_remove(dev);
    else assert(!annibale_battery_suspend(&dev->dev));
    atomic_store(&transition_done, true);
    return NULL;
}
static void concurrent_transition(struct platform_device *dev, bool remove)
{
    pthread_t r, t;
    struct annibale_battery *b = dev->dev.data;
    removing = remove;
    atomic_store(&transition_started, false); atomic_store(&transition_done, false);
    pthread_mutex_lock(&bus_lock); blocked = true; entered = false;
    pthread_mutex_unlock(&bus_lock);
    assert(!pthread_create(&r, NULL, reader, dev));
    pthread_mutex_lock(&bus_lock);
    while (!entered) pthread_cond_wait(&bus_cond, &bus_lock);
    pthread_mutex_unlock(&bus_lock);
    assert(pthread_mutex_trylock(&b->lock.m) == EBUSY);
    assert(!pthread_create(&t, NULL, transition, dev));
    while (!atomic_load(&transition_started)) ;
    assert(!atomic_load(&transition_done));
    pthread_mutex_lock(&bus_lock); blocked = false;
    pthread_cond_broadcast(&bus_cond); pthread_mutex_unlock(&bus_lock);
    assert(!pthread_join(r, NULL) && !pthread_join(t, NULL));
    assert(atomic_load(&transition_done));
    assert(!b->poll.pending);
}
int main(void)
{
    struct platform_device dev = {.dev.of_node = (void *)1};
    int out, before, queued;
    hwid_ready = false;
    assert(annibale_battery_probe(&dev) == -ENODEV);
    hwid_ready = true; product = "dada";
    assert(annibale_battery_probe(&dev) == -ENODEV);
    product = "annibale";
    assert(annibale_battery_probe(&dev) == -EPROBE_DEFER);
    assert(!platform_fg_ic_ops_register(FG_IC_MASTER, values, &ops));
    provider_ready = false;
    assert(annibale_battery_probe(&dev) == -EPROBE_DEFER);
    provider_ready = true; probe_error = -EIO;
    assert(annibale_battery_probe(&dev) == -EIO);
    probe_error = 0; existing = true;
    assert(annibale_battery_probe(&dev) == -EBUSY && !refs);
    existing = false; alloc_fail = true;
    assert(annibale_battery_probe(&dev) == -ENOMEM);
    alloc_fail = false; register_error = -EEXIST;
    assert(annibale_battery_probe(&dev) == -EEXIST && !registered && !queue_count);
    release_devres(&dev); register_error = 0;
    assert(!annibale_battery_probe(&dev) && registered == 1);
    struct annibale_battery *b = dev.dev.data;
    assert(b->poll.pending && last_delay == 0);
    int expected[] = {50, 4000000, -500000, 250, 3500000, 7000000, 7040000, 17, 0, 0};
    for (int i = 0; i < 10; i++) expect_property(annibale_battery_props[i], expected[i]);
    values[2] = -1000000; expect_property(POWER_SUPPLY_PROP_CURRENT_NOW, 1000000);
    values[2] = 0; expect_property(POWER_SUPPLY_PROP_CURRENT_NOW, 0);
    values[0] = 0; expect_property(POWER_SUPPLY_PROP_CAPACITY, 0);
    values[0] = 100; expect_property(POWER_SUPPLY_PROP_CAPACITY, 100);
    values[3] = -100; expect_property(POWER_SUPPLY_PROP_TEMP, -100);
    struct { int index, value; enum power_supply_property prop; } invalid[] = {
        {0, -1, POWER_SUPPLY_PROP_CAPACITY}, {0, 101, POWER_SUPPLY_PROP_CAPACITY},
        {1, 0, POWER_SUPPLY_PROP_VOLTAGE_NOW}, {1, INT_MAX, POWER_SUPPLY_PROP_VOLTAGE_NOW},
        {2, INT_MIN, POWER_SUPPLY_PROP_CURRENT_NOW},
    };
    for (int i = 0; i < ARRAY_SIZE(invalid); i++) {
        int old = values[invalid[i].index]; values[invalid[i].index] = invalid[i].value;
        out = -777;
        assert(annibale_battery_read(invalid[i].prop, &out) == -ERANGE && out == -777);
        expect_error(invalid[i].prop, -ENODATA); values[invalid[i].index] = old;
    }
    expect_error(POWER_SUPPLY_PROP_PRESENT, -EINVAL);
    poll_once(b); assert(changed_count == 1 && last_delay == 30000);
    poll_once(b); assert(changed_count == 1);
    values[0] = 51; poll_once(b); assert(changed_count == 2);
    errors[0] = -EIO; poll_once(b); assert(changed_count == 3);
    expect_error(POWER_SUPPLY_PROP_CAPACITY, -ENODATA);
    expect_property(POWER_SUPPLY_PROP_VOLTAGE_NOW, 4000000);
    expect_property(POWER_SUPPLY_PROP_STATUS, POWER_SUPPLY_STATUS_UNKNOWN);
    expect_property(POWER_SUPPLY_PROP_HEALTH, POWER_SUPPLY_HEALTH_UNKNOWN);
    poll_once(b); assert(changed_count == 3);
    errors[0] = 0; poll_once(b); assert(changed_count == 4);
    /* Property reads use fresh provider data, never the notification snapshot. */
    values[0] = 52; expect_property(POWER_SUPPLY_PROP_CAPACITY, 52);
    poll_once(b); assert(changed_count == 5);
    annibale_battery_external_changed(&supply);
    assert(b->force_notify && last_delay == 0);
    poll_once(b); assert(changed_count == 6);
    concurrent_transition(&dev, false);
    before = read_count; queued = queue_count;
    expect_error(POWER_SUPPLY_PROP_TEMP, -ENODATA);
    poll_once(b); annibale_battery_external_changed(&supply);
    assert(read_count == before && queue_count == queued);
    assert(!annibale_battery_resume(&dev.dev) && last_delay == 0);
    poll_once(b); assert(changed_count == 7);
    assert(!platform_fg_ic_ops_unregister(FG_IC_MASTER, values, &ops));
    expect_error(POWER_SUPPLY_PROP_CAPACITY, -ENODATA);
    poll_once(b); assert(changed_count == 8);
    assert(!platform_fg_ic_ops_register(FG_IC_MASTER, values, &ops));
    poll_once(b); assert(changed_count == 9);
    concurrent_transition(&dev, true);
    assert(!registered && unregistered_at > cancelled_at);
    before = read_count; queued = queue_count;
    expect_error(POWER_SUPPLY_PROP_CAPACITY, -ENODEV);
    poll_once(b); annibale_battery_external_changed(&supply);
    assert(!annibale_battery_resume(&dev.dev));
    assert(read_count == before && queue_count == queued);
    assert(!platform_fg_ic_ops_unregister(FG_IC_MASTER, values, &ops));
    release_devres(&dev);
    puts("PASS: battery units/errors, probe rollback, notifications, provider loss, concurrent PM/remove");
    return 0;
}
