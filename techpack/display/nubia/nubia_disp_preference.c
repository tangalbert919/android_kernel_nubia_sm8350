/*
 * nubia_disp_preference.c - nubia lcd display color enhancement and temperature setting
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
 * Supports NUBIA lcd display color enhancement and color temperature setting
 */

/*------------------------------ header file --------------------------------*/
#include <linux/delay.h>
#include <linux/kthread.h>
#include "nubia_disp_preference.h"

struct dsi_display *nubia_display = NULL;
struct dsi_display *nubia_sec_display = NULL;
/*------------------------------- variables ---------------------------------*/
static struct kobject *enhance_kobj = NULL;
#ifdef CONFIG_NUBIA_HDMI_FEATURE
extern struct kobject *nubia_global_enhace_kobj;
#endif
struct nubia_disp_type nubia_disp_val = {
	.en_cabc = 1,
	.cabc = CABC_OFF,
	.lhbm_mode = LHBM_OFF,
	.en_dfps = 1,
	.dfps = DFPS_90,
	.en_fps_change = 1,
	.panel_reg_val = {0x0},
	.panel_reg_lens = 1,
	.panel_reg_addr = 0xe7,
	.panel_trace_code = {0},
	.bl_limit = 4095,
	.panel_cur_brightness = 0,
	.hbm_state = 0,
};

static bool has_init_hbm_feedback_wq = false;
wait_queue_head_t hbm_feedback_event;

unsigned int fps_store = 90;
unsigned int fps_temp = 90;
bool fps_changed = false;

u8 rx_buf[1024] = {0};
u16 rx_len = 0;

u8 hex_to_char(u8 bChar)
{
	if((bChar >= 0x30) && (bChar <= 0x39))
		bChar -= 0x30;
	else if((bChar >= 0x41) && (bChar <= 0x46))
		bChar -= 0x37;
	else if((bChar >= 0x61) && (bChar <= 0x66))
		bChar -= 0x57;
	return bChar;
}

unsigned char char_to_hex(unsigned char bChar)
{
	if((bChar >=0) && (bChar<=9))
		bChar += 0x30;
	else if((bChar >=10) && (bChar<=15))
		bChar += 0x37;
	else bChar = 0xFF;

	return bChar;
}

static void kernel_str_int(const char *buf, unsigned char *tx_buf)
{
	int i = 0, j = 0, k = 0;
	char *p;
	p = (char *)buf;

	for(i=0; *p != '\n'; i++){
		if(*p == ' '){
			j++;
			k = 0;
		}else{
			tx_buf[j] = (tx_buf[j] << 4) | hex_to_char(*p);
			k++;
		}
		p++;
	}
}

static ssize_t panel_firmware_id_show(struct kobject *kobj,
        struct kobj_attribute *attr, char *buf)
{
	if (nubia_disp_val.panel_firmware_id)
		return snprintf(buf, PAGE_SIZE, "0x%x\n", nubia_disp_val.panel_firmware_id);
	else
		return snprintf(buf, PAGE_SIZE, "NULL\n");
}

static ssize_t panel_firmware_id_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
	return size; /* Nothing need to do */
}

static ssize_t nubia_lcd_trace_code_show(struct kobject *kobj,
        struct kobj_attribute *attr, char *buf)
{
	if(nubia_display == NULL) {
		NUBIA_DISP_ERROR("no nubia_display node!\n");
		return -EINVAL;
	}

	NUBIA_DISP_INFO(" panel_reg_lens = %s" , nubia_disp_val.panel_trace_code);

	return snprintf(buf, PAGE_SIZE, "%s\n", nubia_disp_val.panel_trace_code);
}

static ssize_t nubia_lcd_trace_code_store(struct kobject *kobj,
        struct kobj_attribute *attr, const char *buf, size_t size)
{
	uint8_t addr1 = 0, addr2 = 0;
	uint8_t page_addr = 0;
	size_t len1 = 0, len2 = 0;
	u8 *reg_val1 = NULL;
	u8 *reg_val2 = NULL;
	u32 i, j = 0;
	int ret = 0;

	if(nubia_display == NULL) {
		NUBIA_DISP_ERROR("no nubia_display node!\n");
		return -EINVAL;
	}

