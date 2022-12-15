#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/thermal.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include "ps5013.h"
#include <linux/msm_pcie.h>


#define PS5013_LOG(fmt, args...) printk("[ps5013] [%s: %d] "fmt,  __func__, __LINE__, ##args)
#define PS5013_NAME    "ps5013"

static int enable_gpio = -1;

static ssize_t pcie_enumerate_ssd_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned long val = 0;
	int ret = kstrtoul(buf, 0, &val);
	if (ret){
		printk("%s is not in hex or decimal form.\n", buf);
	}else{
		printk("%s(),val=%d.\n", __func__, val);
	}
	msm_pcie_enumerate(1);
	return count;
}
static struct kobj_attribute pcie_enumerate_ssd_attr = __ATTR(pcie_enumerate_ssd, 0200, NULL, pcie_enumerate_ssd_store);

static struct attribute *ssd_attributes[] = {
    &pcie_enumerate_ssd_attr.attr,
    NULL
};

static struct attribute_group ssd_attribute_group = {
        .attrs = ssd_attributes
};

static int ps5013_parse_dt(struct device *dev, struct device_node *np)
{
    int ret;
    enable_gpio = of_get_named_gpio(np, "qcom,ps5013_power_enable_gpio", 0);
    if (enable_gpio < 0) {
        PS5013_LOG("no enable gpio provided, will not HW enable device\n");
        return -1;
    } else {
        PS5013_LOG("enable gpio:%d provided ok\n", enable_gpio);
    }

	ret = devm_gpio_request_one(dev, enable_gpio, GPIOF_OUT_INIT_HIGH, "ps5013_enable");
	if (ret) {
		PS5013_LOG("enable_gpio request failed.\n");
		return ret;
	}
	ret = gpio_direction_output(enable_gpio, 1);
    if (ret < 0) {
        PS5013_LOG("set enable_gpio output error\n");
        return ret;
    }

    return 0;
}

static int ps5013_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int ret = ps5013_parse_dt(&pdev->dev, np);
	if (ret) {
		PS5013_LOG("failed to parse device tree node.\n");
	}
	return ret;
}

static int ps5013_remove(struct platform_device *pdev)
{
	gpio_set_value(enable_gpio, 0);
    return 0;
}

static const struct of_device_id ps5013_of_match[] = {
		{ .compatible = "nubia,ps5013" },
		{ }
};

static struct platform_driver ps5013_platform_driver = {
        .probe = ps5013_probe,
        .remove = ps5013_remove,
        .driver = {
                .name = PS5013_NAME,
                .owner = THIS_MODULE,
                .of_match_table = of_match_ptr(ps5013_of_match),
        },
};

static struct kobject *ssd_kobj = NULL;
static int __init ps5013_platform_init(void)
{
	int ret = 0;
	ret = platform_driver_register(&ps5013_platform_driver);
	if (ret) {
		PS5013_LOG("failed to register ps5013_platform_driver into paltform.\n");
	}
	
	ssd_kobj = kobject_create_and_add("nubia_ssd", NULL);
	if (!ssd_kobj){
		printk(KERN_ERR "%s: ssd kobj create error\n", __func__);
		return -ENOMEM;
	}
	ret = sysfs_create_group(ssd_kobj, &ssd_attribute_group);
	if(ret){
		printk(KERN_ERR "%s: failed to create ssd group attributes\n", __func__);
	}
	return ret;
}

static void __exit ps5013_platform_exit(void)
{
	platform_driver_unregister(&ps5013_platform_driver);
	if(ssd_kobj){
    	sysfs_remove_group(ssd_kobj, &ssd_attribute_group);
    	kobject_put(ssd_kobj);
    	ssd_kobj = NULL;
    }
}

module_init(ps5013_platform_init);
//late_initcall(ps5013_platform_init);
module_exit(ps5013_platform_exit);
MODULE_DESCRIPTION("NUBIA_PS5013 DRIVER");
MODULE_LICENSE("GPL v2");
