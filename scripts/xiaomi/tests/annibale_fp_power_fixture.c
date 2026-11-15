/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
struct regulator { int unused; } reg;
struct fp_vreg { bool IsGpio, enable; struct regulator *mRegulator; } v;
#define IS_ERR_OR_NULL(p) (!(p) || (uintptr_t)(p) >= (uintptr_t)-4095)
static int en_error, off_error, set_error, voltage, refs, enables, disables, sets;
static int regulator_enable(struct regulator *r) { enables++; if (!en_error) refs++; return en_error; }
static int regulator_disable(struct regulator *r) { disables++; if (!off_error) refs--; return off_error; }
static int regulator_get_voltage(struct regulator *r) { return voltage; }
static int regulator_set_voltage(struct regulator *r, int lo, int hi)
{ assert(lo == 3300000 && hi == 3300000); sets++; return set_error; }
static void usleep_range(int lo, int hi) { }
/* PRODUCTION */
static void reset(void)
{
 memset(&v, 0, sizeof(v)); v.mRegulator = &reg;
 en_error = off_error = set_error = refs = enables = disables = sets = 0;
 voltage = 1800000;
}
int main(void)
{
 reset(); v.mRegulator = NULL; assert(fp_power_on(&v) == -ENODEV && !enables);
 reset(); v.IsGpio = true; assert(fp_power_on(&v) == -ENODEV && !enables);
 reset(); en_error = -EPERM; assert(fp_power_on(&v) == -EPERM && !v.enable && !refs);
 reset(); voltage = -EIO; assert(fp_power_on(&v) == -EIO && !v.enable && !refs);
 reset(); voltage = 3100000; set_error = -ERANGE;
 assert(fp_power_on(&v) == -ERANGE && !v.enable && !refs && sets == 1);
 reset(); voltage = 3100000;
 assert(!fp_power_on(&v) && v.enable && refs == 1 && enables == 1 && sets == 1);
 assert(!fp_power_on(&v) && refs == 1 && enables == 1);
 off_error = -EBUSY; assert(fp_power_off(&v) == -EBUSY && v.enable && refs == 1);
 off_error = 0; assert(!fp_power_off(&v) && !v.enable && !refs);
 int old = disables; assert(!fp_power_off(&v) && disables == old);
 reset(); voltage = -EIO; off_error = -EBUSY;
 assert(fp_power_on(&v) == -EIO && v.enable && refs == 1);
 off_error = 0; assert(!fp_power_off(&v) && !refs);
 reset(); assert(!fp_power_on(&v) && !sets && refs == 1);
 assert(!fp_power_off(&v) && !refs);
 puts("PASS: fingerprint power errors, P01 single-enable transition and balanced rollback");
 return 0;
}