	NUBIA_DISP_INFO("buf = %s\n", buf);
	//sscanf(buf, "%d", &val);
	//NUBIA_DISP_INFO("val = 0x%x\n", val);
	if (!strcmp(buf, "F")) {
		addr1 = 0xD6;
		addr2 = 0xD7;
		page_addr = 0xF0;
		len1 = 8;
		len2 = 6;
	} else {
		NUBIA_DISP_ERROR("unsupport code !!!\n");
		return size;
	}

	/* First change to page one */
	ret = nubia_dsi_panel_write_reg(nubia_display->panel, page_addr);

	/* Second read DDIC register */
	reg_val1 = kzalloc(len1, GFP_KERNEL);
	if (!reg_val1)
		return -ENOMEM;

	ret = nubia_dsi_panel_read_reg(nubia_display->panel, addr1, reg_val1, len1);
	if (ret > 0) {
		for (i = 0; i < len1; i++) {
			nubia_disp_val.trace_code_val1[i] = reg_val1[i];
			NUBIA_DISP_INFO("trace_code_val1[0x%x][%d] = 0x%x\n", addr1, i, *(reg_val1 + i));
			NUBIA_DISP_INFO("trace_code_val1[0x%x][%d] = %c\n", addr1, i, (char)reg_val1[i]);
			nubia_disp_val.panel_trace_code[j] = (char)reg_val1[i];
			j++;
		}
		ret = 0;
	}

	reg_val2 = kzalloc(len2, GFP_KERNEL);
	if (!reg_val2)
		return -ENOMEM;

	ret = nubia_dsi_panel_read_reg(nubia_display->panel, addr2, reg_val2, len2);
	if (ret > 0) {
		for (i = 0; i < len2; i++) {
			nubia_disp_val.trace_code_val2[i] = reg_val2[i];
			//nubia_disp_val.panel_trace_code += hex_to_char(reg_val2[i]);
			NUBIA_DISP_INFO("trace_code_val2[0x%x][%d] = 0x%x\n", addr2, i, *(reg_val2 + i));
			NUBIA_DISP_INFO("trace_code_val1[0x%x][%d] = %c\n", addr1, i, (char)reg_val2[i]);
			nubia_disp_val.panel_trace_code[j] = (char)reg_val2[i];
			j++;
		}
		ret = 0;
	}

	/*Third change register value to ASCII code*/
	NUBIA_DISP_INFO("Get trace_code = %s\n", nubia_disp_val.panel_trace_code);

	kfree(reg_val1);
	kfree(reg_val2);

	return ret ? ret : size;
}

static ssize_t nubia_panel_reg_show(struct kobject *kobj,
        struct kobj_attribute *attr, char *buf)
{
	int i = 0;
	ssize_t ret = 0;

	if(nubia_display == NULL) {
		NUBIA_DISP_ERROR("no nubia_display node!\n");
		return -EINVAL;
	}

	for (i = 0; i < nubia_disp_val.panel_reg_lens; i++) {
		ret += snprintf(buf + ret, PAGE_SIZE, "[%d][0x%x]:0x%x\n",
			i,
			nubia_disp_val.panel_reg_addr,
			nubia_disp_val.panel_reg_val[i]);
	}

	NUBIA_DISP_INFO("ret = %d, panel_reg_lens = %d\n", ret, nubia_disp_val.panel_reg_lens);
	return ret;
}

static ssize_t nubia_panel_reg_store(struct kobject *kobj,
        struct kobj_attribute *attr, const char *buf, size_t size)
{
	uint8_t addr = 0;
	size_t len;
	uint8_t get_buf[2] = {0};
	u8 *reg_val = NULL;
	u32 i;
	int ret = 0;

	if(nubia_display == NULL) {
		NUBIA_DISP_ERROR("no nubia_display node!\n");
		return -EINVAL;
	}

	NUBIA_DISP_INFO("buf = %s\n", buf);
	if (!strcmp(buf, "e7")) {
		addr = 0xe7;
		len = 1;
	} else {
		kernel_str_int(buf, get_buf);
		if (get_buf[1] == 0)
			get_buf[1] = 1;

		addr = get_buf[0];
		len = get_buf[1];
	}
	nubia_disp_val.panel_reg_addr = addr;
	nubia_disp_val.panel_reg_lens = len;

