// SPDX-License-Identifier: GPL-2.0-only
/* Xiaomi MiSight event transport, hardened for Annibale and optional readers. */
#define pr_fmt(fmt) "miev: " fmt
#include "midevice.h"
#include <linux/compat.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/poll.h>
#include <linux/workqueue.h>

#define FIFO_SIZE (64 * 1024)
#define WORK_MAX_CNT 10
struct miev_work_struct {
	bool in_use;
	bool in_work;
	int size;
	int offset;
	char *msg;
	struct work_struct wk;
};
static struct miev_device *miev_dev;
static struct workqueue_struct *miev_workqueue;
static struct miev_work_struct miev_works[WORK_MAX_CNT];
static DEFINE_RAW_SPINLOCK(miev_spinlock);
static bool bInited;

/* Called only from a worker or a userspace operation, never in atomic context. */
static int write_buf_inner(const char *buf, int size)
{
	int old_size, ret;

	if (!buf || size <= 0 || size > BUF_MAX_SIZE)
		return -EINVAL;
	mutex_lock(&miev_dev->lock);
	/* AOSP need not run a MiSight reader. Keep memory bounded and evict whole
	 * oldest records rather than waiting indefinitely for a consumer. */
	while (kfifo_avail(&miev_dev->fifo) < size + sizeof(int)) {
		if (kfifo_out(&miev_dev->fifo, &old_size, sizeof(int)) != sizeof(int) ||
		    old_size <= 0 || old_size > BUF_MAX_SIZE ||
		    kfifo_out(&miev_dev->fifo, miev_dev->cbuf, old_size) != old_size)
			kfifo_reset(&miev_dev->fifo);
	}
	if (kfifo_in(&miev_dev->fifo, &size, sizeof(int)) != sizeof(int) ||
	    kfifo_in(&miev_dev->fifo, buf, size) != size) {
		kfifo_reset(&miev_dev->fifo);
		ret = -EIO;
	} else {
		ret = size;
	}
	mutex_unlock(&miev_dev->lock);
	if (ret > 0)
		wake_up_interruptible(&miev_dev->wait_queue);
	return ret;
}

/* Match pointer identity before dereferencing; no container_of on foreign input. */
static struct miev_work_struct *find_work(char **buf)
{
	int i;

	for (i = 0; i < WORK_MAX_CNT; i++)
		if (buf == &miev_works[i].msg)
			return &miev_works[i];
	return NULL;
}

bool miev_event_writable(const void *event)
{
	unsigned long flags;
	bool valid = false;
	int i;

	raw_spin_lock_irqsave(&miev_spinlock, flags);
	for (i = 0; i < WORK_MAX_CNT; i++) {
		if (event && event == miev_works[i].msg) {
			valid = smp_load_acquire(&bInited) && miev_works[i].in_use && !miev_works[i].in_work;
			break;
		}
	}
	raw_spin_unlock_irqrestore(&miev_spinlock, flags);
	return valid;
}

char **miev_get_work_msg(void)
{
	struct miev_work_struct *wk = NULL;
	unsigned long flags;
	int i;

	raw_spin_lock_irqsave(&miev_spinlock, flags);
	if (!smp_load_acquire(&bInited))
		goto out;
	for (i = 0; i < WORK_MAX_CNT; i++) {
		if (!miev_works[i].in_use && !miev_works[i].in_work) {
			if (!try_module_get(THIS_MODULE))
				break;
			wk = &miev_works[i];
			wk->in_use = true;
			break;
		}
	}
 out:
	raw_spin_unlock_irqrestore(&miev_spinlock, flags);
	if (!wk)
		return NULL;
	memset(wk->msg, 0, BUF_MAX_SIZE);
	return &wk->msg;
}

void miev_release_work_msg(char **buf)
{
	struct miev_work_struct *wk;
	unsigned long flags;
	bool released = false;

	raw_spin_lock_irqsave(&miev_spinlock, flags);
	wk = find_work(buf);
	if (wk && wk->in_use) {
		wk->in_use = false;
		released = true;
	}
	raw_spin_unlock_irqrestore(&miev_spinlock, flags);
	if (released)
		module_put(THIS_MODULE);
}

