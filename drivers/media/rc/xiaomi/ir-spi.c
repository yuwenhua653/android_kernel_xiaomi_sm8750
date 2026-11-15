/*
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 * Author: Andi Shyti <andi.shyti@samsung.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Xiaomi raw SPI IR interface, derived from MiCode annibale-w-oss.
 * See Documentation/xiaomi/annibale-port.md for source and porting changes.
 */
#include <linux/compat.h>
#include <linux/kref.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>
#include <uapi/linux/lirc.h>

#define IR_SPI_DEFAULT_FREQUENCY	1920000
#define IR_SPI_BITS_PER_WORD	32
#define IR_SPI_DATA_BUFFER	150000

struct ir_spi_data {
	struct kref ref;
	struct mutex lock;
	struct miscdevice miscdev;
	struct spi_device *spi;
	struct spi_transfer xfer;
	u8 *buffer;
	size_t buffer_size;
	unsigned int users;
};

static void ir_spi_release(struct kref *ref)
{
	struct ir_spi_data *idata = container_of(ref, struct ir_spi_data, ref);

	kfree(idata->buffer);
	kfree(idata);
}

static ssize_t ir_spi_write(struct file *file, const char __user *buffer,
			    size_t length, loff_t *offset)
{
	struct ir_spi_data *idata = file->private_data;
	struct spi_transfer xfer;
	u8 *new;
	int ret;

	mutex_lock(&idata->lock);
	if (!idata->spi) {
		ret = -ENODEV;
		goto unlock;
	}
	if (length > UINT_MAX ||
	    length > spi_max_transfer_size(idata->spi) ||
	    length > spi_max_message_size(idata->spi) ||
	    (idata->xfer.len && idata->xfer.len != length)) {
		ret = -EINVAL;
		goto unlock;
	}
	if (!length) {
		ret = 0;
		goto unlock;
	}
	if (length > idata->buffer_size) {
		new = krealloc(idata->buffer, length, GFP_KERNEL | GFP_DMA);
		if (!new) {
			ret = -ENOMEM;
			goto unlock;
		}
		idata->buffer = new;
		idata->buffer_size = length;
	}
	if (copy_from_user(idata->buffer, buffer, length)) {
		ret = -EFAULT;
		goto unlock;
	}
	/* A zero configured length means a single transfer sized by write(). */
	xfer = idata->xfer;
	xfer.len = length;
	xfer.tx_buf = idata->buffer;
	ret = spi_sync_transfer(idata->spi, &xfer, 1);
unlock:
	mutex_unlock(&idata->lock);
	return ret ? ret : length;
}

static int ir_spi_open(struct inode *inode, struct file *file)
{
	struct ir_spi_data *idata = container_of(file->private_data,
					       struct ir_spi_data, miscdev);
	int ret = 0;

	/* misc_open serializes this callback with misc_deregister. */
	mutex_lock(&idata->lock);
	if (!idata->spi) {
		ret = -ENODEV;
	} else if (idata->users == UINT_MAX) {
		ret = -EBUSY;
	} else {
		kref_get(&idata->ref);
		idata->users++;
		file->private_data = idata;
	}
	mutex_unlock(&idata->lock);
	return ret;
}

static int ir_spi_close(struct inode *inode, struct file *file)
{
	struct ir_spi_data *idata = file->private_data;

	mutex_lock(&idata->lock);
	if (!--idata->users) {
		idata->xfer.len = 0;
		idata->xfer.speed_hz = IR_SPI_DEFAULT_FREQUENCY;
	}
	mutex_unlock(&idata->lock);
	kref_put(&idata->ref, ir_spi_release);
	return 0;
}

static long ir_spi_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ir_spi_data *idata = file->private_data;
	u8 *new;
	u32 length;
	int ret = 0;

	if (cmd != LIRC_SET_SEND_MODE)
		return -EINVAL;
	if (get_user(length, (u32 __user *)arg))
		return -EFAULT;
	mutex_lock(&idata->lock);
	if (!idata->spi) {
		ret = -ENODEV;
		goto unlock;
	}
	if (length > spi_max_transfer_size(idata->spi) ||
	    length > spi_max_message_size(idata->spi)) {
		ret = -EINVAL;
		goto unlock;
	}
	if (length == idata->xfer.len)
		goto unlock;
	if (idata->users > 1) {
		ret = -EPERM;
		goto unlock;
	}
	if (!length) {
		kfree(idata->buffer);
		idata->buffer = NULL;
		idata->buffer_size = 0;
	} else if (length > idata->buffer_size) {
		new = krealloc(idata->buffer, length, GFP_KERNEL | GFP_DMA);
		if (!new) {
			ret = -ENOMEM;
			goto unlock;
		}
		idata->buffer = new;
		idata->buffer_size = length;
	}
	/* Preserve Xiaomi's byte-count interpretation of LIRC_SET_SEND_MODE. */
	idata->xfer.len = length;
unlock:
	mutex_unlock(&idata->lock);
	return ret;
}

static const struct file_operations ir_spi_fops = {
	.owner = THIS_MODULE,
	.write = ir_spi_write,
	.open = ir_spi_open,
	.release = ir_spi_close,
	.llseek = no_llseek,
	.unlocked_ioctl = ir_spi_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

static int ir_spi_probe(struct spi_device *spi)
{
	struct ir_spi_data *idata;
	int ret;

	idata = kzalloc(sizeof(*idata), GFP_KERNEL);
	if (!idata)
		return -ENOMEM;
	kref_init(&idata->ref);
	mutex_init(&idata->lock);
	idata->spi = spi;
	idata->xfer.bits_per_word = IR_SPI_BITS_PER_WORD;
	idata->xfer.speed_hz = IR_SPI_DEFAULT_FREQUENCY;
	idata->buffer = kmalloc(IR_SPI_DATA_BUFFER, GFP_KERNEL | GFP_DMA);
	if (!idata->buffer) {
		ret = -ENOMEM;
		goto put;
	}
	idata->buffer_size = IR_SPI_DATA_BUFFER;
	idata->miscdev.minor = MISC_DYNAMIC_MINOR;
	idata->miscdev.name = "ir_spi";
	idata->miscdev.nodename = "ir_spi";
	idata->miscdev.mode = 0666;
	idata->miscdev.fops = &ir_spi_fops;
	idata->miscdev.parent = &spi->dev;
	ret = misc_register(&idata->miscdev);
	if (ret)
		goto put;
	spi_set_drvdata(spi, idata);
	return 0;
put:
	kref_put(&idata->ref, ir_spi_release);
	return ret;
}

static void ir_spi_remove(struct spi_device *spi)
{
	struct ir_spi_data *idata = spi_get_drvdata(spi);

	misc_deregister(&idata->miscdev);
	mutex_lock(&idata->lock);
	idata->spi = NULL;
	mutex_unlock(&idata->lock);
	/* Existing descriptors retain the object and return ENODEV until closed. */
	kref_put(&idata->ref, ir_spi_release);
}

static const struct of_device_id ir_spi_of_match[] = {
	{ .compatible = "ir-spi" },
	{ }
};
MODULE_DEVICE_TABLE(of, ir_spi_of_match);

static struct spi_driver ir_spi_driver = {
	.probe = ir_spi_probe,
	.remove = ir_spi_remove,
	.driver = {
		.name = "ir-spi-xiaomi",
		.of_match_table = ir_spi_of_match,
	},
};
module_spi_driver(ir_spi_driver);

MODULE_AUTHOR("Andi Shyti <andi.shyti@samsung.com>");
MODULE_DESCRIPTION("Xiaomi raw SPI IR transmitter");
MODULE_LICENSE("GPL v2");
