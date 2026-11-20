/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t u32;
/* CONTRACT */
struct device { int unused; };
struct device_node { int unused; };
struct regmap { u32 control, settings; };
struct fsa4480_priv { struct regmap *regmap; };
struct i2c_client { struct device dev; struct fsa4480_priv *priv; };
static struct regmap map;
static struct fsa4480_priv priv;
static struct i2c_client client;
static int step, fail_step, references, validation_error;
static bool present;

static int regmap_read(struct regmap *m, int reg, u32 *value)
{
    if (++step == fail_step) return -EIO;
    *value = reg == FSA4480_SWITCH_CONTROL ? m->control : m->settings;
    return 0;
}
static int regmap_write(struct regmap *m, int reg, u32 value)
{
    if (++step == fail_step) return -EIO;
    if (reg == FSA4480_SWITCH_CONTROL) m->control = value;
    else m->settings = value;
    return 0;
}
static void usleep_range(int low, int high) { assert(low == 50 && high == 55); }
static struct i2c_client *of_find_i2c_device_by_node(struct device_node *node)
{
    if (!present) return NULL;
    references++;
    return &client;
}
static void *i2c_get_clientdata(struct i2c_client *c) { return c->priv; }
static void put_device(struct device *dev) { assert(references > 0); references--; }
static int fsa4480_validate_display_port_settings(struct fsa4480_priv *p)
{ return validation_error; }
/* PRODUCTION */
static void reset(void)
{
    assert(references == 0);
    map = (struct regmap){0};
    priv.regmap = &map;
    client.priv = &priv;
    step = fail_step = validation_error = 0;
    present = true;
}
int main(void)
{
    struct device_node node;
    for (int failure = 1; failure <= 5; failure++) {
        reset(); fail_step = failure;
        assert(fsa4480_switch_event(&node, FSA_USBC_LPD_STATUS_CC1) == -EIO);
        assert(step == failure && references == 0);
    }
    reset();
    assert(!fsa4480_switch_event(&node, FSA_USBC_LPD_STATUS_CC1));
    assert(map.control == 0x19 && map.settings == 0xd9 && references == 0);
    reset();
    assert(!fsa4480_switch_event(&node, FSA_USBC_LPD_STATUS_CC2));
    assert(map.control == 0x78 && map.settings == 0xd9 && references == 0);
    reset(); validation_error = -EIO;
    assert(fsa4480_switch_event(&node, FSA_USBC_ORIENTATION_CC1) == -EIO);
    assert(references == 0);
    reset(); client.priv = NULL;
    assert(fsa4480_switch_event(&node, FSA_MIC_GND_SWAP) == -EINVAL);
    assert(references == 0);
    reset(); priv.regmap = NULL;
    assert(fsa4480_switch_event(&node, FSA_MIC_GND_SWAP) == -EINVAL);
    assert(references == 0);
    reset(); present = false;
    assert(fsa4480_switch_event(&node, FSA_MIC_GND_SWAP) == -EINVAL);
    reset();
    assert(fsa4480_switch_event(&node, FSA_EVENT_MAX) == -EINVAL);
    assert(step == 0 && references == 0);
    puts("PASS: FSA LPD registers, I2C failure propagation and device reference release");
}
