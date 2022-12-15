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
#include "ec_color.h"


#define EC_COLOR_LOG(fmt, args...) printk("[ec_color] [%s: %d] "fmt,  __func__, __LINE__, ##args)
#define EC_COLOR_NAME    "ec_color"

#define debug	1

#if debug
#define FUNC_ENTER	printk("[ec_color] [%s : %d] enter\n", __func__, __LINE__);
#define FUNC_EXIT	printk("[ec_color] [%s : %d] exit normal\n", __func__, __LINE__);

#else
#define FUNC_ENTER
#define FUNC_EXIT

#endif


#ifdef NUBIA_EC_COLOR
#define EC_COLOR_PINCTRL_STATE_ACTIVE "ec_color_enable_default"
#define EC_COLOR_PINCTRL_STATE_SUSPEND "ec_color_enable_sleep"
#endif

static struct kobject *ec_color_kobj = NULL;
static struct ec_color *total_ec_color =NULL;
static int ec_color_update_count = 0;

#if 0

#define UPDATE_WAIT_TIME   			30 * 60 * 1000
#define WAIT_NEXT_CHARGE_DEFAUTL	24 * 60 * 60 *1000
#define CHARGE_TIME_DEFAULT			20 * 1000

#endif


#define UPDATE_WAIT_TIME   			10 * 1000
#define WAIT_NEXT_CHARGE_DEFAUTL	80 *1000
#define CHARGE_TIME_DEFAULT			30 * 1000


#define CHARGE_TIME_NUMBER 5
/*eg: 25℃ --> 30s*/
#if 0
static unsigned int charge_time[CHARGE_TIME_NUMBER][2] = {
	{25, 30 * 1000},
	{30, 20 * 1000},
	{40, 15 * 1000}
};

#endif

static unsigned int charge_time[CHARGE_TIME_NUMBER][2] = {
	{25, 60 * 1000},
	{30, 55 * 1000},
	{40, 50 * 1000},
	{45, 45 * 1000},
	{50, 40 * 1000}
};

#define UPDATE_TIME_NUMBER 5
/*eg: 30℃ --> 24h*/
#if 0
static unsigned int update_time[UPDATE_TIME_NUMBER][2] = {
	{25, 30*60*60*1000},
	{30, 24*60*60*1000},
	{40, 20*60*60*1000}
};
#endif

static unsigned int update_time[UPDATE_TIME_NUMBER][2] = {
	{25, 80*1000},
	{30, 70*1000},
	{40, 65*1000},
	{40, 60*1000},
	{40, 55*1000}
};

static int ec_color_get_temp(int *temp)
{

	int ret = 0;
	FUNC_ENTER
	if (total_ec_color->tzd) {
		ret = thermal_zone_get_temp(total_ec_color->tzd, temp);
		if (!ret)
		{
			*temp /= 1000;
			EC_COLOR_LOG("sucess get temp = %d\n", *temp);
		}
	}
	FUNC_EXIT
	return ret;
}

static unsigned int ec_color_get_charge_time(int temp)
{

    unsigned int charge_hold_time = 0;
	int i;
	FUNC_ENTER
	for(i = 0; (charge_time[i][0] < temp) && (i < CHARGE_TIME_NUMBER); i++)
	{
		if(charge_time[i+1][0] > temp)
			break;
	}
	if(i == UPDATE_TIME_NUMBER)
		charge_hold_time = charge_time[i - 1][1];
	else
		charge_hold_time = charge_time[i][1];

	EC_COLOR_LOG("ec color charge_hold_time = %d\n", charge_hold_time);
	FUNC_EXIT
    return charge_hold_time;
}

static unsigned int ec_color_get_wait_time(int temp)
{

    unsigned int wait_next_charge_time = 0;
	int i = 0;
	FUNC_ENTER
	for(i = 0; (update_time[i][0] < temp) && (i < UPDATE_TIME_NUMBER); i++)
	{
		if(update_time[i+1][0] > temp)
			break;
	}
	if(i == UPDATE_TIME_NUMBER)
		wait_next_charge_time = update_time[i - 1][1];
	else
		wait_next_charge_time = update_time[i - 1][1];

	EC_COLOR_LOG("wait next charge time = %d\n", wait_next_charge_time);
	FUNC_EXIT
    return wait_next_charge_time;
}

