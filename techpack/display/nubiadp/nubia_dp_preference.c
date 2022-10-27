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
static struct kobject *enhance_kobj = NULL;
struct _select_sde_edid_info select_sde_edid_info = {0};
char edid_mode_best_info[32] = {0};
struct dp_debug_private *debug_node = NULL;
struct dp_aux *global_nubia_dp_aux = NULL;
struct kobject *nubia_global_enhace_kobj = NULL;
char edid_device_name[20] = {0};
struct _user_select_sde_edid_info *edid_info = NULL;

static ssize_t link_statue_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{		
	if(global_nubia_dp_aux == NULL)
		return snprintf(buf, PAGE_SIZE, "%d\n", -1);
	if ((global_nubia_dp_aux->state & DP_STATE_TRAIN_1_SUCCEEDED) && (global_nubia_dp_aux->state & DP_STATE_TRAIN_2_SUCCEEDED))
		return snprintf(buf, PAGE_SIZE, "%d\n", 1);
	else
		return snprintf(buf, PAGE_SIZE, "%d\n", -1);
}

static ssize_t link_statue_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
//	NUBIA_DISP_INFO("please don't try to set the link_statue\n");

	if(global_nubia_dp_aux == NULL)
		return size;

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
	int const hpd_data_mask = 0x7;
	int hpd = 0;

	if (!debug_node)
		return -ENODEV;

    sscanf(buf, "%d", &hpd);
	printk("%s:  hpd = %d \n", __func__, hpd);
	
	hpd &= hpd_data_mask;
	debug_node->hotplug = !!(hpd & BIT(0));

	debug_node->dp_debug.psm_enabled = !!(hpd & BIT(1));

	/*
	 * print hotplug value as this code is executed
	 * only while running in debug mode which is manually
	 * triggered by a tester or a script.
	 */
	DP_INFO("%s\n", debug_node->hotplug ? "[CONNECT]" : "[DISCONNECT]");
	if(hpd == 0)
	{
		select_sde_edid_info.edid_hot_plug = true;
	}

	debug_node->hpd->simulate_connect(debug_node->hpd, debug_node->hotplug);

	return size;
}

static ssize_t edid_modes_show(struct kobject *kobj,
		 struct kobj_attribute *attr, char *buf)
{
		char *buf_edid;
		u32 len = 0, ret = 0, max_size = SZ_4K;
		int rc = 0;
	
		buf_edid = kzalloc(SZ_4K, GFP_KERNEL);
		if (ZERO_OR_NULL_PTR(buf_edid)) {
			rc = -ENOMEM;
			goto error;
		}
	
		ret = snprintf(buf_edid, max_size, "%s", edid_mode_best_info);
		len = snprintf(buf_edid + ret, max_size, "%s", select_sde_edid_info.edid_mode_info);
		printk("%s: edid_mode_best_info = %s \n", edid_mode_best_info);
		len = sprintf(buf, "%s", buf_edid);
 		kfree(buf_edid);
	
		return len;
	error:
		return rc;

}

void sys_print_edid_info(struct _user_select_sde_edid_info *info_temp)
{
	printk("h = %d, v = %d, fps = %d, name=%s \n", info_temp->h, info_temp->v, info_temp->fps, info_temp->device_name);
}

void sys_show_edid_info()
{
#if 1
	struct _user_select_sde_edid_info *info_temp = edid_info;
	printk("to show edid info \n");
	for(; info_temp; ){
		sys_print_edid_info(info_temp);
		info_temp = info_temp->next;
	}
#endif
}

