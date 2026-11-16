/* SPDX-License-Identifier: GPL-2.0-only */
typedef int16_t s16;
struct mutex { pthread_mutex_t m; };
static void mutex_init(struct mutex *m) { assert(!pthread_mutex_init(&m->m, NULL)); }
static void mutex_lock(struct mutex *m) { assert(!pthread_mutex_lock(&m->m)); }
static void mutex_unlock(struct mutex *m) { assert(!pthread_mutex_unlock(&m->m)); }
struct device { void *of_node, *data; int kobj; };
struct i2c_client { struct device dev; int addr, flags; void *adapter; };
#define I2C_CLIENT_TEN 0x10
#define I2C_FUNC_SMBUS_READ_WORD_DATA 1
#define GFP_KERNEL 0
static bool ready = true, supported = true;
static const char *product = "annibale";
static int role, dt_error, sysfs_error, groups, allocations;
static void *last_alloc;
static bool alloc_fail;
static int reads, bus_error, registers[256];
static u8 last_reg;
static pthread_mutex_t bus_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bus_cond = PTHREAD_COND_INITIALIZER;
static bool block_bus, bus_entered;
static atomic_bool removed;
static const int annibale_fg_group;
static bool xiaomi_hwid_is_ready(void) { return ready; }
static const char *product_name_get(void) { return product; }
static int of_property_read_u32(void *node, const char *name, u32 *out)
{
    assert(node && !strcmp(name, "ic_role"));
    if (!dt_error) *out = role;
    return dt_error;
}
static bool i2c_check_functionality(void *adapter, int flags) { return supported; }
static void *devm_kzalloc(struct device *dev, size_t n, int flags)
{
    if (alloc_fail) return NULL;
    allocations++;
    assert(!last_alloc);
    return last_alloc = calloc(1, n);
}
static void i2c_set_clientdata(struct i2c_client *c, void *p) { c->dev.data = p; }
static void *i2c_get_clientdata(struct i2c_client *c) { return c->dev.data; }
static void *dev_get_drvdata(struct device *d) { return d->data; }
static int dev_err_probe(struct device *d, int err, const char *fmt, ...) { return err; }
static int sysfs_create_group(void *kobj, const void *group)
{
    if (sysfs_error) return sysfs_error;
    groups++;
    return 0;
}
static void sysfs_remove_group(void *kobj, const void *group) { assert(groups == 1); groups--; }
static int i2c_smbus_read_word_data(struct i2c_client *client, u8 reg)
{
    pthread_mutex_lock(&bus_lock);
    reads++;
    last_reg = reg;
    bus_entered = true;
    pthread_cond_broadcast(&bus_cond);
    while (block_bus) pthread_cond_wait(&bus_cond, &bus_lock);
    int result = bus_error ? bus_error : registers[reg];
    pthread_mutex_unlock(&bus_lock);
    return result;
}

/* PRODUCTION */

static void release_devres(struct i2c_client *c)
{
    if (last_alloc) {
        struct annibale_fg *fg = last_alloc;
        assert(!pthread_mutex_destroy(&fg->lock.m));
        free(last_alloc);
        last_alloc = NULL;
        c->dev.data = NULL;
    }
}

static void conversions(void)
{
    struct sample { enum annibale_fg_value kind; int raw, want; } samples[] = {
        {FG_VOLTAGE_MV, 4012, 4012}, {FG_CURRENT_UA, 1000, -1000000},
        {FG_CURRENT_UA, 0xfc18, 1000000}, {FG_CURRENT_UA, 0, 0},
        {FG_CURRENT_UA, 25000, -25000000}, {FG_CURRENT_UA, 59536, 6000000},
        {FG_TEMP_DECIC, 2980, 250}, {FG_TEMP_DECIC, 2630, -100},
        {FG_INTERNAL_TEMP_DECIC, 3030, 300}, {FG_SOC_PERCENT, 0, 0},
        {FG_SOC_PERCENT, 100, 100}, {FG_REMAINING_UAH, 0, 0},
        {FG_REMAINING_UAH, 3500, 3500000}, {FG_FULL_UAH, 7040, 7040000},
        {FG_DESIGN_UAH, 7040, 7040000}, {FG_CYCLES, 400, 400},
        {FG_SOH_PERCENT, 92, 92}, {FG_FLAGS, 0xa55a, 0xa55a},
    };
    struct sample bad[] = {
        {FG_VOLTAGE_MV, 0, 0}, {FG_VOLTAGE_MV, 5001, 0},
        {FG_CURRENT_UA, 0x8000, 0}, {FG_CURRENT_UA, 25001, 0},
        {FG_CURRENT_UA, 59535, 0}, {FG_TEMP_DECIC, 1449, 0},
        {FG_INTERNAL_TEMP_DECIC, 4011, 0}, {FG_SOC_PERCENT, 101, 0},
        {FG_SOH_PERCENT, 101, 0}, {FG_FULL_UAH, 0, 0},
        {FG_FULL_UAH, 8001, 0}, {FG_DESIGN_UAH, 0, 0},
        {FG_REMAINING_UAH, 65535, 0},
    };
    int out;
    for (unsigned i = 0; i < sizeof(samples)/sizeof(samples[0]); i++) {
        out = -777;
        assert(!annibale_fg_decode(samples[i].kind, samples[i].raw, &out));
        assert(out == samples[i].want);
    }
    for (unsigned i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
        out = -777;
        assert(annibale_fg_decode(bad[i].kind, bad[i].raw, &out) == -ERANGE);
        assert(out == -777);
    }
    out = -777;
    assert(annibale_fg_decode(FG_VALUE_COUNT, 1, &out) == -EINVAL && out == -777);
}