	NUBIA_DISP_INFO("get reg address = 0x%x , len = %d\n", addr, len);

	reg_val = kzalloc(len, GFP_KERNEL);
	if (!reg_val)
		return -ENOMEM;

	ret = nubia_dsi_panel_read_reg(nubia_display->panel, addr, reg_val, len);
	if (ret > 0) {
		for (i = 0; i < len; i++) {
			nubia_disp_val.panel_reg_val[i] = reg_val[i];
			NUBIA_DISP_INFO("Read reg[0x%x][%d] = 0x%x\n", addr, i, *(reg_val + i));
		}
		ret = 0;
	}

	kfree(reg_val);

	return ret ? ret : size;
}

static ssize_t cabc_enable_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	if(nubia_display == NULL) {
		NUBIA_DISP_ERROR("no nubia_display node!\n");
		return -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", nubia_disp_val.en_cabc);
}

static ssize_t cabc_enable_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
	uint32_t val = 0;

	if(nubia_display == NULL) {
		NUBIA_DISP_ERROR("no nubia_display node!\n");
		return size;
	}

	sscanf(buf, "%d", &val);
	if ((val != 0) && (val != 1)) {
		NUBIA_DISP_ERROR("invalid cabc_enable val = %d\n", val);
		return size;
	}

	nubia_disp_val.en_cabc = val;
	NUBIA_DISP_INFO("success to set cabc_enable as = %d\n", val);

	return size;

}

static ssize_t cabc_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
		if (nubia_disp_val.en_cabc)
				return snprintf(buf, PAGE_SIZE, "%d\n", nubia_disp_val.cabc);
		else
				return snprintf(buf, PAGE_SIZE, "NULL\n");
}

static ssize_t cabc_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
	uint32_t val = 0;
	int ret = 0;

	if(nubia_display == NULL) {
		NUBIA_DISP_ERROR("no nubia_display node!\n");
		return size;
	}

	if(!nubia_disp_val.en_cabc) {
		NUBIA_DISP_ERROR("no cabc\n");
		return size;
	}

	sscanf(buf, "%d", &val);
	if ((val != CABC_OFF) && (val != CABC_LEVEL1) &&
		(val != CABC_LEVEL2) && (val != CABC_LEVEL3)) {
			NUBIA_DISP_ERROR("invalid cabc val = %d\n", val);
			return size;
	}
	NUBIA_DISP_INFO("cabc value = %d\n", val);

	if (((val != CABC_OFF) && (nubia_disp_val.panel_cur_brightness == 4095)) ||
		((val != CABC_OFF) && (nubia_display->panel->hbm_mode == true))) {
		NUBIA_DISP_INFO("panel in HBM mode, need disable cabc!!\n");
		val = CABC_OFF;
	}

	ret = nubia_dsi_panel_cabc(nubia_display->panel, val);
	if (ret == 0) {
		nubia_disp_val.cabc = val;
		NUBIA_DISP_INFO("success to set cabc as = %d\n", val);
	}

	return size;
}

static ssize_t lhbm_mode_show(struct kobject *kobj,
        struct kobj_attribute *attr, char *buf)
{
	if(nubia_display == NULL) {
		NUBIA_DISP_ERROR("no nubia_display node!\n");
		return -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", nubia_disp_val.lhbm_mode);
}

static ssize_t lhbm_mode_store(struct kobject *kobj,
        struct kobj_attribute *attr, const char *buf, size_t size)
{
	uint32_t val = 0;
	int ret = 0;

	if(nubia_display == NULL) {
		NUBIA_DISP_ERROR("no nubia_display node!\n");
		return size;
	}

	sscanf(buf, "%d", &val);
	NUBIA_DISP_INFO("LHBM mode value = %d\n", val);

	ret = nubia_dsi_panel_lhbm(nubia_display->panel, val);
	if (ret == 0) {
		nubia_disp_val.hbm_mode = val;
		NUBIA_DISP_INFO("success to set local hbm mode as = %d\n", val);
	}