static void ec_color_set_wait_time_work(struct work_struct *work)
{

	int ret = 0, temp = 0;
	FUNC_ENTER
	//1.get the temperature,calc to wait next charge
	ret = ec_color_get_temp(&temp);
	if (ret) {
        EC_COLOR_LOG("Failed to get ec color temperature\n");
		total_ec_color->wait_next_charge_time = WAIT_NEXT_CHARGE_DEFAUTL;
    }else
    {
        total_ec_color->wait_temp = temp;
		total_ec_color->wait_next_charge_time = ec_color_get_wait_time(temp);
		EC_COLOR_LOG("wait time = %d\n", total_ec_color->wait_next_charge_time);
    }
	//2,start the hrtimer to wait next charge
	hrtimer_start(&total_ec_color->wait_next_charge_timer,
            ktime_set(total_ec_color->wait_next_charge_time/1000,
			(total_ec_color->wait_next_charge_time%1000)*1000000), HRTIMER_MODE_REL);

	total_ec_color->update_wait_charge_time = UPDATE_WAIT_TIME;
	hrtimer_start(&total_ec_color->update_wait_charge_timer,
            ktime_set(total_ec_color->update_wait_charge_time/1000,
			(total_ec_color->update_wait_charge_time%1000)*1000000), HRTIMER_MODE_REL);
	FUNC_EXIT
}

static enum hrtimer_restart ec_color_hrtimer_charge_handler(struct hrtimer *ec_color_charge_timer)
{
	FUNC_ENTER
	gpio_set_value(total_ec_color->enable_gpio, 0);
	gpio_set_value(total_ec_color->sel_gpio, 0);
	ec_color_update_count = 0;
	queue_work(total_ec_color->ec_color_workqueue, &total_ec_color->ec_color_set_wait_time);