int write_kbuf(char **buf, int offset, int size)
{
	struct miev_work_struct *wk;
	unsigned long flags;
	int ret = 0;

	if (offset < 0 || offset >= BUF_MAX_SIZE || size <= 0 ||
	    size > BUF_MAX_SIZE - offset)
		return -EINVAL;
	raw_spin_lock_irqsave(&miev_spinlock, flags);
	wk = find_work(buf);
	if (!smp_load_acquire(&bInited) || !wk || !wk->in_use) {
		ret = -EINVAL;
	} else if (wk->in_work) {
		ret = -EBUSY;
	} else {
		wk->offset = offset;
		wk->size = size;
		wk->in_work = true;
		/* Always defer: a process-context caller can still hold a spinlock. */
		if (!queue_work(miev_workqueue, &wk->wk)) {
			wk->in_work = false;
			ret = -EBUSY;
		}
	}
	raw_spin_unlock_irqrestore(&miev_spinlock, flags);
	return ret;
}

static void do_miev_work(struct work_struct *work)
{
	struct miev_work_struct *wk = container_of(work, struct miev_work_struct, wk);
	unsigned long flags;

	write_buf_inner(wk->msg + wk->offset, wk->size);
	raw_spin_lock_irqsave(&miev_spinlock, flags);
	wk->in_work = false;
	raw_spin_unlock_irqrestore(&miev_spinlock, flags);
}

static int miev_open(struct inode *inode, struct file *filp)
{
	return smp_load_acquire(&bInited) ? nonseekable_open(inode, filp) : -ENODEV;
}

static long miev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	if (_IOC_TYPE(cmd) != MIEV_IOC_MAGIC || _IOC_NR(cmd) > MIEV_IOC_MAXNR)
		return -EINVAL;
	if (!access_ok((void __user *)arg, _IOC_SIZE(cmd)))
		return -EFAULT;
	/* The published ABI defines these three commands as reserved no-ops. */
	switch (cmd) {
	case MIEV_IOC_WRITE:
	case MIEV_IOC_READ:
	case MIEV_IOC_NONE:
		return 0;
	default:
		return -ENOTTY;
	}
}

static __poll_t miev_poll(struct file *filp, poll_table *wait)
{
	__poll_t mask = 0;

	poll_wait(filp, &miev_dev->wait_queue, wait);
	mutex_lock(&miev_dev->lock);
	if (!kfifo_is_empty(&miev_dev->fifo))
		mask = EPOLLIN | EPOLLRDNORM;
	mutex_unlock(&miev_dev->lock);
	return mask;
}

static ssize_t miev_read(struct file *filp, char __user *ubuf, size_t size,
			 loff_t *offset)
{
	int length, total, ret = 0;

	if (!size)
		return 0;
	mutex_lock(&miev_dev->lock);
	/* Retain the original empty-read return value; poll supplies readiness. */
	if (kfifo_is_empty(&miev_dev->fifo))
		goto out;
	if (kfifo_out_peek(&miev_dev->fifo, &length, sizeof(int)) != sizeof(int) ||
	    length <= 0 || length > BUF_MAX_SIZE) {
		ret = -EIO;
		goto corrupt;
	}
	total = length + sizeof(int);
	if (kfifo_len(&miev_dev->fifo) < total) {
		ret = -EIO;
		goto corrupt;
	}
	if (size < length) {
		ret = -EMSGSIZE;
		goto out;
	}
	if (kfifo_out_peek(&miev_dev->fifo, miev_dev->cbuf, total) != total) {
		ret = -EIO;
		goto corrupt;
	}
	if (copy_to_user(ubuf, miev_dev->cbuf + sizeof(int), length)) {
		ret = -EFAULT;
		goto out;
	}
	ret = kfifo_out(&miev_dev->fifo, miev_dev->cbuf, total) == total ? length : -EIO;
	goto out;
 corrupt:
	kfifo_reset(&miev_dev->fifo);
 out:
	mutex_unlock(&miev_dev->lock);
	return ret;
}

static ssize_t miev_write(struct file *filp, const char __user *ubuf,
			  size_t size, loff_t *offset)
{
	char *buf;
	int ret;

	if (!size)
		return 0;
	if (size > BUF_MAX_SIZE)
		return -EMSGSIZE;
	buf = memdup_user(ubuf, size);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	ret = write_buf_inner(buf, size);
	kfree(buf);
	return ret;
}