struct _user_select_sde_edid_info * store_edid_info(int h, int v, int fps, int rto, char *name)
{
	struct _user_select_sde_edid_info *info;
	info = kzalloc(sizeof(struct _user_select_sde_edid_info), GFP_KERNEL);
	if(!info){
		printk("store_edid_info: alloc struct fail\n");
		return NULL;
	}
	info->h = h;
	info->v = v;
	info->fps = fps;
	info->ratio = rto;
	strcpy(info->device_name, name);
	info->next = NULL;
	return info;
}

 void sys_store_edid_info(int h, int v, int fps, int rto, char *name)
 {
	 struct _user_select_sde_edid_info *info_temp = edid_info;
	 printk("sys_store_edid_info: h=%d, v=%d, fps=%d rto=%d\n", h, v, fps, rto);
	 if(!edid_info){
		edid_info = store_edid_info(h, v, fps, rto, name);
		return ;
	 }
	for(; info_temp; ){
		/**
		** if we find same device, and user change to fps and solution, we just change the fps and soultion
		** do not change the others
		**/
		if(!strcmp(name, info_temp->device_name)){
			info_temp->h = h;
			info_temp->v = v;
			info_temp->fps = fps;
			info_temp->ratio = rto;
			return;
		}else{
			if(info_temp->next != NULL)
				info_temp = info_temp->next;
			else
				break;
		}
	}

	for(; info_temp->next; ){
		info_temp = info_temp->next;
	}
	info_temp->next = store_edid_info(h, v, fps, rto, name);
 }

static ssize_t edid_modes_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
	int hdisplay = 0, vdisplay = 0, vrefresh = 0, aspect_ratio;
	char device_name[20];
	if (!debug_node)
		return -ENODEV;

	if (sscanf(buf, "%d %d %d %d", &hdisplay, &vdisplay, &vrefresh,
				&aspect_ratio) != 4)
		goto clear;
	printk("NUBIA_DP:hdisplay = %d, vdisplay = %d, vrefresh = %d, aspect_ratio = %d\n", 
		hdisplay, vdisplay, vrefresh, aspect_ratio);
		
	if (!hdisplay || !vdisplay || !vrefresh)
		goto clear;
	select_sde_edid_info.node_control = true;
	debug_node->dp_debug.debug_en = true;
	debug_node->dp_debug.hdisplay = hdisplay;
	debug_node->dp_debug.vdisplay = vdisplay;
	debug_node->dp_debug.vrefresh = vrefresh;
	debug_node->dp_debug.aspect_ratio = aspect_ratio;
	/*store the select fps and resulation of edid_mode_info*/
	memset(edid_mode_best_info, 0x00, 32);
	snprintf(edid_mode_best_info, 32,"%dx%d %d %d\n",hdisplay, vdisplay,vrefresh, aspect_ratio);

	strcpy(device_name, edid_device_name);
	sys_store_edid_info(hdisplay, vdisplay, vrefresh, aspect_ratio, device_name);

	sys_show_edid_info();

	select_sde_edid_info.edid_mode_store = true;
	goto end;
clear:
	printk("NUBIA_DP:clearing debug modes\n");
	debug_node->dp_debug.debug_en = false;
end:
	return size;
}

static ssize_t dp_debug_selected_edid_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
	int hdisplay = 0, vdisplay = 0, vrefresh = 0, aspect_ratio;
	char device_name[20];
	char sub_name[20];
	if (!debug_node)
		return -ENODEV;

	if (sscanf(buf, "%d %d %d %d %s %s", &hdisplay, &vdisplay, &vrefresh, &aspect_ratio, device_name, sub_name) <= 0)
		goto end;

	if (!hdisplay || !vdisplay || !vrefresh)
		goto end;
	if (strlen(sub_name)){
		strcat(device_name, " ");
		strcat(device_name, sub_name);
		printk("dp_debug_selected_edid_store device_name = %s.\n", device_name);
	}

	sys_store_edid_info(hdisplay, vdisplay, vrefresh, aspect_ratio, device_name);
	sys_show_edid_info();
end:
	return size;
}

static ssize_t dp_debug_selected_edid_show(struct kobject *kobj,
		 struct kobj_attribute *attr, char *buf)
{
	char *buf_edid;
	char edid_modes_list[32] = {0};
	u32 len = 0, ret = 0, max_size = SZ_4K;
	int rc = 0;
	struct _user_select_sde_edid_info *info_temp = edid_info;
	printk("%s: to show edid info \n", __func__);

    if (!info_temp)
        return rc;

	sys_print_edid_info(info_temp);

	buf_edid = kzalloc(SZ_4K, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(buf_edid)) {
		rc = -ENOMEM;
		goto error;
	}

