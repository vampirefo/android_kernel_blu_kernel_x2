/*
 * Simple synchronous userspace interface to SPI devices
 *
 * Copyright (C) 2006 SWAPP
 *	Andrea Paterniani <a.paterniani@swapp-eng.it>
 * Copyright (C) 2007 David Brownell (simplification, cleanup)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>

#include "fp_drv.h"

static int fp_probe(struct platform_device *pdev);
static int fp_remove(struct platform_device *pdev);

static struct platform_driver fp_driver = {
    .probe = fp_probe,
    .remove = fp_remove,
    .driver = {
        .name = "fp_drv",
    },
};

struct platform_device fp_device = {
    .name   	= "fp_drv",
    .id        	= -1,
};

static char m_dev_name[50] = {0};
static DECLARE_WAIT_QUEUE_HEAD(waiter);

int full_fp_chip_name(const char *name)
{
    int i;

    if (name == NULL)
        return -1;

    memset(m_dev_name, 0, sizeof(m_dev_name));

    for (i = 0; *(name +i) != 0; i++)
        m_dev_name[i] = *(name +i);

    return 0;
}

static ssize_t info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%s", m_dev_name);
}
static DEVICE_ATTR(fp_drv_info, 0444, info_show, NULL);

static int fp_probe(struct platform_device *pdev)
{
    msleep(100);
    device_create_file(&pdev->dev, &dev_attr_fp_drv_info);
    return 0;
}

static int fp_remove(struct platform_device *pdev)
{
    device_remove_file(&pdev->dev, &dev_attr_fp_drv_info);
    return 0;
}

static int __init fp_drv_init(void)
{
    if ((platform_device_register(&fp_device) != 0) || (platform_driver_register(&fp_driver) != 0))
        return -1;

    return 0;
}

static void __exit fp_drv_exit(void)
{
    platform_driver_unregister(&fp_driver);
}

late_initcall(fp_drv_init);
module_exit(fp_drv_exit);