static const struct file_operations miev_fops = {
	.owner = THIS_MODULE,
	.open = miev_open,
	.read = miev_read,
	.write = miev_write,
	.unlocked_ioctl = miev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_ptr_ioctl,
#endif
	.poll = miev_poll,
	.llseek = no_llseek,
};

static int __init miev_init_module(void)
{
	struct device *device;
	int ret, i;

	miev_dev = kzalloc(sizeof(*miev_dev), GFP_KERNEL);
	if (!miev_dev)
		return -ENOMEM;
	mutex_init(&miev_dev->lock);
	init_waitqueue_head(&miev_dev->wait_queue);
	ret = kfifo_alloc(&miev_dev->fifo, FIFO_SIZE, GFP_KERNEL);
	if (ret)
		goto free_device;
	miev_workqueue = alloc_ordered_workqueue("miev_workqueue", WQ_MEM_RECLAIM);
	if (!miev_workqueue) {
		ret = -ENOMEM;
		goto free_fifo;
	}
	for (i = 0; i < WORK_MAX_CNT; i++) {
		miev_works[i].msg = kzalloc(BUF_MAX_SIZE, GFP_KERNEL);
		if (!miev_works[i].msg) {
			ret = -ENOMEM;
			goto free_pool;
		}
		INIT_WORK(&miev_works[i].wk, do_miev_work);
	}
	miev_dev->dev_no = MKDEV(DEV_MAJOR, DEV_MINOR);
	ret = register_chrdev_region(miev_dev->dev_no, 1, DEV_NAME);
	if (ret)
		ret = alloc_chrdev_region(&miev_dev->dev_no, 0, 1, DEV_NAME);
	if (ret)
		goto free_pool;
	miev_dev->my_class = class_create(DEV_NAME);
	if (IS_ERR(miev_dev->my_class)) {
		ret = PTR_ERR(miev_dev->my_class);
		goto unregister_device;
	}
	cdev_init(&miev_dev->chrdev, &miev_fops);
	miev_dev->chrdev.owner = THIS_MODULE;
	ret = cdev_add(&miev_dev->chrdev, miev_dev->dev_no, 1);
	if (ret)
		goto destroy_class;
	device = device_create(miev_dev->my_class, NULL, miev_dev->dev_no, NULL, DEV_NAME);
	if (IS_ERR(device)) {
		ret = PTR_ERR(device);
		goto delete_cdev;
	}
	/* No userspace open or exported allocation succeeds before all setup. */
	smp_store_release(&bInited, true);
	return 0;
 delete_cdev:
	cdev_del(&miev_dev->chrdev);
 destroy_class:
	class_destroy(miev_dev->my_class);
 unregister_device:
	unregister_chrdev_region(miev_dev->dev_no, 1);
 free_pool:
	for (i = 0; i < WORK_MAX_CNT; i++) {
		kfree(miev_works[i].msg);
		miev_works[i].msg = NULL;
	}
	destroy_workqueue(miev_workqueue);
	miev_workqueue = NULL;
 free_fifo:
	kfifo_free(&miev_dev->fifo);
 free_device:
	kfree(miev_dev);
	miev_dev = NULL;
	return ret;
}

static void __exit miev_exit_module(void)
{
	unsigned long flags;
	int i;

	raw_spin_lock_irqsave(&miev_spinlock, flags);
	bInited = false;
	raw_spin_unlock_irqrestore(&miev_spinlock, flags);
	device_destroy(miev_dev->my_class, miev_dev->dev_no);
	cdev_del(&miev_dev->chrdev);
	class_destroy(miev_dev->my_class);
	unregister_chrdev_region(miev_dev->dev_no, 1);
	/* Pending workers still own pool buffers and use the FIFO/device. */
	destroy_workqueue(miev_workqueue);
	for (i = 0; i < WORK_MAX_CNT; i++)
		kfree(miev_works[i].msg);
	kfifo_free(&miev_dev->fifo);
	kfree(miev_dev);
}
module_init(miev_init_module);
module_exit(miev_exit_module);
MODULE_AUTHOR("huangqibo <huangqibo@xiaomi.com>");
MODULE_DESCRIPTION("MiSight bounded event transport");
MODULE_LICENSE("GPL");
