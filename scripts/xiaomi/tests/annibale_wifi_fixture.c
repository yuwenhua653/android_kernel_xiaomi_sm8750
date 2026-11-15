/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
typedef int irqreturn_t;
#define IRQ_HANDLED 1
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
struct work_struct { int unused; };
struct delayed_work { struct work_struct work; bool pending; };
#define to_delayed_work(p) container_of(p, struct delayed_work, work)
struct mutex { bool locked; };
struct device { int unused; } dev;
struct wifi_gpio_data {
 struct device *dev;
 int gpio, irq;
 u32 debounce_ms;
 struct delayed_work work;
 int event_lock;
 unsigned long generation;
};
struct platform_device { struct wifi_gpio_data *data; };
struct sk_buff { int unused; }; struct genl_info { int unused; };
static struct mutex sar_lock;
static struct wifi_gpio_data data, *sar_device;
static int reads, sends, gpio_result, send_error, irq_freed, cancelled, wake, race;
static int system_wq;
static u32 sent_status;
static irqreturn_t wifi_gpio_irq(int irq, void *ptr);
#define spin_lock_irqsave(l, f) do { (f) = 0; assert(!*(l)); *(l) = 1; } while (0)
#define spin_unlock_irqrestore(l, f) do { (void)(f); assert(*(l)); *(l) = 0; } while (0)
static void mutex_lock(struct mutex *m) { assert(!m->locked); m->locked = true; }
static void mutex_unlock(struct mutex *m) { assert(m->locked); m->locked = false; }
static int gpio_get_value_cansleep(int gpio) { assert(gpio == 79); reads++; return gpio_result; }
static int sar_send_status(u32 status)
{
 sends++; sent_status = status;
 if (race) { race = 0; wifi_gpio_irq(5, &data); }
 return send_error;
}
static void pm_relax(struct device *d) { wake = 0; }
static void pm_stay_awake(struct device *d) { wake = 1; }
static bool delayed_work_pending(struct delayed_work *w) { return w->pending; }
static u32 msecs_to_jiffies(u32 ms) { return ms; }
static void mod_delayed_work(int wq, struct delayed_work *w, u32 delay)
{ assert(delay == 30); w->pending = true; }
static struct wifi_gpio_data *platform_get_drvdata(struct platform_device *p) { return p->data; }
static void dev_pm_clear_wake_irq(struct device *d) { }
static void devm_free_irq(struct device *d, int irq, void *p) { assert(irq == 5); irq_freed = 1; }
static void cancel_delayed_work_sync(struct delayed_work *w)
{ assert(irq_freed && !sar_device && !sar_lock.locked); cancelled = 1; w->pending = false; }
static void device_init_wakeup(struct device *d, bool enabled) { assert(!enabled && cancelled && !wake); }
/* PRODUCTION */
int main(void)
{
 struct platform_device p = { &data };
 assert(sar_wpas_ready(NULL, NULL) == -ENODEV && !reads);
 data.dev = &dev; data.gpio = 79; data.irq = 5; data.debounce_ms = 30;
 sar_device = &data; gpio_result = -EIO;
 assert(sar_wpas_ready(NULL, NULL) == -EIO && !sends);
 gpio_result = 0; assert(!sar_wpas_ready(NULL, NULL) && sent_status == 0);
 gpio_result = 1; send_error = -ENOMEM;
 assert(sar_wpas_ready(NULL, NULL) == -ENOMEM && sent_status == 1);
 send_error = 0;
 assert(wifi_gpio_irq(5, &data) == IRQ_HANDLED && wake && data.work.pending);
 data.work.pending = false; wifi_gpio_work(&data.work.work); assert(!wake);
 wifi_gpio_irq(5, &data); data.work.pending = false;
 race = 1; wifi_gpio_work(&data.work.work); assert(wake && data.work.pending);
 /* An old worker must not relax a newly queued edge, even if it sampled
  * the new generation at entry. */
 wifi_gpio_work(&data.work.work); assert(wake && data.work.pending);
 data.work.pending = false; wifi_gpio_work(&data.work.work); assert(!wake);
 wifi_gpio_irq(5, &data); assert(!wifi_gpio_remove(&p) && cancelled && !wake);
 int old = reads; assert(sar_wpas_ready(NULL, NULL) == -ENODEV && reads == old);
 puts("PASS: Wi-Fi no-device/read/send errors, debounce wake ownership and IRQ-before-work teardown");
 return 0;
}