	return size;
}

static ssize_t hbm_mode_show(struct kobject *kobj,
        struct kobj_attribute *attr, char *buf)
{
	if(nubia_display == NULL) {
		NUBIA_DISP_ERROR("no nubia_display node!\n");
		return -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", nubia_disp_val.hbm_mode);
}

static ssize_t hbm_mode_store(struct kobject *kobj,
        struct kobj_attribute *attr, const char *buf, size_t size)
{
	uint32_t val = 0;
	int ret = 0;

	if(nubia_display == NULL) {
		NUBIA_DISP_ERROR("no nubia_display node!\n");
		return size;
	}

	sscanf(buf, "%d", &val);
	if(val < 0 || val > 4095) {
		NUBIA_DISP_ERROR("invalid hbm params! value = %d\n", val);
		return size;
	}

	NUBIA_DISP_INFO("[uid:%d, pid:%d]HBM mode value %d\n", current->cred->uid.val, current->tgid, val);

	ret = nubia_dsi_panel_hbm(nubia_display->panel, val);
	if (ret == 0) {
		nubia_disp_val.hbm_mode = val;
		NUBIA_DISP_INFO("success to set hbm mode as = %d\n", val);
	}

	return size;
}

static ssize_t hbm_state_show(struct kobject *kobj,
        struct kobj_attribute *attr, char *buf)
{
	if(nubia_display == NULL) {
		NUBIA_DISP_ERROR("no nubia_display node!\n");
		return -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", nubia_disp_val.hbm_state);
}

static ssize_t hbm_state_store(struct kobject *kobj,
        struct kobj_attribute *attr, const char *buf, size_t size)
{
	uint32_t val = 0;

	if(nubia_display == NULL) {
		NUBIA_DISP_ERROR("no nubia_display node!\n");
		return size;
	}

	sscanf(buf, "%d", &val);
	if(val < 0 || val > 4095) {
		NUBIA_DISP_ERROR("invalid hbm state params! value = %d\n", val);
		return size;
	}

	NUBIA_DISP_INFO("[uid:%d, pid:%d]HBM state value %d\n", current->cred->uid.val, current->tgid, val);

	nubia_disp_val.hbm_state = val;

	return size;
}

static ssize_t hbm_feedback_show(struct kobject *kobj,
        struct kobj_attribute *attr, char *buf)
{
	if(nubia_display == NULL) {
		NUBIA_DISP_ERROR("no nubia_display node!\n");
		return -EINVAL;
	}

	NUBIA_DISP_INFO("Waiting hbm state feedback \n");
	wait_event(hbm_feedback_event, nubia_display->panel->hbm_change_done);
	NUBIA_DISP_INFO("hbm change done!!!%d\n", nubia_disp_val.hbm_state);
	nubia_display->panel->hbm_change_done = false;

	return snprintf(buf, PAGE_SIZE, "%d\n", nubia_disp_val.hbm_state);
}

static ssize_t hbm_feedback_store(struct kobject *kobj,
        struct kobj_attribute *attr, const char *buf, size_t size)
{
	return size; /* TO DO */
}

static ssize_t aod_brightness_show(struct kobject *kobj,
        struct kobj_attribute *attr, char *buf)
{
	if(nubia_display == NULL) {
		NUBIA_DISP_ERROR("no nubia_display node!\n");
		return -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", nubia_disp_val.aod_brightness);
}

static ssize_t aod_brightness_store(struct kobject *kobj,
        struct kobj_attribute *attr, const char *buf, size_t size)
{
	uint32_t val = 0;
	int ret = 0;

	if(nubia_display == NULL) {
		NUBIA_DISP_ERROR("no nubia_display node!\n");
		return size;
	}

	sscanf(buf, "%d", &val);
	if (nubia_display->panel->ddic_vendor == NOVATEK) {
		if(val < 336 || val > 4095) {
			NUBIA_DISP_ERROR("invalid aod_brightness params!\n");
			return size;
		}
	}

	NUBIA_DISP_INFO("[uid:%d, pid:%d] AOD mode brightness value = %d\n", current->cred->uid.val, current->tgid, val);

	nubia_disp_val.aod_brightness = val;

