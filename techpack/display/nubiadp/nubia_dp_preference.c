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
#include "nubia_dp_preference.h"
#include <linux/delay.h>
#include "../msm/dp/dp_debug.h"
#include "../msm/dp/dp_aux.h"
#include "../msm/dp/dp_hpd.h"
/*------------------------------- variables ---------------------------------*/
#ifdef CONFIG_NUBIA_HDMI_FEATURE
extern struct kobject *nubia_global_enhace_kobj;
#endif
static struct kobject *enhance_kobj = NULL;

static ssize_t link_statue_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{		
	return snprintf(buf, PAGE_SIZE, "%d\n", -1);
}

static ssize_t link_statue_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
	return size;
}

static ssize_t dp_debug_hpd_show(struct kobject *kobj,
		 struct kobj_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t dp_debug_hpd_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
	return size;
}

static ssize_t edid_modes_show(struct kobject *kobj,
		 struct kobj_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t edid_modes_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
	return size;
}

static ssize_t dp_debug_selected_edid_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
	return size;
}

static ssize_t dp_debug_selected_edid_show(struct kobject *kobj,
		 struct kobj_attribute *attr, char *buf)
{
	return 0;
}

static struct kobj_attribute usb_disp_attrs[] = {
	__ATTR(link_statue,        0664,	link_statue_show,      link_statue_store),
};
#ifdef CONFIG_NUBIA_HDMI_FEATURE
static struct kobj_attribute disp_attrs[] = {
	__ATTR(edid_modes,         0664,	edid_modes_show,       edid_modes_store),
	__ATTR(hpd,		   0664,        dp_debug_hpd_show,     dp_debug_hpd_store),
	__ATTR(selected_edid,	0664,        dp_debug_selected_edid_show,     dp_debug_selected_edid_store),
};
#endif

static int __init nubia_dp_preference_init(void)
{
	int retval1 = 0;
	int attr_count1 = 0;
#ifdef CONFIG_NUBIA_HDMI_FEATURE
	int retval2 = 0;
	int attr_count2 = 0;
#endif

	enhance_kobj = kobject_create_and_add("usb_dp_enhance", kernel_kobj);

	if (!enhance_kobj) {
		return -ENOMEM;
	}

	/* Create attribute files associated with this kobject */
	for (attr_count1 = 0; attr_count1 < ARRAY_SIZE(usb_disp_attrs); attr_count1++) {
		retval1 = sysfs_create_file(enhance_kobj, &usb_disp_attrs[attr_count1].attr);
		if (retval1 < 0) {
			goto err_sys_creat1;
		}
	}
#ifdef CONFIG_NUBIA_HDMI_FEATURE
	if (!nubia_global_enhace_kobj) {
		printk("NUBIA-->entry lcd_enhance fail nubia_global_enhace_kobj = %x\n", nubia_global_enhace_kobj);
	}
	for (attr_count2 = 0; attr_count2 < ARRAY_SIZE(disp_attrs); attr_count2++) {
		retval2 = sysfs_create_file(nubia_global_enhace_kobj, &disp_attrs[attr_count2].attr);
		if (retval2 < 0) {
			goto err_sys_creat2;
		}
	}
#endif

	return retval2;

err_sys_creat1:
	for (--attr_count1; attr_count1 >= 0; attr_count1--)
		sysfs_remove_file(enhance_kobj, &usb_disp_attrs[attr_count1].attr);

	kobject_put(enhance_kobj);
#ifdef CONFIG_NUBIA_HDMI_FEATURE
err_sys_creat2:
	for (--attr_count2; attr_count2 >= 0; attr_count2--)
		sysfs_remove_file(nubia_global_enhace_kobj, &disp_attrs[attr_count2].attr);
	kobject_put(nubia_global_enhace_kobj);
	return retval2;
#endif
}

static void __exit nubia_dp_preference_exit(void)
{
	int attr_count1 = 0;
#ifdef CONFIG_NUBIA_HDMI_FEATURE
	int attr_count2 = 0;
#endif

	for (attr_count1 = 0; attr_count1< ARRAY_SIZE(usb_disp_attrs); attr_count1++)
		sysfs_remove_file(enhance_kobj, &usb_disp_attrs[attr_count1].attr);

	kobject_put(enhance_kobj);
#ifdef CONFIG_NUBIA_HDMI_FEATURE
	for (attr_count2 = 0; attr_count2< ARRAY_SIZE(usb_disp_attrs); attr_count2++)
		sysfs_remove_file(enhance_kobj, &disp_attrs[attr_count2].attr);

	kobject_put(nubia_global_enhace_kobj);
#endif
}

MODULE_AUTHOR("NUBIA USB Driver Team Software");
MODULE_DESCRIPTION("NUBIA USB DISPLAY Saturation and Temperature Setting");
MODULE_LICENSE("GPL");
module_init(nubia_dp_preference_init);
module_exit(nubia_dp_preference_exit);
