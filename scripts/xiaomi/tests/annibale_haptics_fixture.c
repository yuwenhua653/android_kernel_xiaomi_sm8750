/* SPDX-License-Identifier: GPL-2.0-only */
/* Host-only mocks; never linked into the kernel. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
#define USEC_PER_SEC 1000000U
#define BIT(n) (1U << (n))
#define GENMASK(h, l) (((~0U) >> (31 - (h))) & ((~0U) << (l)))
#define READ_ONCE(x) (x)
#define dev_err(...) ((void)0)
/* PRODUCTION_CONSTANTS */
struct atomic_t { int value; };
struct mutex { int depth; };
struct haptics_effect { u32 t_lra_us, vmax_mv; };
struct haptics_chip {
	void *regmap, *dev;
	u32 cfg_addr_base, max_vmax_mv;
	bool xiaomi_haptics, swr_slave_enabled;
	struct atomic_t mi_control_busy;
	struct {
		bool is_erm;
		u32 t_lra_us, cl_t_lra_us, vmax_mv, fifo_vmax_mv;
	} config;
	struct {
		struct mutex lock;
		bool in_calibration;
		struct { struct atomic_t is_busy; } fifo_status;
	} play;
	struct haptics_effect *custom_effect;
};
struct regulator_dev { struct haptics_chip *chip; };
static struct haptics_chip chip;
static struct haptics_effect effect;
static struct regulator_dev rdev = { .chip = &chip };
static int pm_rc, pm_refs, pm_puts, read_rc, write_rc, adjust_rc, masked_rc;
static int writes, starts, swr_starts, adjusted_period, race_start;
static u8 play_reg;
static u32 programmed_period;
static int cases;
static int haptics_enable_play(struct haptics_chip *, bool);
static int swr_slave_reg_enable(struct regulator_dev *);
static int atomic_read(struct atomic_t *a) { return a->value; }
static void atomic_set_release(struct atomic_t *a, int v) { a->value = v; }
static int atomic_cmpxchg(struct atomic_t *a, int old, int newval)
{
	int previous = a->value;
	if (previous == old)
		a->value = newval;
	return previous;
}
static void mutex_lock(struct mutex *m) { assert(!m->depth); m->depth++; }
static void mutex_unlock(struct mutex *m) { assert(m->depth == 1); m->depth--; }
static int haptics_runtime_resume_get(struct haptics_chip *c)
{
	(void)c;
	if (pm_rc >= 0)
		pm_refs++;
	return pm_rc;
}
static void haptics_runtime_autosuspend_put(struct haptics_chip *c)
{
	(void)c;
	assert(pm_refs == 1);
	pm_refs--; pm_puts++;
}
static int regmap_update_bits(void *r, u16 addr, u8 mask, u8 val)
{
	(void)r; (void)addr; (void)mask; (void)val;
	return masked_rc;
}
static int haptics_adjust_lra_period(struct haptics_chip *c, u32 *period)
{
	(void)c;
	if (adjusted_period)
		*period = adjusted_period;
	return adjust_rc;
}
static int haptics_read(struct haptics_chip *c, u16 base, u8 off, u8 *val, u32 len)
{
	(void)c; (void)base;
	assert(off == HAP_CFG_SPMI_PLAY_REG && len == 1);
	*val = play_reg;
	return read_rc;
}
static int haptics_write(struct haptics_chip *c, u16 base, u8 off, u8 *val, u32 len)
{
	(void)base;
	assert(off == HAP_CFG_TLRA_OL_HIGH_REG && len == 2);
	writes++;
	if (race_start) {
		assert(haptics_enable_play(c, true) == -EBUSY);
		assert(swr_slave_reg_enable(&rdev) == -EBUSY);
		assert(!starts && !swr_starts);
	}
	if (!write_rc)
		programmed_period = (((u32)val[0] << 8) | val[1]) * TLRA_STEP_US;
	return write_rc;
}
static int __haptics_enable_play(struct haptics_chip *c, bool en)
{
	(void)c;
	if (en)
		starts++;
	return 0;
}
static struct haptics_chip *rdev_get_drvdata(struct regulator_dev *r) { return r->chip; }
static int __swr_slave_reg_enable(struct regulator_dev *r)
{
	swr_starts++; r->chip->swr_slave_enabled = true;
	return 0;
}
/* PRODUCTION_FUNCTIONS */
static void reset(void)
{
	memset(&chip, 0, sizeof(chip));
	memset(&effect, 0, sizeof(effect));
	chip.xiaomi_haptics = true;
	chip.custom_effect = &effect;
	chip.max_vmax_mv = 10000;
	chip.config.t_lra_us = chip.config.cl_t_lra_us = effect.t_lra_us = 6667;
	chip.config.vmax_mv = chip.config.fifo_vmax_mv = effect.vmax_mv = 3600;
	pm_rc = pm_refs = pm_puts = read_rc = write_rc = adjust_rc = masked_rc = 0;
	writes = starts = swr_starts = adjusted_period = race_start = play_reg = 0;
	programmed_period = 0;
}
static void failed_f0(u32 f0, int error)
{
	assert(haptics_xiaomi_set_f0(&chip, f0) == error);
	assert(chip.config.t_lra_us == 6667 && chip.config.cl_t_lra_us == 6667);
	assert(effect.t_lra_us == 6667 && !pm_refs && !chip.play.lock.depth);
	cases++;
}
int main(void)
{
	reset(); failed_f0(0, -EINVAL); assert(!writes && !pm_puts);
	reset(); failed_f0(UINT32_MAX, -ERANGE); assert(!writes && !pm_puts);
	reset(); failed_f0(488, -ERANGE); assert(!writes && !pm_puts);
	reset(); chip.config.is_erm = true; failed_f0(1500, -EOPNOTSUPP);
	reset(); pm_rc = -EIO; failed_f0(1500, -EIO); assert(!pm_puts);
	reset(); read_rc = -EIO; failed_f0(1500, -EIO); assert(!writes && pm_puts == 1);
	reset(); write_rc = -EIO; failed_f0(1500, -EIO); assert(writes == 1 && pm_puts == 1);
	reset(); adjust_rc = -EIO; failed_f0(1500, -EIO); assert(!writes);
	reset(); adjusted_period = TLRA_MAX_US + 1; failed_f0(489, -ERANGE); assert(!writes);
	reset(); adjusted_period = TLRA_STEP_US - 1; failed_f0(1500, -ERANGE); assert(!writes);
	reset(); chip.play.in_calibration = true; failed_f0(1500, -EBUSY); assert(!writes);
	reset(); chip.play.fifo_status.is_busy.value = 1; failed_f0(1500, -EBUSY);
	reset(); chip.swr_slave_enabled = true; failed_f0(1500, -EBUSY);
	reset(); play_reg = PLAY_EN_BIT; failed_f0(1500, -EBUSY);
	reset(); chip.mi_control_busy.value = 1; failed_f0(1500, -EBUSY);
	assert(chip.mi_control_busy.value == 1); /* must not release someone else's gate */
	reset(); race_start = 1;
	assert(!haptics_xiaomi_set_f0(&chip, 1500));
	assert(chip.config.cl_t_lra_us == 6666 && chip.config.t_lra_us == 6666);
	assert(effect.t_lra_us == 6666 && programmed_period == 6665);
	assert(!chip.mi_control_busy.value && !pm_refs && pm_puts == 1); cases++;
	reset(); assert(!haptics_xiaomi_set_f0(&chip, 489)); assert(writes == 1); cases++;
	reset(); assert(!haptics_xiaomi_set_f0(&chip, 2000000)); assert(programmed_period == 5); cases++;
	reset(); assert(!haptics_xiaomi_set_vmax(&chip, 0, false)); assert(!chip.config.vmax_mv); cases++;
	reset(); assert(!haptics_xiaomi_set_vmax(&chip, 10000, true));
	assert(effect.vmax_mv == 10000 && chip.config.fifo_vmax_mv == 10000); cases++;
	reset(); assert(haptics_xiaomi_set_vmax(&chip, 10001, false) == -ERANGE);
	assert(chip.config.vmax_mv == 3600); cases++;
	reset(); assert(haptics_xiaomi_set_vmax(&chip, UINT32_MAX, true) == -ERANGE);
	assert(effect.vmax_mv == 3600); cases++;
	reset(); chip.max_vmax_mv = 6000;
	assert(haptics_xiaomi_set_vmax(&chip, 6001, false) == -ERANGE); cases++;
	reset(); chip.custom_effect = NULL;
	assert(haptics_xiaomi_set_vmax(&chip, 1000, true) == -ENODEV);
	assert(chip.config.fifo_vmax_mv == 3600 && !chip.play.lock.depth); cases++;
	reset(); masked_rc = -EIO; assert(haptics_masked_write(&chip, 0, 0, 0, 0) == -EIO); cases++;
	reset(); assert(!haptics_masked_write(&chip, 0, 0, 0, 0)); cases++;
	reset(); assert(!haptics_enable_play(&chip, true)); assert(starts == 1 && !chip.mi_control_busy.value); cases++;
	reset(); assert(!swr_slave_reg_enable(&rdev)); assert(swr_starts == 1 && !chip.mi_control_busy.value); cases++;
	reset(); chip.xiaomi_haptics = false; chip.mi_control_busy.value = 1;
	assert(!haptics_enable_play(&chip, true)); assert(starts == 1); cases++;
	reset(); chip.xiaomi_haptics = false; chip.mi_control_busy.value = 1;
	assert(!swr_slave_reg_enable(&rdev)); assert(swr_starts == 1); cases++;
	assert(cases == 30);
	printf("PASS: %d haptics control cases\n", cases);
	return 0;
}