	ret = nubia_set_aod_brightness(nubia_display->panel, val);
	if (ret == 0) {
		NUBIA_DISP_INFO("success to set aod mode brightness as = %d\n", val);
	}

	return size;
}

static ssize_t dfps_show(struct kobject *kobj,
		 struct kobj_attribute *attr, char *buf)
{
	if (nubia_disp_val.en_dfps)
		return snprintf(buf, PAGE_SIZE, "%d\n", nubia_disp_val.dfps);
	else
		return snprintf(buf, PAGE_SIZE, "NULL\n");
}

static ssize_t dfps_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
	return size; /* Already use setActiveConfig interface set fps */
}

static ssize_t panel_cur_brightness_show(struct kobject *kobj,
		 struct kobj_attribute *attr, char *buf)
{
	if(nubia_display == NULL) {
		NUBIA_DISP_ERROR("no nubia_display node!\n");
		return -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", nubia_disp_val.panel_cur_brightness);
}


static ssize_t panel_cur_brightness_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
	return size; /* TO DO */
}

static ssize_t lcd_reg_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int i = 0, j = 0;
	unsigned char val = 0;
	for(i=0; i< strlen(rx_buf) / sizeof(char); i++){
		val = (rx_buf[i] & 0xF0) >> 4;
		buf[j++] = char_to_hex(val);
		val = rx_buf[i] & 0x0F;
		buf[j++] = char_to_hex(val);
		buf[j++] = ' ';
	}
	buf[j] = '\n';
	//for(i=0; i<j-1; i++)
	//	printk("%c", buf[i]);
	return j;
}

/**
** read and write lcd register
** buf[0] = 1 read;
** 		buf[1] = which register do you want to read
** 		buf[2] = how many num do you want to get
** buf[0] = 0 write
**		buf[1] = which register do you want to write
** 		buf[2] = how many reg num do you want to
**		buf[3] ....  the data to write
**/
static ssize_t lcd_reg_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t size)
{
	int rc = 0, i=0;
	u8 tx_buf[200] = {0};
	u8 *data;
	struct mipi_dsi_device *dsi = &nubia_display->panel->mipi_device;
	if(!dsi){
		pr_err("%s: lcd reg store error, dsi is NULL\n", __func__);
		return 0;
	}

	kernel_str_int(buf, tx_buf);

	if(tx_buf[0] == 1){
		rx_len = tx_buf[2];
		data = (u8*)kzalloc(rx_len, GFP_KERNEL);

		printk("lcd_reg: read tx_buf[0] = %d, tx_buf[1] = %d, tx_buf[2] = %d", tx_buf[0] , tx_buf[1], tx_buf[2] );
		rc = dsi_panel_read_data(dsi, tx_buf[1], data, tx_buf[2]);
		printk(">>>>>>>lcd_reg: rc = %d \n", rc);
		for(i=0; i<rc; i++){
			printk("lcd_reg: %d = %x \n",i, data[i]);
		}
		if(rc > 0)
			memcpy(rx_buf, data, rc);

		kfree(data);
		data = NULL;
		if(rc<0){
			pr_err("%s: read panel data error \n", __func__);
			return 0;
		}
	}else{
		rc = dsi_panel_write_data(dsi, tx_buf[1], &tx_buf[3], tx_buf[2]);
	}
	if(rc<0){
		pr_err("%s: write panel data error \n", __func__);
		return 0;
	}
	return size;
}

static ssize_t bl_limit_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	if(nubia_display == NULL) {
		NUBIA_DISP_ERROR("no nubia_display node!\n");
		return -EINVAL;
	}

	return snprintf(buf, PAGE_SIZE, "%d\n", nubia_disp_val.bl_limit);
}

extern void nubia_display_set_backlight(struct dsi_display *display);
static ssize_t bl_limit_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t size)
{
	int val;

	if (nubia_display == NULL) {
		NUBIA_DISP_ERROR("no nubia_display node!\n");
		return -EINVAL;
	}

	sscanf(buf, "%d", &val);
	nubia_disp_val.bl_limit = val;

	if (nubia_disp_val.bl_limit > 4095)
		nubia_disp_val.bl_limit = 4095;

	if (nubia_disp_val.hbm_state != 4095) {
		nubia_display_set_backlight(nubia_display);//set backlight again
		NUBIA_DISP_INFO("success to set bl_limit = [%d]\n", nubia_disp_val.bl_limit);
	} else {
		NUBIA_DISP_INFO("panel in HBM mode, needn't set bl_limit[%d]\n", nubia_disp_val.bl_limit);
	}

	return size;
}

