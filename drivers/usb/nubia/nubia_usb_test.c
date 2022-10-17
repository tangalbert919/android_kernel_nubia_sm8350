/*
 * nubia_dp_preference.c - nubia usb display enhancement and temperature setting
 *	      Linux kernel modules for mdss
 *
 * Copyright (c) 2015 nubia <nubia@nubia.com.cn>
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

/*
 * Supports NUBIA usb display enhancement and color temperature setting
 */

/*------------------------------ header file --------------------------------*/
#include "nubia_usb_test.h"
#include <linux/delay.h>
#include <linux/usb/typec.h>
#ifdef CONFIG_NUBIA_USB30_FEATURE
extern struct usb_device *nubia_usb_device;
extern struct usb_gadget *nubia_usb_gadget;
extern enum usb_device_speed nubia_global_otg_speed;
extern u32 nubia_global_cc_orientation;
#define ORIENTATION_CC1 0x01
#define ORIENTATION_CC2 0x02
#endif
#ifdef CONFIG_NUBIA_DOCK_FEATURE 
extern bool global_is_dock2_detect;
extern bool nubia_global_is_charge;
#endif
#ifdef CONFIG_FEATURE_NUBIA_BATTERY_1S_2S
extern ssize_t nubia_set_charge_bypass(const char *buf);
extern ssize_t nubia_get_charge_bypass(char *buf);
#endif

/*------------------------------- variables ---------------------------------*/
static struct kobject *enhance_kobj = NULL;

#ifdef CONFIG_NUBIA_DOCK_FEATURE 
static ssize_t dock2_detection_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	if(global_is_dock2_detect && nubia_global_is_charge)
		return snprintf(buf, PAGE_SIZE, "%d\n", 1);
	else
		return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}
#endif
#ifdef CONFIG_NUBIA_USB30_FEATURE
static ssize_t otg30_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	if(nubia_usb_device == NULL)
		return snprintf(buf, PAGE_SIZE, "%d\n", 0);
	if (nubia_global_otg_speed == USB_SPEED_SUPER)
		return snprintf(buf, PAGE_SIZE, "%d\n", 3);
	else if (nubia_global_otg_speed == USB_SPEED_HIGH)
		return snprintf(buf, PAGE_SIZE, "%d\n", 2);
	else
		return snprintf(buf, PAGE_SIZE, "%d\n", 0);

}

static ssize_t otg30_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
	//NUBIA_DISP_INFO("please don't try to set the otg30\n");

	if(nubia_usb_device == NULL)
		return size;

	return size;
}

static ssize_t usb30_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{		
	if(nubia_usb_gadget == NULL)
		return scnprintf(buf, PAGE_SIZE, "none\n");
	if (nubia_usb_gadget->speed == USB_SPEED_SUPER_PLUS)
		return scnprintf(buf, PAGE_SIZE, "USB31\n");
	else if (nubia_usb_gadget->speed == USB_SPEED_SUPER)
		return scnprintf(buf, PAGE_SIZE, "USB30\n");
	else if (nubia_usb_gadget->speed == USB_SPEED_HIGH)
		return scnprintf(buf, PAGE_SIZE, "USB20\n");
	else
		return scnprintf(buf, PAGE_SIZE, "none\n");
}

static ssize_t usb30_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
	//NUBIA_DISP_INFO("please don't try to set the usb30\n");

	if(nubia_usb_gadget == NULL)
		return size;

	return size;
}
static ssize_t cc_orientation_show(struct kobject *kobj,
                struct kobj_attribute *attr, char *buf)
{
        if (nubia_global_cc_orientation == ORIENTATION_CC1)
                return scnprintf(buf, PAGE_SIZE, "CC1\n");
        if (nubia_global_cc_orientation == ORIENTATION_CC2)
                return scnprintf(buf, PAGE_SIZE, "CC2\n");
        return scnprintf(buf, PAGE_SIZE, "none\n");
}
#endif
#if CONFIG_FEATURE_NUBIA_BATTERY_1S_2S
static ssize_t nubia_charger_bypass_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	nubia_set_charge_bypass(buf);

	return count;
}

static ssize_t nubia_charger_bypass_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    size_t val;
	val = nubia_get_charge_bypass(buf);
	return val;
}

#endif
static struct kobj_attribute usb_test_attrs[] = {
#ifdef CONFIG_NUBIA_USB30_FEATURE
	__ATTR(otg30,        0664, otg30_show,       otg30_store),
	__ATTR(usb30,        0664, usb30_show,       usb30_store),
	__ATTR(cc_orientation,         0664,    cc_orientation_show,    NULL),
#endif
#ifdef CONFIG_NUBIA_DOCK_FEATURE 
	__ATTR(dock2_detection,        0664, 	dock2_detection_show,	NULL),
#endif
};
#if CONFIG_FEATURE_NUBIA_BATTERY_1S_2S
static struct kobj_attribute nubia_battery_class_attrs[] = {
	__ATTR(charger_bypass,    0664, 	nubia_charger_bypass_show,	nubia_charger_bypass_store),
};
#endif
static int __init nubia_usb_test_init(void)
{
	int retval = 0;
	int attr_count = 0;
#if CONFIG_FEATURE_NUBIA_BATTERY_1S_2S
	struct kobject * charge_kobj;
#endif
	//NUBIA_DISP_INFO("start\n");

	enhance_kobj = kobject_create_and_add("usb_enhance", kernel_kobj);

	if (!enhance_kobj) {
		//NUBIA_DISP_ERROR("failed to create and add kobject\n");
		return -ENOMEM;
	}

	/* Create attribute files associated with this kobject */
	for (attr_count = 0; attr_count < ARRAY_SIZE(usb_test_attrs); attr_count++) {
		retval = sysfs_create_file(enhance_kobj, &usb_test_attrs[attr_count].attr);
		if (retval < 0) {
			//NUBIA_DISP_ERROR("failed to create sysfs attributes\n");
			goto err_sys_creat;
		}
	}
#if CONFIG_FEATURE_NUBIA_BATTERY_1S_2S
	charge_kobj = kobject_create_and_add("nubia_charge", kernel_kobj);

	if (!charge_kobj) {
		return -ENOMEM;
	}

	for (attr_count = 0; attr_count < ARRAY_SIZE(nubia_battery_class_attrs); attr_count++) {
		retval = sysfs_create_file(charge_kobj, &nubia_battery_class_attrs[attr_count].attr);
		if (retval < 0) {
			goto err_sys_creat;
		}
	}
#endif
	//NUBIA_DISP_INFO("success\n");

	return retval;

err_sys_creat:
//#ifdef CONFIG_NUBIA_SWITCH_LCD
	//cancel_delayed_work_sync(&nubia_disp_val.lcd_states_work);
//#endif
	for (--attr_count; attr_count >= 0; attr_count--)
		sysfs_remove_file(enhance_kobj, &usb_test_attrs[attr_count].attr);

	kobject_put(enhance_kobj);
	return retval;
}

static void __exit nubia_usb_test_exit(void)
{
	int attr_count = 0;

	for (attr_count = 0; attr_count < ARRAY_SIZE(usb_test_attrs); attr_count++)
		sysfs_remove_file(enhance_kobj, &usb_test_attrs[attr_count].attr);

	kobject_put(enhance_kobj);

}

MODULE_AUTHOR("NUBIA USB Driver Team Software");
MODULE_DESCRIPTION("NUBIA USB3.0 Saturation and Temperature Setting");
MODULE_LICENSE("GPL");
module_init(nubia_usb_test_init);
module_exit(nubia_usb_test_exit);
