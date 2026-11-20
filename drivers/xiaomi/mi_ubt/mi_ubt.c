// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for dump user backtrace.
 *
 * Copyright (C) 2024 Xiaomi Ltd.
 */

#define pr_fmt(fmt) "mi_ubt: "fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched/task_stack.h>
#include <linux/panic_notifier.h>
#include <asm/stacktrace.h>
#include <asm/memory.h>

static void dump_frames(unsigned long *stackframes, int frames_cnt,
			struct task_struct *task)
{
	int i = 0;
	unsigned long pc_cur = 0;
	struct mm_struct *mm;
	struct vm_area_struct *vma;

	if (task != current)
		mm = get_task_mm(task);
	else
		mm = task->mm;

	if (!mm)
		return;

	if (!mmap_read_trylock(mm)) {
		if (task != current)
			mmput(mm);
		return;
	}

	while (i < frames_cnt) {
		pc_cur = stackframes[i];
		if (!pc_cur) {
			i++;
			continue;
		}

		vma = find_vma(mm, pc_cur);
		if (!vma || pc_cur < vma->vm_start) {
			i++;
			continue;
		}

		if (vma->vm_file) {
			struct file *f = vma->vm_file;
			char *buf = (char *)__get_free_page(GFP_NOWAIT);

			if (buf) {
				char *p;
				char prefix[64];

				p = file_path(f, buf, PAGE_SIZE);
				if (IS_ERR(p))
					p = "?";
				snprintf(prefix, sizeof(prefix), "<0x%lx> in ", pc_cur);
				pr_err("#%d %s%s[%lx-%lx]\n", i, prefix, p,
						vma->vm_start,
						vma->vm_end);
				free_page((unsigned long)buf);
			}
		}

		i++;
	}

	mmap_read_unlock(mm);
	if (task != current)
		mmput(mm);
}

/* Return a copied bound, never a VMA pointer after releasing mmap_lock. */
static unsigned long find_user_stack_end(struct task_struct *task, unsigned long sp)
{
	struct mm_struct *mm = task == current ? task->mm : get_task_mm(task);
	struct vm_area_struct *vma;
	unsigned long end = 0;

	if (!mm)
		return 0;
	if (mmap_read_trylock(mm)) {
		vma = find_vma(mm, sp);
		if (vma && vma->vm_start <= sp && sp < vma->vm_end)
			end = vma->vm_end;
		mmap_read_unlock(mm);
	}
	if (task != current)
		mmput(mm);
	return end;
}

static int get_stackframes(struct task_struct *task,
			    unsigned long *stackframes, int max_frames)
{
	struct pt_regs *user_regs;
	unsigned long userstack_start, userstack_end = 0;
	unsigned long fp_cur, fp_next, lr, tmp;
	int index = 0;

	if (max_frames < 2)
		return -EINVAL;
	user_regs = task_pt_regs(task);
	if (!user_mode(user_regs)) {
		return -EFAULT;
	}

	userstack_start = user_regs->user_regs.sp;
	userstack_end = find_user_stack_end(task, userstack_start);
	if (userstack_end < 2 * sizeof(unsigned long))
		return -EFAULT;

	tmp = user_regs->user_regs.pc;
	if (tmp < 4 || tmp & 0x3)
		return -EFAULT;
	stackframes[index++] = tmp - 4;

	tmp = user_regs->user_regs.regs[30];
	if (tmp < 4 || tmp & 0x3)
		return -EFAULT;
	stackframes[index++] = tmp - 4;

	fp_cur = user_regs->user_regs.regs[29];

	/* get frames */
	while (fp_cur >= userstack_start &&
	       fp_cur <= userstack_end - 2 * sizeof(unsigned long) &&
	       !(fp_cur & 0xf) && index < max_frames) {
		if (task == current) {
			int failed;

			pagefault_disable();
			failed = get_user(fp_next, (unsigned long __user *)fp_cur) ||
				get_user(lr, (unsigned long __user *)(fp_cur + 8));
			pagefault_enable();
			if (failed)
				break;
		} else {
			if (access_process_vm(task, fp_cur, &fp_next,
				sizeof(unsigned long), 0) != sizeof(unsigned long) ||
			    access_process_vm(task, fp_cur + 8, &lr,
				sizeof(unsigned long), 0) != sizeof(unsigned long))
				break;
		}
		if (lr < 4 || (lr & 0x3))
			break;
		stackframes[index++] = lr - 4;
		if (fp_next <= fp_cur)
			break;
		fp_cur = fp_next;
	}

	return index;
}

#define MAX_STACK_TRACE_DEPTH	64
static void show_user_backtrace(struct task_struct *task)
{
	unsigned long stackframes[MAX_STACK_TRACE_DEPTH];
	int frames_cnt = 0;

	memset(stackframes, 0, sizeof(stackframes));
	frames_cnt = get_stackframes(task, stackframes, MAX_STACK_TRACE_DEPTH);
	if (frames_cnt <= 0) {
		return;
	}

	dump_frames(stackframes, frames_cnt, task);
}

void dump_user_backtrace(struct task_struct *task)
{
	if (!task)
		task = current;

	if (task != current && (in_atomic() || irqs_disabled()))
		return;
	get_task_struct(task);
	if (!try_get_task_stack(task)) {
		put_task_struct(task);
		return;
	}
	pr_err("[%s, %d, %c, %llu] User backtrace:\n",
				task->comm,
				task_pid_nr(task),
				task_state_to_char(task),
				task->sched_info.last_arrival);

	show_user_backtrace(task);
	put_task_stack(task);
	put_task_struct(task);
}
EXPORT_SYMBOL_GPL(dump_user_backtrace);

static int mi_ubt_panic_notifier(struct notifier_block *this, unsigned long event, void *ptr)
{
	dump_user_backtrace(NULL);
	return NOTIFY_DONE;
}

static struct notifier_block mi_ubt_panic_blk = {
	.notifier_call  = mi_ubt_panic_notifier,
	.priority = INT_MAX,
};

static int __init user_backtrace_init(void)
{
	atomic_notifier_chain_register(&panic_notifier_list, &mi_ubt_panic_blk);
	pr_err("%s successd\n", __func__);

	return 0;
}
device_initcall(user_backtrace_init);

static void __exit user_backtrace_exit(void)
{
	atomic_notifier_chain_unregister(&panic_notifier_list, &mi_ubt_panic_blk);
	pr_err("%s\n", __func__);
}
module_exit(user_backtrace_exit);

MODULE_DESCRIPTION("Register user backtrace driver");
MODULE_LICENSE("GPL v2");
