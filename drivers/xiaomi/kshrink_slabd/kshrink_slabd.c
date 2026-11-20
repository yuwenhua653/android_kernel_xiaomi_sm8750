// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2024 Xiaomi Ltd. */
#define pr_fmt(fmt) "kshrink_slabd: " fmt
#include <linux/swap.h>
#include <linux/cpufreq.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/memcontrol.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <trace/hooks/vmscan.h>

extern unsigned long shrink_slab(gfp_t gfp_mask, int nid,
				 struct mem_cgroup *memcg, int priority);
static struct task_struct *shrink_slabd_tsk;
static bool async_shrink_slabd_setup;
static DECLARE_WAIT_QUEUE_HEAD(shrink_slabd_wait);
static DEFINE_SPINLOCK(request_lock);
struct async_slabd_parameter {
	struct mem_cgroup *memcg;
	gfp_t gfp_mask;
	int nid;
	int priority;
	bool pending;
	bool submitted;
	unsigned long last_submit;
};
static struct async_slabd_parameter asp;

static bool wakeup_shrink_slabd(gfp_t gfp_mask, int nid,
			      struct mem_cgroup *memcg, int priority)
{
	unsigned long flags;
	bool accepted = false;

	spin_lock_irqsave(&request_lock, flags);
	if (!async_shrink_slabd_setup || asp.pending ||
	    (asp.submitted && time_before(jiffies, asp.last_submit + HZ)))
		goto out;
	if (memcg && !css_tryget_online(&memcg->css))
		goto out;
	asp.memcg = memcg;
	asp.gfp_mask = gfp_mask;
	asp.nid = nid;
	asp.priority = priority;
	asp.pending = true;
	asp.submitted = true;
	asp.last_submit = jiffies;
	accepted = true;
 out:
	spin_unlock_irqrestore(&request_lock, flags);
	if (accepted)
		wake_up_interruptible(&shrink_slabd_wait);
	return accepted;
}

static void set_async_slabd_cpus(void)
{
	struct cpumask fastest, allowed;
	unsigned int cpu, max_freq = 0;
	static bool applied;

	if (applied)
		return;
	cpumask_clear(&fastest);
	for_each_possible_cpu(cpu) {
		struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);

		if (!policy)
			continue;
		if (policy->cpuinfo.max_freq > max_freq) {
			max_freq = policy->cpuinfo.max_freq;
			cpumask_copy(&fastest, policy->related_cpus);
		} else if (policy->cpuinfo.max_freq == max_freq) {
			cpumask_or(&fastest, &fastest, policy->related_cpus);
		}
		cpufreq_cpu_put(policy);
	}
	if (!max_freq)
		return;
	cpumask_andnot(&allowed, cpu_online_mask, &fastest);
	if (!cpumask_empty(&allowed) && !set_cpus_allowed_ptr(current, &allowed))
		applied = true;
}

static int kshrink_slabd_func(void *unused)
{
	struct mem_cgroup *memcg;
	gfp_t gfp_mask;
	int nid, priority;
	unsigned long flags;

	current->flags |= PF_MEMALLOC | PF_KSWAPD;
	set_freezable();
	for (;;) {
		wait_event_freezable(shrink_slabd_wait,
			kthread_should_stop() || READ_ONCE(asp.pending));
		spin_lock_irqsave(&request_lock, flags);
		if (kthread_should_stop()) {
			memcg = asp.pending ? asp.memcg : NULL;
			asp.pending = false;
			asp.memcg = NULL;
			spin_unlock_irqrestore(&request_lock, flags);
			if (memcg)
				css_put(&memcg->css);
			break;
		}
		if (!asp.pending) {
			spin_unlock_irqrestore(&request_lock, flags);
			continue;
		}
		memcg = asp.memcg;
		gfp_mask = asp.gfp_mask;
		nid = asp.nid;
		priority = asp.priority;
		spin_unlock_irqrestore(&request_lock, flags);
		set_async_slabd_cpus();
		shrink_slab(gfp_mask, nid, memcg, priority);
		if (memcg)
			css_put(&memcg->css);
		spin_lock_irqsave(&request_lock, flags);
		asp.memcg = NULL;
		asp.pending = false;
		spin_unlock_irqrestore(&request_lock, flags);
	}
	current->flags &= ~(PF_MEMALLOC | PF_KSWAPD);
	return 0;
}

static void should_shrink_async(void *data, gfp_t gfp_mask, int nid,
			       struct mem_cgroup *memcg, int priority, bool *bypass)
{
	if (*bypass || current == READ_ONCE(shrink_slabd_tsk))
		return;
	/* No ROM-specific process names. Failed/busy requests reclaim normally. */
	if (wakeup_shrink_slabd(gfp_mask, nid, memcg, priority))
		*bypass = true;
}

static int __init shrink_async_init(void)
{
	int ret;
	unsigned long flags;

	shrink_slabd_tsk = kthread_run(kshrink_slabd_func, NULL, "kshrink_slabd");
	if (IS_ERR(shrink_slabd_tsk)) {
		ret = PTR_ERR(shrink_slabd_tsk);
		shrink_slabd_tsk = NULL;
		return ret;
	}
	ret = register_trace_android_vh_shrink_slab_bypass(should_shrink_async, NULL);
	if (ret) {
		kthread_stop(shrink_slabd_tsk);
		shrink_slabd_tsk = NULL;
		return ret;
	}
	spin_lock_irqsave(&request_lock, flags);
	async_shrink_slabd_setup = true;
	spin_unlock_irqrestore(&request_lock, flags);
	return 0;
}

static void __exit shrink_async_exit(void)
{
	unsigned long flags;

	spin_lock_irqsave(&request_lock, flags);
	async_shrink_slabd_setup = false;
	spin_unlock_irqrestore(&request_lock, flags);
	unregister_trace_android_vh_shrink_slab_bypass(should_shrink_async, NULL);
	tracepoint_synchronize_unregister();
	kthread_stop(shrink_slabd_tsk);
}
module_init(shrink_async_init);
module_exit(shrink_async_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Bounded asynchronous slab reclaim for Annibale");