static struct kobj_attribute lcd_disp_attrs[] = {
	__ATTR(cabc, 0664, cabc_show, cabc_store),
	__ATTR(cabc_enable, 0664, cabc_enable_show, cabc_enable_store),
	__ATTR(hbm_mode, 0664, hbm_mode_show, hbm_mode_store),
	__ATTR(lhbm_mode, 0664, lhbm_mode_show, lhbm_mode_store),
	__ATTR(lcd_reg, 0664, lcd_reg_show, lcd_reg_store),
	__ATTR(dfps, 0664, dfps_show, dfps_store),
	__ATTR(nubia_panel_reg, 0664, nubia_panel_reg_show, nubia_panel_reg_store),
	__ATTR(nubia_lcd_trace_code, 0664, nubia_lcd_trace_code_show, nubia_lcd_trace_code_store),
	__ATTR(panel_firmware_id, 0664, panel_firmware_id_show, panel_firmware_id_store),
	__ATTR(aod_brightness, 0664, aod_brightness_show, aod_brightness_store),
	__ATTR(bl_limit, 0664, bl_limit_show, bl_limit_store),
	__ATTR(panel_cur_brightness, 0664, panel_cur_brightness_show, panel_cur_brightness_store),
	__ATTR(hbm_state, 0664, hbm_state_show, hbm_state_store),
	__ATTR(hbm_feedback, 0664, hbm_feedback_show, hbm_feedback_store),
};

void nubia_set_dsi_ctrl(struct dsi_display *display, const char *dsi_type)
{
	NUBIA_DISP_INFO("start %s\n", dsi_type);

	if (!strcmp(dsi_type, "primary"))
		nubia_display = display;
	else
		nubia_sec_display = display;
}

static int __init nubia_disp_preference_init(void)
{
	int retval = 0;
	int attr_count = 0;

	NUBIA_DISP_INFO("start\n");

	enhance_kobj = kobject_create_and_add("lcd_enhance", kernel_kobj);
#ifdef CONFIG_NUBIA_HDMI_FEATURE
	nubia_global_enhace_kobj = enhance_kobj;
#endif
	if (!enhance_kobj) {
		NUBIA_DISP_ERROR("failed to create and add kobject\n");
		return -ENOMEM;
	}

	/* Create attribute files associated with this kobject */
	for (attr_count = 0; attr_count < ARRAY_SIZE(lcd_disp_attrs); attr_count++) {
		retval = sysfs_create_file(enhance_kobj, &lcd_disp_attrs[attr_count].attr);
		if (retval < 0) {
			NUBIA_DISP_ERROR("failed to create sysfs attributes\n");
			goto err_sys_creat;
		}
	}

	if(!has_init_hbm_feedback_wq) {
		init_waitqueue_head(&hbm_feedback_event);
	}
	has_init_hbm_feedback_wq = true;

	NUBIA_DISP_INFO("success\n");

	return retval;

err_sys_creat:
	for (--attr_count; attr_count >= 0; attr_count--)
		sysfs_remove_file(enhance_kobj, &lcd_disp_attrs[attr_count].attr);

	kobject_put(enhance_kobj);
	return retval;
}

static void __exit nubia_disp_preference_exit(void)
{
	int attr_count = 0;

	for (attr_count = 0; attr_count < ARRAY_SIZE(lcd_disp_attrs); attr_count++)
		sysfs_remove_file(enhance_kobj, &lcd_disp_attrs[attr_count].attr);

	kobject_put(enhance_kobj);
}

MODULE_AUTHOR("NUBIA LCD Driver Team Software");
MODULE_DESCRIPTION("NUBIA LCD DISPLAY Color Saturation and Temperature Setting");
MODULE_LICENSE("GPL");
module_init(nubia_disp_preference_init);
module_exit(nubia_disp_preference_exit);