	FUNC_EXIT
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart ec_color_wait_next_charge_handler(struct hrtimer *ec_color_wait_next_charge_timer)
{
	FUNC_ENTER

	hrtimer_cancel(&total_ec_color->update_wait_charge_timer);
	queue_work(total_ec_color->ec_color_workqueue, &total_ec_color->ec_color_charge_work);

	FUNC_EXIT
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart ec_color_update_next_charge_handler(struct hrtimer *ec_color_update_next_charge_timer)
{
	FUNC_ENTER
	EC_COLOR_LOG("total_ec_color->update_wait_charge_work\n");
	queue_work(total_ec_color->ec_color_workqueue, &total_ec_color->update_wait_charge_work);
	FUNC_EXIT
	return HRTIMER_NORESTART;
}

static void ec_color_update_next_charge_work(struct work_struct *work)
{

	int ret = 0, temp = 0;
	int  multi = 0;
	unsigned int time = 0;
	unsigned int had_pass_time = 0;
	FUNC_ENTER
	//get the temperature
	ec_color_update_count += 1;
	ret = ec_color_get_temp(&temp);
	if (ret) {
		total_ec_color->wait_temp = 0;
        EC_COLOR_LOG("Failed to get ec color temperature\n");
		goto timer_start;
    }
	/*
	*if temp no change add count
	*temp changed, update wait time
	*/
	if(total_ec_color->wait_temp == temp)
    {
		EC_COLOR_LOG("wait temp no change\n");
    } else {

		if (temp > total_ec_color->wait_temp)
			multi = 1;
		total_ec_color->wait_temp = temp;

		//TODO,do as linear change

		time = ec_color_get_wait_time(temp);
		if (multi)
			ec_color_update_count +=1;
		had_pass_time = ec_color_update_count * UPDATE_WAIT_TIME;
		if (time < had_pass_time)	//maybe charge again had happened
			return;

		total_ec_color->wait_next_charge_time = time - had_pass_time;
		//for new start， first cancel timer
		hrtimer_cancel(&total_ec_color->wait_next_charge_timer);

		//2,restart the hrtimer to wait next charge
		hrtimer_start(&total_ec_color->wait_next_charge_timer,
			ktime_set(total_ec_color->wait_next_charge_time/1000,
			(total_ec_color->wait_next_charge_time%1000)*1000000), HRTIMER_MODE_REL);

		if (total_ec_color->wait_next_charge_time < UPDATE_WAIT_TIME)
			return;
	}

timer_start:
		total_ec_color->update_wait_charge_time = UPDATE_WAIT_TIME;
		hrtimer_start(&total_ec_color->update_wait_charge_timer,
				ktime_set(total_ec_color->update_wait_charge_time/1000,
				(total_ec_color->update_wait_charge_time%1000)*1000000), HRTIMER_MODE_REL);
	FUNC_EXIT
}


static void ec_color_hrtimer_init(struct ec_color *ec_color)
{
	FUNC_ENTER
    /*for ec color charge*/
    hrtimer_init(&ec_color->ec_color_charge_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    ec_color->ec_color_charge_timer.function = ec_color_hrtimer_charge_handler;

	/*for ec color wait next charge*/
	hrtimer_init(&ec_color->wait_next_charge_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ec_color->wait_next_charge_timer.function = ec_color_wait_next_charge_handler;

	/*update ec color wait next charge*/
	hrtimer_init(&ec_color->update_wait_charge_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ec_color->update_wait_charge_timer.function = ec_color_update_next_charge_handler;
	FUNC_EXIT
}

static void ec_color_charge_work_routine(struct work_struct *work)
{

	int ret = 0, temp = 0;
	FUNC_ENTER
	//1.get the temperature,calc to hold the reset gpio pull up how long
	ret = ec_color_get_temp(&temp);
	if (ret) {
        EC_COLOR_LOG("Failed to get ec color temperature\n");
		total_ec_color->ec_color_charge_time = CHARGE_TIME_DEFAULT;
    }else
    {
		total_ec_color->ec_color_charge_time = ec_color_get_charge_time(temp);
		EC_COLOR_LOG("enable and sel hold time = %d\n", total_ec_color->ec_color_charge_time);
    }

	//2.set the gpio last status
	gpio_set_value(total_ec_color->sel_gpio, total_ec_color->last_sel_gpio_status);
	gpio_set_value(total_ec_color->enable_gpio, total_ec_color->last_enable_gpio_status);

	//3.start the hrtimer to pull down enable if the time up
	hrtimer_start(&total_ec_color->ec_color_charge_timer,
            ktime_set(total_ec_color->ec_color_charge_time/1000,
			(total_ec_color->ec_color_charge_time%1000)*1000000), HRTIMER_MODE_REL);
	FUNC_EXIT
}

#ifdef NUBIA_EC_COLOR
static int ec_color_pinctrl_init(struct device *dev,
		struct ec_color *ec_color_data)
{
	FUNC_ENTER
	ec_color_data->pinctrl_info.pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR_OR_NULL(ec_color_data->pinctrl_info.pinctrl)) {
		EC_COLOR_LOG("ec_color get pinctrl info error.\n");
		return -EINVAL;
	}

	ec_color_data->pinctrl_info.pin_active = pinctrl_lookup_state(
					ec_color_data->pinctrl_info.pinctrl,
					EC_COLOR_PINCTRL_STATE_ACTIVE);
	if (IS_ERR_OR_NULL(ec_color_data->pinctrl_info.pin_active)) {
		EC_COLOR_LOG("ec_color get pin_active info error.\n");
		return -EINVAL;
	}

	ec_color_data->pinctrl_info.pin_suspend = pinctrl_lookup_state(
					ec_color_data->pinctrl_info.pinctrl,
					EC_COLOR_PINCTRL_STATE_SUSPEND);
	if (IS_ERR_OR_NULL(ec_color_data->pinctrl_info.pin_suspend)) {
		EC_COLOR_LOG("ec_color get pin_suspend info error.\n");
		return -EINVAL;
	}
	FUNC_EXIT
	return 0;
}

static int ec_color_pinctrl_set_state(
		struct ec_color *ec_color_data, bool active)
{

	int ret = -1;
	FUNC_ENTER
	if (!ec_color_data->pinctrl_info.pinctrl ||
			!ec_color_data->pinctrl_info.pin_active ||
			!ec_color_data->pinctrl_info.pin_suspend) {
		EC_COLOR_LOG("pinctrl is invalid, skip.\n");
		return ret;
	}
	if (active) {
		ret = pinctrl_select_state(ec_color_data->pinctrl_info.pinctrl,
				ec_color_data->pinctrl_info.pin_active);
	} else {
		ret = pinctrl_select_state(ec_color_data->pinctrl_info.pinctrl,
				ec_color_data->pinctrl_info.pin_suspend);
	}
	EC_COLOR_LOG("set pinctrl to [%s], ret = %d.\n", active ? "active" : "suspend", ret);
	FUNC_EXIT
	return ret;
}
#endif


/*****************************************************
 * device tree
 *****************************************************/
static int ec_color_parse_dt(struct device *dev, struct ec_color *ec_color,
        struct device_node *np)
{
    int ret;
	FUNC_ENTER
    ec_color->enable_gpio = of_get_named_gpio(np, "qcom,ec_color_enable_gpio", 0);
    if (ec_color->enable_gpio < 0) {
        EC_COLOR_LOG("no enable gpio provided, will not HW enable device\n");
        return -1;
    } else {
        EC_COLOR_LOG("enable gpio:%d provided ok\n", ec_color->enable_gpio);
    }
    ec_color->sel_gpio = of_get_named_gpio(np, "qcom,ec_color_sel_gpio", 0);
    if (ec_color->sel_gpio < 0) {
        EC_COLOR_LOG("no sel gpio provided, will not suppport sel gpio\n");
        return -1;
    } else {
        EC_COLOR_LOG("sel gpio:%d provided ok\n", ec_color->sel_gpio);
    }

	ret = devm_gpio_request_one(dev, ec_color->enable_gpio, GPIOF_OUT_INIT_LOW, "ec_color_enable");
	if (ret) {
		EC_COLOR_LOG("enable_gpio request failed.\n");
		return ret;
	}
	ret = gpio_direction_output(ec_color->enable_gpio, 0);
    if (ret < 0) {
        EC_COLOR_LOG("set enable_gpio output error\n");
        return ret;
    }

	ret = devm_gpio_request_one(dev, ec_color->sel_gpio, GPIOF_OUT_INIT_LOW, "ec_color_sel");
	if (ret) {
		EC_COLOR_LOG("sel_gpio request failed.\n");
		return ret;
	}
	ret = gpio_direction_output(ec_color->sel_gpio, 0);
    if (ret < 0) {
        EC_COLOR_LOG("set sel_gpio output error\n");
        return ret;
    }
	FUNC_EXIT
    return 0;
}

static ssize_t ec_color_enable_store(struct kobject *kobj,
	    struct kobj_attribute *attr, const char *buf, size_t count)
{

    uint32_t enable_val = 0;
	FUNC_ENTER
	sscanf(buf, "%d", &enable_val);
	EC_COLOR_LOG("enable_val = %d\n", enable_val);

	//enable or not, cancel hrtimer all
	hrtimer_cancel(&total_ec_color->ec_color_charge_timer);
	hrtimer_cancel(&total_ec_color->wait_next_charge_timer);
	hrtimer_cancel(&total_ec_color->update_wait_charge_timer);

    if(1 == enable_val) {
		total_ec_color->last_enable_gpio_status = 1;
	    queue_work(total_ec_color->ec_color_workqueue,&total_ec_color->ec_color_charge_work);

    } else if (0 == enable_val) {
        gpio_set_value(total_ec_color->enable_gpio, 0);	//enable_gpio pull down
		gpio_set_value(total_ec_color->sel_gpio, 0);	//sel_gpio pull down
		total_ec_color->last_enable_gpio_status = 0;
		total_ec_color->last_sel_gpio_status = 0;
    }else {
		EC_COLOR_LOG("invalid enable_val,must be 1 or 0\n");
	}
	FUNC_EXIT
    return count;
}

static ssize_t ec_color_enable_show(struct kobject *kobj,
	   struct kobj_attribute *attr, char *buf)
{
	EC_COLOR_LOG("enable gpip = %d\n", gpio_get_value(total_ec_color->enable_gpio));
    return snprintf(buf, PAGE_SIZE, "enable gpio = %d\n", gpio_get_value(total_ec_color->enable_gpio));
}

//sel gpio
static ssize_t ec_color_sel_mode_store(struct kobject *kobj,
	    struct kobj_attribute *attr, const char *buf, size_t count)
{
    uint32_t sel_val = 0;
	FUNC_ENTER
    sscanf(buf, "%d", &sel_val);
	EC_COLOR_LOG("sel_val = %d\n", sel_val);
    if(1 == sel_val) {
        gpio_set_value(total_ec_color->sel_gpio, 1);	//pull up sel gpio
		total_ec_color->last_sel_gpio_status = 1;
    } else if (0 == sel_val) {
        gpio_set_value(total_ec_color->sel_gpio, 0);	//pull down sel gpio
		total_ec_color->last_sel_gpio_status = 0;
    } else {
		EC_COLOR_LOG("invalid sel_val, must be 1 or 0\n");
	}
	EC_COLOR_LOG("sel gpio = %d\n", gpio_get_value(total_ec_color->sel_gpio));
	FUNC_EXIT
    return count;
}

static ssize_t ec_color_sel_mode_show(struct kobject *kobj,
	   struct kobj_attribute *attr, char *buf)
{
	EC_COLOR_LOG("sel gpio cur = %d\n", gpio_get_value(total_ec_color->sel_gpio));
    return snprintf(buf, PAGE_SIZE, "sel_gpio cur = %d\n", gpio_get_value(total_ec_color->sel_gpio));
}
#if 0
static ssize_t ec_color_wait_time_change_store(struct kobject *kobj,
	   struct kobj_attribute *attr, const char *buf, size_t count)
{

	uint32_t sel_val = 0;
	FUNC_ENTER
	EC_COLOR_LOG("enter\n");
	sscanf(buf, "%d", &sel_val);
	EC_COLOR_LOG("add time = %d\n", sel_val);

	FUNC_EXIT
	return count;
}

static ssize_t ec_color_wait_time_change_show(struct kobject *kobj,
	  struct kobj_attribute *attr, char *buf)
{
	FUNC_ENTER
	//u64 time = 0;
	//time = hrtimer_next_event_without(&total_ec_color->ec_color_charge_timer);
	//EC_COLOR_LOG("hrtimer_next_event_without = %llu\n", time);
	FUNC_EXIT
	return 0;
}
#endif
static struct kobj_attribute ec_color_attrs[] = {
    __ATTR(enable,		0664, ec_color_enable_show,		       ec_color_enable_store),
    __ATTR(sel_mode,	0664, ec_color_sel_mode_show,	   	   ec_color_sel_mode_store),
};

static int ec_color_node_init(struct ec_color *ec_color)
{
    int ret = 0;
	int retval = 0;
	int attr_count = 0;
	FUNC_ENTER

	ec_color_kobj = kobject_create_and_add("ec_color", kernel_kobj);
	if (!ec_color_kobj) {
		EC_COLOR_LOG("failed to create and add kobject\n");
		return -ENOMEM;
	}

	/* Create attribute files associated with this kobject */
	for (attr_count = 0; attr_count < ARRAY_SIZE(ec_color_attrs); attr_count++) {
		retval = sysfs_create_file(ec_color_kobj, &ec_color_attrs[attr_count].attr);
		if (retval < 0) {
			EC_COLOR_LOG("failed to create sysfs attributes\n");
		}
	}
	FUNC_EXIT
    return ret;
}

static int ec_color_probe(struct platform_device *pdev)
{
	struct ec_color *ec_color;
	struct device_node *np = pdev->dev.of_node;
    int ret;
	FUNC_ENTER
	if (!pdev){
		EC_COLOR_LOG("pdev is null\n");
		return -EPROBE_DEFER;
	}

	ec_color = devm_kzalloc(&pdev->dev, sizeof(struct ec_color), GFP_KERNEL);
	if (ec_color == NULL)
		return -ENOMEM;
	total_ec_color = ec_color;
	ec_color->dev = pdev->dev;

#ifdef NUBIA_EC_COLOR
	ret = ec_color_pinctrl_init(&pdev->dev, ec_color);
	if (ret < 0) {
		EC_COLOR_LOG("ec_color pinctrl init failed.\n");
		goto pinctrl_init_failed;
	}

	ret = ec_color_pinctrl_set_state(ec_color, true);
	if (ret) {
		EC_COLOR_LOG("ec_color pinctrl set state failed.\n");
		goto pinctrl_set_failed;
	}

	EC_COLOR_LOG("ec_color pinctrl inited, set state to active sucessfully.\n");
#endif

	ret = ec_color_parse_dt(&pdev->dev, ec_color, np);
	if (ret) {
		EC_COLOR_LOG("failed to parse device tree node.\n");
		goto err_parse_dt;
	}

	ec_color->tzd = thermal_zone_get_zone_by_name("wlc-therm-usr");
	if (IS_ERR(ec_color->tzd))
		EC_COLOR_LOG("get zone wlc-therm-usr fail\n");

	dev_set_drvdata(&pdev->dev, ec_color);
	ec_color_node_init(ec_color);

	ec_color_hrtimer_init(ec_color);
	ec_color->ec_color_workqueue = create_singlethread_workqueue("ec_color_workqueue");

	INIT_WORK(&ec_color->ec_color_set_wait_time,ec_color_set_wait_time_work);
	INIT_WORK(&ec_color->ec_color_charge_work,ec_color_charge_work_routine);
	INIT_WORK(&ec_color->update_wait_charge_work,ec_color_update_next_charge_work);

	EC_COLOR_LOG("completed suceessfully\n");

	FUNC_EXIT
	return 0;

err_parse_dt:

#ifdef NUBIA_EC_COLOR
pinctrl_set_failed:
	devm_pinctrl_put(ec_color->pinctrl_info.pinctrl);
	ec_color->pinctrl_info.pinctrl = NULL;
pinctrl_init_failed:
	devm_kfree(&pdev->dev, ec_color);
		ec_color = NULL;
#endif

	return ret;
}

static int ec_color_remove(struct platform_device *pdev)
{

	struct ec_color *ec_color = dev_get_drvdata(&pdev->dev);
	int attr_count = 0;
	FUNC_ENTER

	for (attr_count = 0; attr_count < ARRAY_SIZE(ec_color_attrs); attr_count++)
		sysfs_remove_file(ec_color_kobj, &ec_color_attrs[attr_count].attr);

    if (gpio_is_valid(ec_color->enable_gpio))
        devm_gpio_free(&pdev->dev, ec_color->enable_gpio);
    if (gpio_is_valid(ec_color->sel_gpio))
        devm_gpio_free(&pdev->dev, ec_color->sel_gpio);
	destroy_workqueue(ec_color->ec_color_workqueue);
    devm_kfree(&pdev->dev, ec_color);
    ec_color = NULL;
	FUNC_EXIT
    return 0;
}

static const struct of_device_id ec_color_of_match[] = {
		{ .compatible = "nubia,ec_color" },
		{ }
};

static struct platform_driver ec_color_platform_driver = {
        .probe = ec_color_probe,
        .remove = ec_color_remove,
        .driver = {
                .name = EC_COLOR_NAME,
                .owner = THIS_MODULE,
                .of_match_table = of_match_ptr(ec_color_of_match),
        },
};

static int __init ec_color_platform_init(void)
{
	int ret = 0;
	FUNC_ENTER

	ret = platform_driver_register(&ec_color_platform_driver);

	if (ret) {
		EC_COLOR_LOG("failed to register ec_color_platform_driver into paltform.\n");
	}
	FUNC_EXIT
	return ret;
}

static void __exit ec_color_platform_exit(void)
{
	FUNC_ENTER
	platform_driver_unregister(&ec_color_platform_driver);
	FUNC_EXIT
}

module_init(ec_color_platform_init);
module_exit(ec_color_platform_exit);
MODULE_DESCRIPTION("NUBIA_EC_COLOR DRIVER");
MODULE_LICENSE("GPL v2");