/*
 *
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/switch.h>

#define	LID_DEV_NAME	"hall_sensor"
#define HALL_INPUT	"/dev/input/hall_dev"

struct hall_data {
    int gpio;
    int irq;
    int active_low;
    bool wakeup;
    struct input_dev *hall_dev;
    struct regulator *vddio;
    u32 min_uv;
    u32 max_uv;
};

#ifdef CONFIG_SWITCH
struct switch_dev hall_sensor_data = {
    .name = "sensor_hall",
};
#endif

static int g_hall_state = 0;
static  void sendevent(int status,struct input_dev *dev_input)
{
    if (status == 1) {
#ifdef CONFIG_SWITCH
        switch_set_state(&hall_sensor_data, 1);
#endif
        input_report_key(dev_input, KEY_HALLCLOSE, 1);
        input_report_key(dev_input, KEY_HALLCLOSE, 0);
        pr_info("hall_interrupt_handler near :1 \n");
        g_hall_state = 1;
    } else {
#ifdef CONFIG_SWITCH
        switch_set_state(&hall_sensor_data, 0);
#endif
        pr_info("hall_interrupt_handler far: 0 \n");
        input_report_key(dev_input, KEY_HALLOPEN, 1);
        input_report_key(dev_input, KEY_HALLOPEN, 0);
        g_hall_state = 0;
    }
    input_sync(dev_input);
}

static irqreturn_t hall_interrupt_handler(int irq, void *dev)
{
#if 1
    int value;
    struct hall_data *data = dev;

    value = gpio_get_value_cansleep(data->gpio);
    sendevent(value,data->hall_dev);
    return IRQ_HANDLED;
#else
    int value;
    struct hall_data *data = dev;

    value = (gpio_get_value_cansleep(data->gpio) ? 1 : 0) ^
            data->active_low;
    if (value) {
        input_report_switch(data->hall_dev, SW_LID, 0);
        dev_dbg(&data->hall_dev->dev, "far\n");
    } else {
        input_report_switch(data->hall_dev, SW_LID, 1);
        dev_dbg(&data->hall_dev->dev, "near\n");
    }
    input_sync(data->hall_dev);
    return IRQ_HANDLED;
#endif
}

static int hall_input_init(struct platform_device *pdev,
                           struct hall_data *data)
{
    int err = -1;
    data->hall_dev = devm_input_allocate_device(&pdev->dev);
    if (!data->hall_dev) {
        dev_err(&data->hall_dev->dev,
                "input device allocation failed\n");
        return -EINVAL;
    }
    data->hall_dev->name = LID_DEV_NAME;
    data->hall_dev->phys = HALL_INPUT;

    __set_bit(KEY_HALLOPEN, data->hall_dev->keybit);
    __set_bit(KEY_HALLCLOSE, data->hall_dev->keybit);
    __set_bit(EV_KEY, data->hall_dev->evbit);
    __set_bit(EV_SYN, data->hall_dev->evbit);
    err = input_register_device(data->hall_dev);

    if (err < 0) {
        dev_err(&data->hall_dev->dev,
                "unable to register input device %s\n",
                LID_DEV_NAME);
        return err;
    }
    return 0;
}

static int hall_config_regulator(struct platform_device *dev, bool on)
{
    struct hall_data *data = dev_get_drvdata(&dev->dev);
    int rc = 0;

    if (on) {
        data->vddio = devm_regulator_get(&dev->dev, "vddio");
        if (IS_ERR(data->vddio)) {
            rc = PTR_ERR(data->vddio);
            dev_err(&dev->dev, "Regulator vddio get failed rc=%d\n",
                    rc);
            data->vddio = NULL;
            return rc;
        }

        if (regulator_count_voltages(data->vddio) > 0) {
            rc = regulator_set_voltage(
                     data->vddio,
                     data->min_uv,
                     data->max_uv);
            if (rc) {
                dev_err(&dev->dev, "Regulator vddio Set voltage failed rc=%d\n",
                        rc);
                goto deinit_vregs;
            }
        }
        return rc;
    } else {
        goto deinit_vregs;
    }

deinit_vregs:
    if (regulator_count_voltages(data->vddio) > 0)
        regulator_set_voltage(data->vddio, 0, data->max_uv);

    return rc;
}

static int hall_set_regulator(struct platform_device *dev, bool on)
{
    struct hall_data *data = dev_get_drvdata(&dev->dev);
    int rc = 0;

    if (on) {
        if (!IS_ERR_OR_NULL(data->vddio)) {
            rc = regulator_enable(data->vddio);
            if (rc) {
                dev_err(&dev->dev, "Enable regulator vddio failed rc=%d\n",
                        rc);
                goto disable_regulator;
            }
        }
        return rc;
    } else {
        if (!IS_ERR_OR_NULL(data->vddio)) {
            rc = regulator_disable(data->vddio);
            if (rc)
                dev_err(&dev->dev, "Disable regulator vddio failed rc=%d\n",
                        rc);
        }
        return 0;
    }

disable_regulator:
    if (!IS_ERR_OR_NULL(data->vddio))
        regulator_disable(data->vddio);
    return rc;
}

#ifdef CONFIG_OF
static int hall_parse_dt(struct device *dev, struct hall_data *data)
{
    unsigned int tmp;
    u32 tempval;
    int rc;
    struct device_node *np = dev->of_node;

    data->gpio = of_get_named_gpio_flags(dev->of_node,
                                         "linux,gpio-int", 0, &tmp);
    if (!gpio_is_valid(data->gpio)) {
        dev_err(dev, "hall gpio is not valid\n");
        return -EINVAL;
    }
    data->active_low = tmp & OF_GPIO_ACTIVE_LOW ? 0 : 1;

    data->wakeup = of_property_read_bool(np, "linux,wakeup");

    rc = of_property_read_u32(np, "linux,max-uv", &tempval);
    if (rc) {
        dev_err(dev, "unable to read max-uv\n");
        return -EINVAL;
    }
    data->max_uv = tempval;

    rc = of_property_read_u32(np, "linux,min-uv", &tempval);
    if (rc) {
        dev_err(dev, "unable to read min-uv\n");
        return -EINVAL;
    }
    data->min_uv = tempval;

    return 0;
}
#else
static int hall_parse_dt(struct device *dev, struct hall_data *data)
{
    return -EINVAL;
}
#endif


static ssize_t hall_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d", g_hall_state);
}

static DEVICE_ATTR(hall_state, 0444, hall_info_show, NULL);


static ssize_t hall_driver_info_show(struct device_driver *ddri, char *buf)
{
    return sprintf(buf, "%d\n", g_hall_state);
}
static DRIVER_ATTR(hall_state,     S_IWUSR | S_IRUGO, hall_driver_info_show, NULL);

static struct platform_driver hall_driver;


static int hall_driver_probe(struct platform_device *dev)
{
    struct hall_data *data;
    int err = 0;
    int irq_flags;
    int ret = 0;

    dev_dbg(&dev->dev, "hall_driver probe\n");
    data = devm_kzalloc(&dev->dev, sizeof(struct hall_data), GFP_KERNEL);
    if (data == NULL) {
        err = -ENOMEM;
        dev_err(&dev->dev,
                "failed to allocate memory %d\n", err);
        goto exit;
    }
    dev_set_drvdata(&dev->dev, data);
    if (dev->dev.of_node) {
        err = hall_parse_dt(&dev->dev, data);
        if (err < 0) {
            dev_err(&dev->dev, "Failed to parse device tree\n");
            goto exit;
        }
    } else if (dev->dev.platform_data != NULL) {
        memcpy(data, dev->dev.platform_data, sizeof(*data));
    } else {
        dev_err(&dev->dev, "No valid platform data.\n");
        err = -ENODEV;
        goto exit;
    }


#ifdef CONFIG_SWITCH
    ret = switch_dev_register(&hall_sensor_data);
    if (ret < 0) {
        dev_err(&dev->dev, "not able to register hall_sensor_data\n");
    }
#endif

    err = hall_input_init(dev, data);
    if (err < 0) {
        dev_err(&dev->dev, "input init failed\n");
        goto exit;
    }

    if (!gpio_is_valid(data->gpio)) {
        dev_err(&dev->dev, "gpio is not valid\n");
        err = -EINVAL;
        goto exit;
    }

    irq_flags = IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
                | IRQF_ONESHOT;
    err = gpio_request_one(data->gpio, GPIOF_DIR_IN, "hall_sensor_irq");
    if (err) {
        dev_err(&dev->dev, "unable to request gpio %d\n", data->gpio);
        goto exit;
    }

    data->irq = gpio_to_irq(data->gpio);
    err = devm_request_threaded_irq(&dev->dev, data->irq, NULL,
                                    hall_interrupt_handler,
                                    irq_flags, "hall_sensor", data);
    if (err < 0) {
        dev_err(&dev->dev, "request irq failed : %d\n", data->irq);
        goto free_gpio;
    } else {
        int value;
        enable_irq_wake(data->irq);
        value = gpio_get_value_cansleep(data->gpio);
        sendevent(value, data->hall_dev);
    }

    device_init_wakeup(&dev->dev, data->wakeup);
    enable_irq_wake(data->irq);

    err = hall_config_regulator(dev, true);
    if (err < 0) {
        dev_err(&dev->dev, "Configure power failed: %d\n", err);
        goto free_irq;
    }

    err = hall_set_regulator(dev, true);
    if (err < 0) {
        dev_err(&dev->dev, "power on failed: %d\n", err);
        goto err_regulator_init;
    }


    device_create_file(&dev->dev, &dev_attr_hall_state);


    err = driver_create_file(&hall_driver.driver, &driver_attr_hall_state);
    if (err < 0) {
        dev_err(&dev->dev, "driver_create_file  failed: %d\n", err);
    }

    return 0;

err_regulator_init:
    hall_config_regulator(dev, false);
free_irq:
    disable_irq_wake(data->irq);
    device_init_wakeup(&dev->dev, 0);
free_gpio:
    gpio_free(data->gpio);
exit:
    return err;
}

static int hall_driver_remove(struct platform_device *dev)
{
    struct hall_data *data = dev_get_drvdata(&dev->dev);

    disable_irq_wake(data->irq);
    device_init_wakeup(&dev->dev, 0);
    if (data->gpio)
        gpio_free(data->gpio);
    hall_set_regulator(dev, false);
    hall_config_regulator(dev, false);


#ifdef CONFIG_SWITCH
    switch_dev_unregister(&hall_sensor_data);
#endif


    device_remove_file(&dev->dev, &dev_attr_hall_state);
    driver_remove_file(&hall_driver.driver, &driver_attr_hall_state);
    return 0;
}

static struct platform_device_id hall_id[] = {
    {LID_DEV_NAME, 0 },
    { },
};


#ifdef CONFIG_OF
static struct of_device_id hall_match_table[] = {
    {.compatible = "hall-switch", },
    { },
};
#endif

static struct platform_driver hall_driver = {
    .driver = {
        .name = "hall",
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(hall_match_table),
    },
    .probe = hall_driver_probe,
    .remove = hall_driver_remove,
    .id_table = hall_id,
};

static int __init hall_init(void)
{

    return platform_driver_register(&hall_driver);
}

static void __exit hall_exit(void)
{
    platform_driver_unregister(&hall_driver);
}

module_init(hall_init);
module_exit(hall_exit);
MODULE_DESCRIPTION("Hall sensor driver");
MODULE_LICENSE("GPL v2");