static void *read_thread(void *arg)
{
    int value;
    assert(!platform_fg_ops_get_volt(FG_IC_MASTER, &value) && value == 4000);
    return NULL;
}
static void *remove_thread(void *arg)
{
    annibale_fg_remove(arg);
    atomic_store(&removed, true);
    return NULL;
}

int main(void)
{
    struct i2c_client c = {.addr = 0x55, .dev.of_node = (void *)1};
    int out, before;
    bool ok;
    conversions();
    registers[0x08] = 4000;
    registers[0x2c] = 50;
    ready = false;
    assert(annibale_fg_probe(&c) == -ENODEV);
    ready = true; product = "dada";
    assert(annibale_fg_probe(&c) == -ENODEV);
    product = "annibale"; c.addr = 0x56;
    assert(annibale_fg_probe(&c) == -ENODEV);
    c.addr = 0x55; c.flags = I2C_CLIENT_TEN;
    assert(annibale_fg_probe(&c) == -ENODEV);
    c.flags = 0; c.dev.of_node = NULL;
    assert(annibale_fg_probe(&c) == -ENODEV);
    c.dev.of_node = (void *)1; role = FG_IC_SLAVE;
    assert(annibale_fg_probe(&c) == -EINVAL);
    role = FG_IC_MASTER; dt_error = -EINVAL;
    assert(annibale_fg_probe(&c) == -EINVAL);
    dt_error = 0; supported = false;
    assert(annibale_fg_probe(&c) == -EOPNOTSUPP);
    supported = true;
    assert(!reads && !allocations);
    alloc_fail = true;
    assert(annibale_fg_probe(&c) == -ENOMEM);
    alloc_fail = false; bus_error = -EREMOTEIO;
    assert(annibale_fg_probe(&c) == -EREMOTEIO && !groups);
    out = 777;
    assert(platform_fg_ops_get_volt(FG_IC_MASTER, &out) < 0 && out == 777);
    release_devres(&c); bus_error = 0;
    registers[0x2c] = 101;
    assert(annibale_fg_probe(&c) == -ERANGE && !groups);
    release_devres(&c); registers[0x2c] = 50;
    /* A duplicate provider must survive our failed probe unchanged. */
    struct annibale_fg incumbent = {.client = &c};
    mutex_init(&incumbent.lock);
    assert(!platform_fg_ic_ops_register(FG_IC_MASTER, &incumbent, &annibale_fg_ops));
    assert(annibale_fg_probe(&c) == -EBUSY && !groups);
    assert(!platform_fg_ops_get_volt(FG_IC_MASTER, &out) && out == 4000);
    assert(!platform_fg_ic_ops_unregister(FG_IC_MASTER, &incumbent, &annibale_fg_ops));
    assert(!pthread_mutex_destroy(&incumbent.lock.m));
    release_devres(&c);
    sysfs_error = -ENOMEM;
    assert(annibale_fg_probe(&c) == -ENOMEM && !groups);
    assert(platform_fg_ops_get_volt(FG_IC_MASTER, &out) < 0);
    release_devres(&c); sysfs_error = 0;
    assert(!annibale_fg_probe(&c) && groups == 1);
    struct annibale_fg *fg = c.dev.data;
    assert(!platform_fg_ops_get_volt(FG_IC_MASTER, &out) && out == 4000 && last_reg == 0x08);
    assert(!platform_fg_ops_get_rsoc(FG_IC_MASTER, &out) && out == 50 && last_reg == 0x2c);
    registers[0x0c] = 0xfc18;
    assert(!platform_fg_ops_get_curr(FG_IC_MASTER, &out) && out == 1000000 && last_reg == 0x0c);
    registers[0x06] = 2980;
    assert(!platform_fg_ops_get_temp(FG_IC_MASTER, &out) && out == 250 && last_reg == 0x06);
    registers[0x1e] = 3030;
    assert(!platform_fg_ops_get_original_temp(FG_IC_MASTER, &out) && out == 300 && last_reg == 0x1e);
    registers[0x10] = 3500;
    assert(!platform_fg_ops_get_rm(FG_IC_MASTER, &out) && out == 3500000 && last_reg == 0x10);
    registers[0x12] = registers[0x3c] = 7040;
    assert(!platform_fg_ops_get_fcc(FG_IC_MASTER, &out) && out == 7040000 && last_reg == 0x12);
    assert(!platform_fg_ops_get_full_design(FG_IC_MASTER, &out) && out == 7040000 && last_reg == 0x3c);
    registers[0x2a] = 400; registers[0x2e] = 92;
    assert(!platform_fg_ops_get_cyclecount(FG_IC_MASTER, &out) && out == 400 && last_reg == 0x2a);
    assert(!platform_fg_ops_get_soh(FG_IC_MASTER, &out) && out == 92 && last_reg == 0x2e);
    assert(!platform_fg_ops_get_chip_ok(FG_IC_MASTER, &out) && out == 1 && last_reg == 0x0a);
    ok = false;
    assert(!platform_fg_ops_probe_ok(FG_IC_MASTER, &ok) && ok);
    before = reads;
    assert(platform_fg_ops_set_fastcharge(FG_IC_MASTER, true) < 0 && reads == before);
    assert(annibale_fg_read(fg, FG_VALUE_COUNT, &out) == -EINVAL);
    assert(annibale_fg_read(fg, -1, &out) == -EINVAL);
    assert(annibale_fg_read(NULL, FG_FLAGS, &out) == -EINVAL);
    assert(annibale_fg_read(fg, FG_FLAGS, NULL) == -EINVAL);
    assert(reads == before);
    int errors[] = {-EREMOTEIO, -ETIMEDOUT, -EAGAIN, -EIO};
    for (unsigned i = 0; i < sizeof(errors)/sizeof(errors[0]); i++) {
        bus_error = errors[i]; out = 777; ok = false;
        assert(platform_fg_ops_get_volt(FG_IC_MASTER, &out) == bus_error && out == 777);
        assert(platform_fg_ops_probe_ok(FG_IC_MASTER, &ok) == bus_error && !ok);
        assert(platform_fg_ops_get_chip_ok(FG_IC_MASTER, &out) == bus_error && out == 777);
    }
    bus_error = 0; before = reads;
    assert(!annibale_fg_suspend(&c.dev));
    assert(platform_fg_ops_get_volt(FG_IC_MASTER, &out) == -EHOSTDOWN && out == 777);
    assert(reads == before);
    assert(!annibale_fg_resume(&c.dev));
    assert(!platform_fg_ops_get_volt(FG_IC_MASTER, &out) && out == 4000);
    /* remove must wait for the actual in-flight registry + SMBus reader. */
    pthread_t reader, remover;
    block_bus = true; bus_entered = false;
    assert(!pthread_create(&reader, NULL, read_thread, NULL));
    pthread_mutex_lock(&bus_lock);
    while (!bus_entered) pthread_cond_wait(&bus_cond, &bus_lock);
    pthread_mutex_unlock(&bus_lock);
    assert(!pthread_create(&remover, NULL, remove_thread, &c));
    struct timespec delay = {.tv_nsec = 20000000};
    nanosleep(&delay, NULL);
    assert(!atomic_load(&removed));
    pthread_mutex_lock(&bus_lock);
    block_bus = false;
    pthread_cond_broadcast(&bus_cond);
    pthread_mutex_unlock(&bus_lock);
    assert(!pthread_join(reader, NULL));
    assert(!pthread_join(remover, NULL));
    assert(atomic_load(&removed) && !groups);
    before = reads; out = 777;
    assert(annibale_fg_read(fg, FG_FLAGS, &out) == -ENODEV && out == 777);
    assert(platform_fg_ops_get_volt(FG_IC_MASTER, &out) < 0 && out == 777);
    assert(reads == before);
    release_devres(&c);
    puts("PASS: gauge conversions, bus faults, real MCA dispatch, probe rollback, PM, concurrent remove");
    return 0;
}