	for(; info_temp; ){
		snprintf(edid_modes_list, 40,
			"%dx%d %d %d %s\n",info_temp->h, info_temp->v,
			info_temp->fps, info_temp->ratio, info_temp->device_name);
		ret += snprintf(buf_edid+ret, max_size, "%s", edid_modes_list);
		info_temp = info_temp->next;
	}
	printk("%s: edid_mode_best_info = %s \n", buf_edid);
	len = sprintf(buf, "%s", buf_edid);
		kfree(buf_edid);

	return len;
error:
		return rc;
}

static struct kobj_attribute usb_disp_attrs[] = {
	__ATTR(link_statue,        0664,	link_statue_show,      link_statue_store),
};
static struct kobj_attribute disp_attrs[] = {
	__ATTR(edid_modes,         0664,	edid_modes_show,       edid_modes_store),
	__ATTR(hpd,		   0664,        dp_debug_hpd_show,     dp_debug_hpd_store), 
	__ATTR(selected_edid,	0664,        dp_debug_selected_edid_show,     dp_debug_selected_edid_store), 
};

//void nubia_set_usbdp_ctrl(struct dp_aux *display)
//{
//	NUBIA_DISP_INFO("start\n");
    
//	nubia_dp_aux = display;
//}

static int __init nubia_dp_preference_init(void)
{
	int retval1 = 0;
	int retval2 = 0;
	int attr_count1 = 0;
	int attr_count2 = 0;

//	NUBIA_DISP_INFO("start\n");

	enhance_kobj = kobject_create_and_add("usb_dp_enhance", kernel_kobj);

	if (!enhance_kobj) {
//		NUBIA_DISP_ERROR("failed to create and add kobject\n");
		return -ENOMEM;
	}

	/* Create attribute files associated with this kobject */
	for (attr_count1 = 0; attr_count1 < ARRAY_SIZE(usb_disp_attrs); attr_count1++) {
		retval1 = sysfs_create_file(enhance_kobj, &usb_disp_attrs[attr_count1].attr);
		if (retval1 < 0) {
//			NUBIA_DISP_ERROR("failed to create sysfs attributes\n");
			goto err_sys_creat1;
		}
	}
	if (!nubia_global_enhace_kobj) {
		printk("NUBIA-->entry lcd_enhance fail nubia_global_enhace_kobj = %x\n", nubia_global_enhace_kobj);
	}
	for (attr_count2 = 0; attr_count2 < ARRAY_SIZE(disp_attrs); attr_count2++) {
		retval2 = sysfs_create_file(nubia_global_enhace_kobj, &disp_attrs[attr_count2].attr);
		if (retval2 < 0) {
			goto err_sys_creat2;
		}
	}
//	NUBIA_DISP_INFO("success\n");

	return retval2;

err_sys_creat1:
	for (--attr_count1; attr_count1 >= 0; attr_count1--)
		sysfs_remove_file(enhance_kobj, &usb_disp_attrs[attr_count1].attr);

	kobject_put(enhance_kobj);
err_sys_creat2:
	for (--attr_count2; attr_count2 >= 0; attr_count2--)
		sysfs_remove_file(nubia_global_enhace_kobj, &disp_attrs[attr_count2].attr);
	kobject_put(nubia_global_enhace_kobj);
	return retval2;
}

static void __exit nubia_dp_preference_exit(void)
{
	int attr_count1 = 0;
	int attr_count2 = 0;

	for (attr_count1 = 0; attr_count1< ARRAY_SIZE(usb_disp_attrs); attr_count1++)
		sysfs_remove_file(enhance_kobj, &usb_disp_attrs[attr_count1].attr);

	kobject_put(enhance_kobj);
	for (attr_count2 = 0; attr_count2< ARRAY_SIZE(usb_disp_attrs); attr_count2++)
		sysfs_remove_file(enhance_kobj, &disp_attrs[attr_count2].attr);

	kobject_put(nubia_global_enhace_kobj);
}

MODULE_AUTHOR("NUBIA USB Driver Team Software");
MODULE_DESCRIPTION("NUBIA USB DISPLAY Saturation and Temperature Setting");
MODULE_LICENSE("GPL");
module_init(nubia_dp_preference_init);
module_exit(nubia_dp_preference_exit);
#endif
