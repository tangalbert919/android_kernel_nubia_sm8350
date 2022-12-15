/*
* aw9610x.c
*
* Copyright (c) 2020 AWINIC Technology CO., LTD
*
* Author: Bob <renxinghu@awinic.com>
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/string.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/miscdevice.h>
#include "aw_bin_parse.h"
#include "aw9610x.h"
#include "aw9610x_reg.h"

#define AW9610X_I2C_NAME "aw9610x_sar"
#define AW9610X_DRIVER_VERSION "v1.0.1"

#define AW_READ_CHIPID_RETRIES		(5)
#define AW_I2C_RETRIES			(5)
#define AW9610X_SCAN_DEFAULT_TIME	(10000)
#define CALI_FILE_MAX_SIZE		(128)

//yulei add
struct aw9610x * g_aw9610x = NULL;
//yulei add end
/******************************************************
*
* aw9610x i2c write/read
*
******************************************************/
static int32_t
i2c_write(struct aw9610x *aw9610x, uint16_t reg_addr16, uint32_t reg_data32)
{
	int32_t ret =  -ENOMEM;
	struct i2c_client *i2c = aw9610x->i2c;
	struct i2c_msg msg;
	uint8_t w_buf[6];

	/*reg_addr*/
	w_buf[0] = (u8)(reg_addr16 >> 8);
	w_buf[1] = (u8)(reg_addr16);
	/*data*/
	w_buf[2] = (u8)(reg_data32 >> 24);
	w_buf[3] = (u8)(reg_data32 >> 16);
	w_buf[4] = (u8)(reg_data32 >> 8);
	w_buf[5] = (u8)(reg_data32);

	msg.addr = i2c->addr;
	msg.flags = AW9610X_I2C_WR;
	msg.len = 6;
	/*2 bytes regaddr + 4 bytes data*/
	msg.buf = (unsigned char *)w_buf;

	ret = i2c_transfer(i2c->adapter, &msg, 1);
	if (ret < 0)
		AWLOGE(aw9610x->dev,
			"Write reg is 0x%x,error value = %d", reg_addr16, ret);

	return ret;
}

static int32_t
i2c_read(struct aw9610x *aw9610x, uint16_t reg_addr16, uint32_t *reg_data32)
{
	int32_t ret =  -ENOMEM;
	struct i2c_client *i2c = aw9610x->i2c;
	struct i2c_msg msg[2];
	uint8_t w_buf[2];
	uint8_t buf[4];

	w_buf[0] = (unsigned char)(reg_addr16 >> 8);
	w_buf[1] = (unsigned char)(reg_addr16);
	msg[0].addr = i2c->addr;
	msg[0].flags = AW9610X_I2C_WR;
	msg[0].len = 2;
	msg[0].buf = (unsigned char *)w_buf;

	msg[1].addr = i2c->addr;
	msg[1].flags = AW9610X_I2C_RD;
	msg[1].len = 4;
	msg[1].buf = (unsigned char *)buf;

	ret = i2c_transfer(i2c->adapter, msg, 2);
	if (ret < 0)
		AWLOGE(aw9610x->dev,
			"Read reg is 0x%x,error value = %d", reg_addr16, ret);

	reg_data32[0] = ((u32)buf[3]) | ((u32)buf[2]<<8) |
			((u32)buf[1]<<16) | ((u32)buf[0]<<24);

	return ret;
}

static int32_t aw9610x_i2c_write(struct aw9610x *aw9610x,
				uint16_t reg_addr16, uint32_t reg_data32)
{
	int32_t ret = -1;
	uint8_t cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_write(aw9610x, reg_addr16, reg_data32);
		if (ret < 0)
			AWLOGE(aw9610x->dev,
					"write cnt = %d,error = %d", cnt, ret);
		else
			break;

		cnt++;
	}

	return ret;
}

static int32_t aw9610x_i2c_read(struct aw9610x *aw9610x,
				uint16_t reg_addr16, uint32_t *reg_data32)
{
	int32_t ret = -1;
	uint8_t cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_read(aw9610x, reg_addr16, reg_data32);
		if (ret < 0)
			AWLOGE(aw9610x->dev,
					"i2c_read cnt=%d error=%d", cnt, ret);
		else
			break;
		cnt++;
	}

	return ret;
}

static int32_t
aw9610x_i2c_write_bits(struct aw9610x *aw9610x, uint16_t reg_addr16,
				uint32_t mask, uint32_t reg_data32)
{
	uint32_t reg_val;

	aw9610x_i2c_read(aw9610x, reg_addr16, &reg_val);
	reg_val &= mask;
	reg_val |= reg_data32;
	aw9610x_i2c_write(aw9610x, reg_addr16, reg_val);

	return 0;
}

/******************************************************************************
*
* aw9610x i2c sequential write/read --- one first addr with multiple data.
*
******************************************************************************/
static int32_t i2c_write_seq(struct aw9610x *aw9610x)
{
	int32_t ret =  -ENOMEM;
	struct i2c_client *i2c = aw9610x->i2c;
	struct i2c_msg msg;
	uint8_t w_buf[228];
	uint8_t addr_bytes = aw9610x->aw_i2c_package.addr_bytes;
	uint8_t msg_cnt = 0;
	uint8_t data_bytes = aw9610x->aw_i2c_package.data_bytes;
	uint8_t reg_num = aw9610x->aw_i2c_package.reg_num;
	uint8_t *p_reg_data = aw9610x->aw_i2c_package.p_reg_data;
	uint8_t msg_idx = 0;

	for (msg_idx = 0; msg_idx < addr_bytes; msg_idx++) {
		w_buf[msg_idx] = aw9610x->aw_i2c_package.init_addr[msg_idx];
		AWLOGI(aw9610x->dev, "w_buf_addr[%d] = 0x%02x",
						msg_idx, w_buf[msg_idx]);
	}
	msg_cnt = addr_bytes;
	for (msg_idx = 0; msg_idx < data_bytes * reg_num; msg_idx++) {
		w_buf[msg_cnt] = *p_reg_data++;
		msg_cnt++;
	}
	AWLOGD(aw9610x->dev, "%d reg_num = %d", msg_cnt, reg_num);
	p_reg_data = aw9610x->aw_i2c_package.p_reg_data;
	msg.addr = i2c->addr;
	msg.flags = AW9610X_I2C_WR;
	msg.len = msg_cnt;
	msg.buf = (uint8_t *)w_buf;
	ret = i2c_transfer(i2c->adapter, &msg, 1);
	if (ret < 0)
		AWLOGE(aw9610x->dev, "i2c write seq error %d", ret);

	return ret;
}

static int32_t i2c_read_seq(struct aw9610x *aw9610x, uint8_t *reg_data)
{
	int32_t ret =  -ENOMEM;
	struct i2c_client *i2c = aw9610x->i2c;
	struct i2c_msg msg[2];
	uint8_t w_buf[4];
	uint8_t buf[228];
	uint8_t data_bytes = aw9610x->aw_i2c_package.data_bytes;
	uint8_t reg_num = aw9610x->aw_i2c_package.reg_num;
	uint8_t addr_bytes = aw9610x->aw_i2c_package.addr_bytes;
	uint8_t msg_idx = 0;
	uint8_t msg_cnt = 0;

	/*
	* step 1 : according to addr_bytes assemble first_addr.
	* step 2 : initialize msg[0] including first_addr transfer to client.
	* step 3 : wait for client return reg_data.
	*/
	for (msg_idx = 0; msg_idx < addr_bytes; msg_idx++) {
		w_buf[msg_idx] = aw9610x->aw_i2c_package.init_addr[msg_idx];
		AWLOGD(aw9610x->dev, "w_buf_addr[%d] = 0x%02x",
					msg_idx, w_buf[msg_idx]);
	}
	msg[0].addr = i2c->addr;
	msg[0].flags = AW9610X_I2C_WR;
	msg[0].len = msg_idx;
	msg[0].buf = (uint8_t *)w_buf;

	/*
	* recieve client to msg[1].buf.
	*/
	msg_cnt = data_bytes * reg_num;
	msg[1].addr = i2c->addr;
	msg[1].flags = AW9610X_I2C_RD;
	msg[1].len = msg_cnt;
	msg[1].buf = (uint8_t *)buf;

	ret = i2c_transfer(i2c->adapter, msg, 2);
	if (ret < 0) {
		AWLOGE(aw9610x->dev, "i2c write error %d", ret);
		return ret;
	}

	for (msg_idx = 0; msg_idx < msg_cnt; msg_idx++) {
		reg_data[msg_idx] = buf[msg_idx];
		AWLOGD(aw9610x->dev, "buf = 0x%02x", buf[msg_idx]);
	}

	return ret;
}

static void
aw9610x_addrblock_load(struct device *dev, const char *buf)
{
	uint32_t addrbuf[4] = { 0 };
	uint8_t temp_buf[2] = { 0 };
	uint32_t i = 0;
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	uint8_t addr_bytes = aw9610x->aw_i2c_package.addr_bytes;
	uint8_t reg_num = aw9610x->aw_i2c_package.reg_num;

	for (i = 0; i < addr_bytes; i++) {
		if (reg_num < attr_buf[1]) {
			temp_buf[0] = buf[attr_buf[0] + i * 5];
			temp_buf[1] = buf[attr_buf[0] + i * 5 + 1];
		} else if (reg_num >= attr_buf[1] && reg_num < attr_buf[3]) {
			temp_buf[0] = buf[attr_buf[2] + i * 5];
			temp_buf[1] = buf[attr_buf[2] + i * 5 + 1];
		} else if (reg_num >= attr_buf[3] && reg_num < attr_buf[5]) {
			temp_buf[0] = buf[attr_buf[4] + i * 5];
			temp_buf[1] = buf[attr_buf[4] + i * 5 + 1];
		}
		if (sscanf(temp_buf, "%02x", &addrbuf[i]) == 1)
			aw9610x->aw_i2c_package.init_addr[i] =
							(uint8_t)addrbuf[i];
	}
}

/******************************************************
 *
 *the document of storage_spedata
 *
 ******************************************************/
static int32_t aw9610x_filedata_deal(struct aw9610x *aw9610x)
{
	struct file *fp = NULL;
	mm_segment_t fs;
	int8_t *buf;
	int8_t temp_buf[8] = { 0 };
	uint8_t i = 0;
	uint8_t j = 0;
	int32_t ret;
	uint32_t nv_flag = 0;
	uint8_t cali_file_name[20] = { 0 };

	snprintf(cali_file_name, sizeof(cali_file_name), "aw_cali_%d.bin", aw9610x->sar_num);
	AWLOGI(aw9610x->dev, "cali_file_name : %s", cali_file_name);

	fp = filp_open(cali_file_name, O_RDWR | O_CREAT, 0644);
	if (IS_ERR(fp)) {
		AWLOGE(aw9610x->dev, "open failed!");
		return -EINVAL;
	}

	fs = get_fs();
	set_fs(KERNEL_DS);
	buf = (char *)kzalloc(CALI_FILE_MAX_SIZE, GFP_KERNEL);
	if (!buf) {
		AWLOGE(aw9610x->dev, "malloc failed!");
		filp_close(fp, NULL);
		set_fs(fs);
		return -EINVAL;
	}

	ret = vfs_read(fp, buf, CALI_FILE_MAX_SIZE, &(fp->f_pos));
	if (ret < 0) {
		AWLOGE(aw9610x->dev, "read failed");
		set_fs(fs);
		aw9610x->cali_flag = AW_CALI;
		return ret;
	} else if (ret == 0) {
		AWLOGE(aw9610x->dev, "read len = 0");
		set_fs(fs);
		aw9610x->cali_flag = AW_CALI;
		return ret;
	} else {
		for (i = 0; i < AW_SPE_REG_NUM; i++) {
			for (j = 0; j < AW_SPE_REG_DWORD; j++)
				temp_buf[j] = buf[AW_SPE_REG_DWORD * i + j];

			if (sscanf(temp_buf, "%08x",
					&aw9610x->nvspe_data[i]) == 1)
				AWLOGD(aw9610x->dev,
						"nv_spe_data[%d] = 0x%08x",
						i, aw9610x->nvspe_data[i]);
			}
	}

	set_fs(fs);
	filp_close(fp, NULL);
	kfree(buf);

	/* nvspe_datas come from nv*/
	for (i = 0; i < AW_SPE_REG_NUM; i++) {
		nv_flag |= aw9610x->nvspe_data[i];
		if (nv_flag != 0)
			break;
	}

	if (nv_flag == 0) {
		aw9610x->cali_flag = AW_CALI;
		AWLOGI(aw9610x->dev,
			"the chip need to cali! nv_flag = 0x%08x", nv_flag);
	} else {
		aw9610x->cali_flag = AW_NO_CALI;
		AWLOGI(aw9610x->dev,
			"chip not need to cali! nv_flag = 0x%08x", nv_flag);
	}

	return 0;
}

static int32_t
aw9610x_store_spedata_to_file(struct aw9610x *aw9610x, char *buf)
{
	struct file *fp = NULL;
	loff_t pos = 0;
	mm_segment_t fs;
	uint8_t cali_file_name[20] = { 0 };

	AWLOGD(aw9610x->dev, "buf = %s", buf);

	snprintf(cali_file_name, 20, "aw_cali_%d.bin", aw9610x->sar_num);
	AWLOGI(aw9610x->dev, "cali_file_name : %s", cali_file_name);

	fp = filp_open(cali_file_name, O_RDWR | O_CREAT, 0644);
	if (IS_ERR(fp)) {
		AWLOGE(aw9610x->dev, "open failed!");
		return -EINVAL;
	}

	fs = get_fs();
	set_fs(KERNEL_DS);

	vfs_write(fp, buf, strlen(buf), &pos);

	set_fs(fs);

	AWLOGI(aw9610x->dev, "write successfully!");

	filp_close(fp, NULL);
	return 0;
}

/******************************************************
 *
 *configuration of special reg
 *
 ******************************************************/
static void aw9610x_get_calidata(struct aw9610x *aw9610x)
{
	uint8_t i = 0;
	uint32_t buf_size = 0;
	int32_t ret;
	uint32_t reg_val = 0;
	uint8_t temp_buf[9] = { 0 };
	uint8_t buf[CALI_FILE_MAX_SIZE] = { 0 };

	AWLOGD(aw9610x->dev, "enter");

	/*class 1 special reg*/
	for (i = 0; i < AW_CLA1_SPE_REG_NUM; i++)
		aw9610x_i2c_read(aw9610x,
		REG_AFECFG1_CH0 + i * AW_CL1SPE_CALI_OS, &aw9610x->spedata[i]);

	/*class 2 special reg*/
	for (; i < AW_SPE_REG_NUM; i++)
		aw9610x_i2c_read(aw9610x,
			REG_REFACFG + (i - AW_CHANNEL_MAX) *
				AW_CL2SPE_CALI_OS, &aw9610x->spedata[i]);

	for (i = AW_CLA1_SPE_REG_NUM; i < AW_SPE_REG_NUM; i++) {
		ret = aw9610x->spedata[i] & 0x07;
		switch (ret) {
		case AW_CHANNEL0:
			aw9610x_i2c_read(aw9610x, REG_VALID_CH0,
							&reg_val);
			break;
		case AW_CHANNEL1:
			aw9610x_i2c_read(aw9610x, REG_VALID_CH1,
							&reg_val);
			break;
		case AW_CHANNEL2:
			aw9610x_i2c_read(aw9610x, REG_VALID_CH2,
							&reg_val);
			break;
		case AW_CHANNEL3:
			aw9610x_i2c_read(aw9610x, REG_VALID_CH3,
							&reg_val);
			break;
		case AW_CHANNEL4:
			aw9610x_i2c_read(aw9610x, REG_VALID_CH4,
							&reg_val);
			break;
		case AW_CHANNEL5:
			aw9610x_i2c_read(aw9610x, REG_VALID_CH5,
							&reg_val);
			break;
		default:
			return;
		}
		aw9610x->spedata[i] = ((reg_val >> 6) & 0x03fffff0) |
					(aw9610x->spedata[i] & 0xfc00000f);
	}
	/* spedatas come from register*/

	/* write spedatas to nv */
	for (i = 0; i < AW_SPE_REG_NUM; i++) {
		snprintf(temp_buf, sizeof(temp_buf), "%08x",
							aw9610x->spedata[i]);
		memcpy(buf + buf_size, temp_buf, strlen(temp_buf));
		buf_size = strlen(buf);
	}
	ret = aw9610x_store_spedata_to_file(aw9610x, buf);
	if (ret < 0) {
		AWLOGE(aw9610x->dev, "store spedata failed");
		return;
	}

	AWLOGD(aw9610x->dev, "successfully write_spereg_to_file");
}

static void aw9610x_class1_reg(struct aw9610x *aw9610x)
{
	int32_t i = 0;
	uint32_t reg_val;

	AWLOGD(aw9610x->dev, "enter");

	for (i = 0; i < AW_CLA1_SPE_REG_NUM; i++) {
		reg_val = (aw9610x->nvspe_data[i] >> 16) & 0x0000ffff;
		aw9610x_i2c_write_bits(aw9610x, REG_INITPROX0_CH0 +
				i * AW_CL1SPE_DEAL_OS, ~(0xffff), reg_val);
	}
}

static void aw9610x_class2_reg(struct aw9610x *aw9610x)
{
	int32_t i = 0;

	AWLOGD(aw9610x->dev, "enter");

	for (i = AW_CLA1_SPE_REG_NUM; i < AW_SPE_REG_NUM; i++) {
		aw9610x_i2c_write(aw9610x,
			REG_REFACFG + (i - AW_CLA1_SPE_REG_NUM) * AW_CL2SPE_DEAL_OS,
			aw9610x->nvspe_data[i]);
	}
}

static void aw9610x_spereg_deal(struct aw9610x *aw9610x)
{
	AWLOGD(aw9610x->dev, "enter!");

	aw9610x_class1_reg(aw9610x);
	aw9610x_class2_reg(aw9610x);
}

static void aw9610x_datablock_load(struct device *dev, const char *buf)
{
	uint32_t i = 0;
	uint8_t reg_data[220] = { 0 };
	uint32_t databuf[220] = { 0 };
	uint8_t temp_buf[2] = { 0 };
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	uint8_t addr_bytes = aw9610x->aw_i2c_package.addr_bytes;
	uint8_t data_bytes = aw9610x->aw_i2c_package.data_bytes;
	uint8_t reg_num = aw9610x->aw_i2c_package.reg_num;

	for (i = 0; i < data_bytes * reg_num; i++) {
		if (reg_num < attr_buf[1]) {
			temp_buf[0] = buf[attr_buf[0] + (addr_bytes + i) * 5];
			temp_buf[1] =
				buf[attr_buf[0] + (addr_bytes + i) * 5 + 1];
		} else if (reg_num >= attr_buf[1] && reg_num < attr_buf[3]) {
			temp_buf[0] = buf[attr_buf[2] + (addr_bytes + i) * 5];
			temp_buf[1] =
				buf[attr_buf[2] + (addr_bytes + i) * 5 + 1];
		} else if (reg_num >= attr_buf[3] && reg_num < attr_buf[5]) {
			temp_buf[0] = buf[attr_buf[4] + (addr_bytes + i) * 5];
			temp_buf[1] =
				buf[attr_buf[4] + (addr_bytes + i) * 5 + 1];
		}
		sscanf(temp_buf, "%02x", &databuf[i]);
		reg_data[i] = (uint8_t)databuf[i];
	}
	aw9610x->aw_i2c_package.p_reg_data = reg_data;
	i2c_write_seq(aw9610x);
}

static void aw9610x_power_on_prox_detection(struct aw9610x *aw9610x)
{
	int32_t ret = 0;
	uint32_t reg_data = 0;
	uint32_t temp_time = AW9610X_SCAN_DEFAULT_TIME;

	AWLOGD(aw9610x->dev, "enten");

	ret = aw9610x_filedata_deal(aw9610x);
	if ((aw9610x->cali_flag == AW_NO_CALI) && (ret >= 0))
		aw9610x_spereg_deal(aw9610x);

	aw9610x_i2c_write(aw9610x, REG_HOSTIRQEN, 0);
	aw9610x_i2c_write(aw9610x, REG_CMD, 0x0001);
	while ((temp_time)--) {
		aw9610x_i2c_read(aw9610x, REG_HOSTIRQSRC, &reg_data);
		reg_data = (reg_data >> 4) & 0x01;
		if (reg_data == 1) {
			AWLOGI(aw9610x->dev,
				"time = %d", temp_time);
			if ((aw9610x->cali_flag == AW_CALI) && ret >= 0)
				aw9610x_get_calidata(aw9610x);
			break;
		}
		msleep(1);
	}
	aw9610x_i2c_read(aw9610x, REG_STAT2, &reg_data);
	if (reg_data & 0x10000)
		aw9610x->power_prox = 1;
}

static void aw9610x_channel_scan_start(struct aw9610x *aw9610x)
{
	AWLOGD(aw9610x->dev, "enter");
	if (aw9610x->pwprox_dete == true) {
		 aw9610x_power_on_prox_detection(aw9610x);
	} else {
		aw9610x_i2c_write(aw9610x, REG_CMD, AW9610X_ACTIVE_MODE);
	}

	aw9610x_i2c_write(aw9610x, REG_HOSTIRQEN, aw9610x->hostirqen);
	aw9610x->mode = AW9610X_ACTIVE_MODE;
	aw9610x->old_mode = AW9610X_ACTIVE_MODE;
}

static void aw9610x_bin_valid_loaded(struct aw9610x *aw9610x,
						struct aw_bin *aw_bin_data_s)
{
	uint32_t i;
	int32_t ret = 0;
	uint16_t reg_addr;
	uint32_t reg_data;
	uint32_t start_addr = aw_bin_data_s->header_info[0].valid_data_addr;

	for (i = 0; i < aw_bin_data_s->header_info[0].valid_data_len;
						i += 6, start_addr += 6) {
		reg_addr = (aw_bin_data_s->info.data[start_addr]) |
				aw_bin_data_s->info.data[start_addr + 1] << 8;
		reg_data = aw_bin_data_s->info.data[start_addr + 2] |
			(aw_bin_data_s->info.data[start_addr + 3] << 8) |
			(aw_bin_data_s->info.data[start_addr + 4] << 16) |
			(aw_bin_data_s->info.data[start_addr + 5] << 24);
		if ((reg_addr == REG_EEDA0) || (reg_addr == REG_EEDA1))
			continue;
		if (reg_addr == REG_HOSTIRQEN) {
			aw9610x->hostirqen = reg_data;
			continue;
		}
		ret = aw9610x_i2c_write(aw9610x, reg_addr, reg_data);
		if (ret < 0)
			return ;

		AWLOGI(aw9610x->dev,
			"reg_addr = 0x%04x, reg_data = 0x%08x",
					reg_addr, reg_data);
	}
	AWLOGI(aw9610x->dev, "bin writen completely");

	aw9610x_channel_scan_start(aw9610x);
}

/***************************************************************************
* para loaded
****************************************************************************/
static int32_t aw9610x_para_loaded(struct aw9610x *aw9610x)
{
	int32_t i = 0;
	int32_t len = ARRAY_SIZE(aw9610x_reg_default);

	AWLOGD(aw9610x->dev, "start to download para!");

	for (i = 0; i < len; i = i + 2) {
		aw9610x_i2c_write(aw9610x,
				(uint16_t)aw9610x_reg_default[i],
				aw9610x_reg_default[i+1]);
		if (aw9610x_reg_default[i] == REG_HOSTIRQEN)
			aw9610x->hostirqen = aw9610x_reg_default[i+1];
		AWLOGI(aw9610x->dev, "reg_addr = 0x%04x, reg_data = 0x%08x",
						aw9610x_reg_default[i],
						aw9610x_reg_default[i+1]);
	}
	AWLOGI(aw9610x->dev, "para writen completely");

	aw9610x_channel_scan_start(aw9610x);

	return 0;
}

static void
aw9610x_cfg_all_loaded(const struct firmware *cont, void *context)
{
	int32_t ret;
	struct aw_bin *aw_bin;
	struct aw9610x *aw9610x = context;

	AWLOGD(aw9610x->dev, "enter");

	if (!cont) {
		AWLOGE(aw9610x->dev, "%s request failed", aw9610x->cfg_name);
		release_firmware(cont);
		return;
	} else {
		AWLOGI(aw9610x->dev,
			"%s request successfully", aw9610x->cfg_name);
	}

	aw_bin = kzalloc(cont->size + sizeof(struct aw_bin), GFP_KERNEL);
	if (!aw_bin) {
		kfree(aw_bin);
		release_firmware(cont);
		AWLOGE(aw9610x->dev, "failed to allcating memory!");
		return;
	}
	aw_bin->info.len = cont->size;
	memcpy(aw_bin->info.data, cont->data, cont->size);
	ret = aw_parsing_bin_file(aw_bin);
	if (ret < 0) {
		AWLOGE(aw9610x->dev, "[:aw9610x parse bin fail! ret = %d", ret);
		kfree(aw_bin);
		release_firmware(cont);
		return;
	}

	ret = strcmp(aw9610x->chip_name, aw_bin->header_info[0].chip_type);
	if (ret != 0) {
		AWLOGE(aw9610x->dev,
			"chip name(%s) incompatible with chip type(%s)",
			aw9610x->chip_name, aw_bin->header_info[0].chip_type);

		kfree(aw_bin);
		release_firmware(cont);
		return;
	}

	aw9610x_bin_valid_loaded(aw9610x, aw_bin);
	kfree(aw_bin);
	release_firmware(cont);
}

static int32_t aw9610x_cfg_update(struct aw9610x *aw9610x)
{
	AWLOGD(aw9610x->dev, "enter");

	if (aw9610x->firmware_flag == true) {
		snprintf(aw9610x->cfg_name, sizeof(aw9610x->cfg_name),
					"aw9610x_%d.bin", aw9610x->sar_num);

		request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
							aw9610x->cfg_name,
							aw9610x->dev,
							GFP_KERNEL,
							aw9610x,
							aw9610x_cfg_all_loaded);
	} else {
		aw9610x_para_loaded(aw9610x);
	}

	return AW_SAR_SUCCESS;
}

static void aw9610x_cfg_work_routine(struct work_struct *work)
{
	struct aw9610x
		*aw9610x = container_of(work, struct aw9610x, cfg_work.work);

	AWLOGD(aw9610x->dev, "enter");

	aw9610x_cfg_update(aw9610x);
}

static int32_t
aw9610x_sar_cfg_init(struct aw9610x *aw9610x, int32_t flag)
{
	uint32_t cfg_timer_val = 0;
	uint32_t i = 0;

	AWLOGD(aw9610x->dev, "enter");

	if (flag == AW_CFG_LOADED)
		cfg_timer_val = 20;
	else if (flag == AW_CFG_UNLOAD)
		cfg_timer_val = 5000;
	else
		return -AW_CFG_LOAD_TIME_FAILED;

	INIT_DELAYED_WORK(&aw9610x->cfg_work, aw9610x_cfg_work_routine);
	schedule_delayed_work(&aw9610x->cfg_work,
					msecs_to_jiffies(cfg_timer_val));

	for (i = 0; i < AW_CHANNEL_MAX; i++) {
		aw9610x->aw_channel[i].curr_state = 0;
		aw9610x->aw_channel[i].last_state = 0;
	}

	return AW_SAR_SUCCESS;
}

/*****************************************************
 *
 * first irq clear
 *
 *****************************************************/
static int32_t aw9610x_init_irq_handle(struct aw9610x *aw9610x)
{
	uint8_t cnt = 20;
	uint32_t reg_data;
	uint32_t trim0 = 0;
	uint32_t trim1 = 0;

	AWLOGD(aw9610x->dev, "enter");

	while (cnt--) {
		aw9610x_i2c_read(aw9610x, REG_HOSTIRQSRC, &reg_data);
		aw9610x->first_irq_flag = reg_data & 0x01;
		if (aw9610x->first_irq_flag == 1) {
			aw9610x_i2c_read(aw9610x, REG_EEDA0, &trim0);
			aw9610x_i2c_read(aw9610x, REG_EEDA1, &trim1);
			if ((trim0 + trim1) == 0) {
				AWLOGE(aw9610x->dev, "aw9610x trim error");
				return -AW_TRIM_ERROR;
			}
			AWLOGD(aw9610x->dev, "cnt = %d", cnt);
			return AW_SAR_SUCCESS;
		}
	}
	AWLOGE(aw9610x->dev, "hardware has trouble!");

	return -AW_IRQIO_FAILED;
}

/*****************************************************
 *
 * software reset
 *
 *****************************************************/
static void aw9610x_sw_reset(struct aw9610x *aw9610x)
{
	AWLOGD(aw9610x->dev, "enter");

	aw9610x_i2c_write(aw9610x, REG_HOSTCTRL2, 0);
	msleep(20);
}

static int32_t aw9610x_baseline_filter(struct aw9610x *aw9610x)
{
	int32_t ret = 0;
	uint8_t i = 0;
	uint32_t status0 = 0;
	uint32_t status1 = 0;

	ret = aw9610x_i2c_read(aw9610x, REG_STAT1, &status1);
	if (ret < 0)
		return ret;
	ret = aw9610x_i2c_read(aw9610x, REG_STAT0, &status0);
	if (ret < 0)
		return ret;

	for (i = 0; i < AW_CHANNEL_MAX; i++) {
		if (((status1 >> i) & 0x01) == 1) {
			if (aw9610x->satu_flag[i] == 0) {
				ret = aw9610x_i2c_read(aw9610x,
					REG_BLFILT1_CH0 + i * AW_CL1SPE_DEAL_OS,
					&aw9610x->satu_data[i]);
				if (ret < 0)
					return ret;
				ret = aw9610x_i2c_write(aw9610x,
				REG_BLFILT1_CH0 + i * AW_CL1SPE_DEAL_OS,
				((aw9610x->satu_data[i] | 0x1fc) & 0x3fffffff));
				if (ret < 0)
					return ret;
				aw9610x->satu_flag[i] = 1;
			}
		} else if (((status1 >> i) & 0x01) == 0) {
			if (aw9610x->satu_flag[i] == 1) {
				if (((status0 >> (i + 24)) & 0x01) == 0) {
					ret = aw9610x_i2c_write(aw9610x,
					REG_BLFILT1_CH0 + i * AW_CL1SPE_DEAL_OS,
					aw9610x->satu_data[i]);
					if (ret < 0)
						return ret;
					aw9610x->satu_flag[i] = 0;
				}
			}
		}
	}

	return ret;
}

static void aw9610x_saturat_release_handle(struct aw9610x *aw9610x)
{
	uint32_t satu_irq = 0;
	uint8_t i = 0;
	int32_t ret = 0;
	uint32_t status0 = 0;

	AWLOGD(aw9610x->dev, "enter");

	satu_irq = (aw9610x->irq_status >> 7) & 0x01;
	if (satu_irq == 1) {
		ret = aw9610x_baseline_filter(aw9610x);
		if (ret < 0)
			return;
	} else {
		ret = aw9610x_i2c_read(aw9610x, REG_STAT0, &status0);
		if (ret < 0)
			return;
		for (i = 0; i < AW_CHANNEL_MAX; i++) {
			if (aw9610x->satu_flag[i] == 1) {
				if (((status0 >> (i + 24)) & 0x01) == 0) {
					ret = aw9610x_i2c_write(aw9610x,
					REG_BLFILT1_CH0 + i * AW_CL1SPE_DEAL_OS,
					aw9610x->satu_data[i]);
					if (ret < 0)
						return;
					aw9610x->satu_flag[i] = 0;
				}
			}
		}
	}

	AWLOGI(aw9610x->dev, "satu_irq handle over!");
}

/******************************************************
 *
 * sys group attribute
 *
 ******************************************************/
static ssize_t aw9610x_set_reg(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	uint32_t databuf[2] = { 0, 0 };

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2)
		aw9610x_i2c_write(aw9610x, (uint16_t)databuf[0],
							(uint32_t)databuf[1]);

	return count;
}

static ssize_t aw9610x_get_reg(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint32_t i = 0;
	uint32_t reg_val = 0;
	uint32_t reg_num = 0;

	reg_num = ARRAY_SIZE(aw9610x_reg_access);
	for (i = 0; i < reg_num; i++) {
		if (aw9610x_reg_access[i].rw & REG_RD_ACCESS) {
			aw9610x_i2c_read(aw9610x, aw9610x_reg_access[i].reg,
								&reg_val);
			len += snprintf(buf + len, PAGE_SIZE - len,
						"reg:0x%04x=0x%08x\n",
						aw9610x_reg_access[i].reg,
						reg_val);
		}
	}

	return len;
}

static ssize_t aw9610x_valid_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint8_t i = 0;
	int32_t reg_val = 0;

	for (i = 0; i < AW_CHANNEL_MAX; i++) {
		aw9610x_i2c_read(aw9610x, REG_VALID_CH0 + i * 4, &reg_val);
		reg_val /= AW_DATA_PROCESS_FACTOR;
		len += snprintf(buf+len, PAGE_SIZE-len, "VALID_CH%d = %d\n", i,
								reg_val);
	}

	return len;
}

static ssize_t aw9610x_baseline_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint8_t i = 0;
	int32_t reg_val = 0;

	for (i = 0; i < AW_CHANNEL_MAX; i++) {
		aw9610x_i2c_read(aw9610x, REG_BASELINE_CH0 + i * 4, &reg_val);
		reg_val /= AW_DATA_PROCESS_FACTOR;
		len += snprintf(buf+len, PAGE_SIZE-len, "BASELINE_CH%d = %d\n",
								i, reg_val);
	}

	return len;
}

static ssize_t aw9610x_diff_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint8_t i = 0;
	int32_t reg_val = 0;

	for (i = 0; i < AW_CHANNEL_MAX; i++) {
		aw9610x_i2c_read(aw9610x, REG_DIFF_CH0 + i * 4, &reg_val);
		reg_val /= AW_DATA_PROCESS_FACTOR;
		len += snprintf(buf+len, PAGE_SIZE-len, "DIFF_CH%d = %d\n", i,
								reg_val);
	}

	return len;
}

static ssize_t aw9610x_raw_data_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint8_t i = 0;
	int32_t reg_val = 0;

	for (i = 0; i < AW_CHANNEL_MAX; i++) {
		aw9610x_i2c_read(aw9610x, REG_RAW_CH0 + i * 4, &reg_val);
		reg_val /= AW_DATA_PROCESS_FACTOR;
		len += snprintf(buf+len, PAGE_SIZE-len, "RAW_DATA_CH%d = %d\n",
								i, reg_val);
	}

	return len;
}

static ssize_t aw9610x_psc_data_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint8_t i = 0;
	int32_t reg_val = 0;

	for (i = 0; i < AW_CHANNEL_MAX; i++) {
		aw9610x_i2c_read(aw9610x, REG_PSCBD_CH0 + i * 4, &reg_val);
		reg_val /= AW_DATA_PROCESS_FACTOR;
		len += snprintf(buf+len, PAGE_SIZE-len, "PSC_DATA_CH%d = %d\n",
								i, reg_val);
	}

	return len;
}

static ssize_t aw9610x_parasitic_data_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	ssize_t len = 0;
	uint8_t i = 0;
	uint32_t reg_val = 0;
	uint32_t coff_data = 0;
	uint32_t coff_data_int = 0;
	uint32_t coff_data_dec = 0;
	uint8_t temp_data[20] = { 0 };

	for (i = 0; i < AW_CHANNEL_MAX; i++) {
		aw9610x_i2c_read(aw9610x,
			REG_AFECFG1_CH0 + i * AW_CL1SPE_CALI_OS, &reg_val);
		coff_data = (reg_val >> 24) * 900 +
						((reg_val >> 16) & 0xff) * 13;
		coff_data_int = coff_data / 1000;
		coff_data_dec = coff_data % 1000;
		snprintf(temp_data, sizeof(temp_data), "%d.%d", coff_data_int,
								coff_data_dec);
		len += snprintf(buf+len, PAGE_SIZE-len,
				"PARASITIC_DATA_CH%d = %s pf\n", i, temp_data);
	}

	return len;
}

static ssize_t aw9610x_awrw_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	uint8_t reg_data[228] = { 0 };
	uint8_t i = 0;
	ssize_t len = 0;
	uint8_t reg_num = aw9610x->aw_i2c_package.reg_num;
	uint8_t data_bytes = aw9610x->aw_i2c_package.data_bytes;

	i2c_read_seq(aw9610x, reg_data);
	for (i = 0; i < reg_num * data_bytes; i++)
		len += snprintf(buf + len, PAGE_SIZE - len,
						"0x%02x,", reg_data[i]);

	len += snprintf(buf + len - 1, PAGE_SIZE - len, "\n");

	return len;
}

static ssize_t aw9610x_factory_cali_set(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	uint32_t databuf[1] = { 0 };

	if (sscanf(buf, "%d", &databuf[0]) == 1) {
		if ((databuf[0] == 1) && (aw9610x->pwprox_dete == true)) {
			aw9610x_get_calidata(aw9610x);
		} else {
			AWLOGE(aw9610x->dev, "aw_unsupport the pw_prox_dete=%d",
						aw9610x->pwprox_dete);
			return count;
		}
		aw9610x_sw_reset(aw9610x);
		aw9610x->cali_flag = AW_NO_CALI;
		aw9610x_sar_cfg_init(aw9610x, AW_CFG_LOADED);
	}

	return count;
}

static ssize_t aw9610x_power_prox_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	ssize_t len = 0;

	if (aw9610x->pwprox_dete == false) {
		len += snprintf(buf + len, PAGE_SIZE - len,
							"unsupport powerprox!");
		return len;
	}

	len += snprintf(buf + len, PAGE_SIZE - len, "power_prox: ");
	len += snprintf(buf + len, PAGE_SIZE - len, "%d\n",
							aw9610x->power_prox);

	return len;
}

static ssize_t aw9610x_awrw_set(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	uint32_t datatype[3] = { 0 };

	if (sscanf(buf, "%d %d %d", &datatype[0], &datatype[1],
							&datatype[2]) == 3) {
		aw9610x->aw_i2c_package.addr_bytes = (uint8_t)datatype[0];
		aw9610x->aw_i2c_package.data_bytes = (uint8_t)datatype[1];
		aw9610x->aw_i2c_package.reg_num = (uint8_t)datatype[2];

		aw9610x_addrblock_load(dev, buf);
		if (count > 7 + 5 * aw9610x->aw_i2c_package.addr_bytes)
			aw9610x_datablock_load(dev, buf);
	}

	return count;
}

static ssize_t aw9610x_set_update(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	ssize_t ret;
	uint32_t state;
	int32_t cfg_timer_val = 10;
	struct aw9610x *aw9610x = dev_get_drvdata(dev);

	ret = kstrtouint(buf, 10, &state);
	if (ret) {
		AWLOGE(aw9610x->dev, "fail to set update");
		return ret;
	}
	if (state) {
		aw9610x_i2c_write(aw9610x, REG_HOSTIRQEN, 0);
		aw9610x_sw_reset(aw9610x);
		schedule_delayed_work(&aw9610x->cfg_work,
					msecs_to_jiffies(cfg_timer_val));
	}

	return count;
}

static ssize_t aw9610x_aot_cali_set(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	ssize_t ret;
	uint32_t state;
	uint32_t data_en = 0;
	struct aw9610x *aw9610x = dev_get_drvdata(dev);

	ret = kstrtouint(buf, 10, &state);
	AWLOGE(aw9610x->dev, "to set1 aot cali");
	if (ret) {
		AWLOGE(aw9610x->dev, "fail to set aot cali");
		return ret;
	}
	aw9610x_i2c_read(aw9610x, REG_SCANCTRL0, &data_en);

	if (state != 0){
		aw9610x_i2c_write_bits(aw9610x, REG_SCANCTRL0, ~(0x3f << 8),
							(data_en & 0x3f) << 8);
	AWLOGE(aw9610x->dev, " to set2 aot cali");
	}
	else{
	AWLOGE(aw9610x->dev, "fail to set aot cali");
	}
	return count;
}

static ssize_t aw9610x_get_satu(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	ssize_t len = 0;

	if (aw9610x->satu_release != 0)
		len += snprintf(buf + len, PAGE_SIZE - len,
			"satu_ralease function is supporting! the flag = %d\n",
							aw9610x->satu_release);
	else
		len += snprintf(buf + len, PAGE_SIZE - len,
			"satu_ralease function unsupport! the flag = %d\n",
							aw9610x->satu_release);

	return len;
}

static ssize_t aw9610x_set_satu(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	ssize_t ret;
	uint32_t state;
	struct aw9610x *aw9610x = dev_get_drvdata(dev);

	ret = kstrtouint(buf, 10, &state);
	if (ret) {
		AWLOGE(aw9610x->dev, "fail to set satu");
		return ret;
	}
	if (state && (aw9610x->vers == AW9610X)) {
		aw9610x_saturat_release_handle(aw9610x);
		aw9610x->satu_release = AW9610X_FUNC_ON;
	} else {
		aw9610x->satu_release = AW9610X_FUNC_OFF;
	}

	return count;
}

static ssize_t aw9610x_operation_mode_set(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	ssize_t ret = 0;
	uint32_t irq_status_temp = 0;
	uint32_t stat;
	struct aw9610x *aw9610x = dev_get_drvdata(dev);

	ret = kstrtouint(buf, 10, &aw9610x->mode);
	if (ret) {
		AWLOGE(aw9610x->dev, "fail to set operation mode");
		return ret;
	}
    pr_info("%s: aw9610x_operation = 0x%04x\n", __func__, aw9610x->mode);
	if (aw9610x->mode == AW9610X_ACTIVE_MODE &&
				aw9610x->old_mode != AW9610X_ACTIVE_MODE) {
		if (aw9610x->old_mode == AW9610X_DEEPSLEEP_MODE) {
			aw9610x_i2c_write(aw9610x, REG_HOSTCTRL1,
							AW9610X_CPU_WORK_MASK);
			enable_irq(aw9610x->to_irq);
		}
		if (aw9610x->old_mode == AW9610X_SLEEP_MODE)
			enable_irq(aw9610x->to_irq);
		aw9610x_i2c_write(aw9610x, REG_CMD, AW9610X_ACTIVE_MODE);
	} else if (aw9610x->mode == AW9610X_SLEEP_MODE &&
				aw9610x->old_mode != AW9610X_SLEEP_MODE) {
		if (aw9610x->old_mode == AW9610X_DEEPSLEEP_MODE) {
			aw9610x_i2c_write(aw9610x, REG_HOSTCTRL1,
							AW9610X_CPU_WORK_MASK);
		} else {
			disable_irq(aw9610x->to_irq);
			/***interrupt read clear ***/
			aw9610x_i2c_read(aw9610x, REG_HOSTIRQSRC, &irq_status_temp);
			aw9610x_i2c_read(aw9610x, REG_STAT0, &stat);
			if((stat >> 24) != 0){
			input_report_key(aw9610x->aw_channel[0].input, KEY_F6, 0);
            input_sync(aw9610x->aw_channel[0].input);		
			}
		}
		aw9610x_i2c_write(aw9610x, REG_CMD, AW9610X_SLEEP_MODE);
	} else if ((aw9610x->mode == AW9610X_DEEPSLEEP_MODE) &&
		  aw9610x->old_mode != AW9610X_DEEPSLEEP_MODE &&
				 ((aw9610x->vers == AW9610XA) ||
					(aw9610x->vers == AW9610XB))) {
		if (aw9610x->old_mode != AW9610X_SLEEP_MODE) {
			disable_irq(aw9610x->to_irq);
			aw9610x_i2c_read(aw9610x, REG_HOSTIRQSRC, &irq_status_temp);
		}
		if (aw9610x->vers == AW9610XB)
			aw9610x_i2c_write(aw9610x, REG_CMD, AW9610XB_DEEPSLEEP_MODE);
		else
			aw9610x_i2c_write(aw9610x, REG_CMD, AW9610X_DEEPSLEEP_MODE);
	} else {
		AWLOGE(aw9610x->dev, "failed to operation mode!");
		return aw9610x->mode;
	}
	aw9610x->old_mode = aw9610x->mode;

	return count;
}

static ssize_t aw9610x_operation_mode_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	ssize_t len = 0;

	if (aw9610x->mode == AW9610X_ACTIVE_MODE)
		len += snprintf(buf + len, PAGE_SIZE - len,
						"operation mode: Active\n");
	else if (aw9610x->mode == AW9610X_SLEEP_MODE)
		len += snprintf(buf + len, PAGE_SIZE - len,
						"operation mode: Sleep\n");
	else if ((aw9610x->mode == AW9610X_DEEPSLEEP_MODE) &&
				((aw9610x->vers == AW9610XA) ||
					(aw9610x->vers == AW9610XB)))
		len += snprintf(buf + len, PAGE_SIZE - len,
						"operation mode: DeepSleep\n");
	else
		len += snprintf(buf + len, PAGE_SIZE - len,
					"operation mode: Unconfirmed\n");

	return len;
}

static ssize_t aw9610x_click_slide_mode_set(struct device *dev,
						struct device_attribute *attr,
						const char *buf, size_t count)
{
	ssize_t ret = 0;
	uint32_t data_en;
	uint32_t stat;
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	ret = kstrtouint(buf, 10, &aw9610x->click_slide_flag);
	if (ret) {
		AWLOGE(aw9610x->dev, "fail to set click_slide_mode");
		return ret;
	}
	if(aw9610x->click_slide_flag == 1){
		aw9610x_i2c_read(aw9610x, REG_STAT0, &stat);
		if((stat >> 24) != 0){
	    input_report_key(aw9610x->aw_channel[0].input, KEY_F6, 0);
        input_sync(aw9610x->aw_channel[0].input);
		}
		aw9610x_i2c_read(aw9610x, REG_SCANCTRL0, &data_en);
		aw9610x_i2c_write_bits(aw9610x, REG_SCANCTRL0, ~(0x3f << 8), (data_en & 0x3f) << 8);
	}
	pr_info("%s: slide_status = 0x%04x\n", __func__, aw9610x->click_slide_flag);
	return count;
}

static ssize_t aw9610x_click_slide_mode_get(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct aw9610x *aw9610x = dev_get_drvdata(dev);
	ssize_t len = 0;
	if (aw9610x->click_slide_flag == 0)
		len += snprintf(buf + len, PAGE_SIZE - len,
						"click_slide mode: Click\n");
	else if (aw9610x->click_slide_flag == 1)
		len += snprintf(buf + len, PAGE_SIZE - len,
						"click_slide mode: Slide\n");
	return len;
}

static DEVICE_ATTR(reg, 0664, aw9610x_get_reg, aw9610x_set_reg);
static DEVICE_ATTR(valid, 0664, aw9610x_valid_show, NULL);
static DEVICE_ATTR(baseline, 0664, aw9610x_baseline_show, NULL);
static DEVICE_ATTR(diff, 0664, aw9610x_diff_show, NULL);
static DEVICE_ATTR(raw_data, 0664, aw9610x_raw_data_show, NULL);
static DEVICE_ATTR(psc_data, 0664, aw9610x_psc_data_show, NULL);
static DEVICE_ATTR(parasitic_data, 0664, aw9610x_parasitic_data_show, NULL);
static DEVICE_ATTR(factory_cali, 0664, NULL, aw9610x_factory_cali_set);
static DEVICE_ATTR(aot_cali, 0664, NULL, aw9610x_aot_cali_set);
static DEVICE_ATTR(awrw, 0664, aw9610x_awrw_get, aw9610x_awrw_set);
static DEVICE_ATTR(update, 0644, NULL, aw9610x_set_update);
static DEVICE_ATTR(satu, 0644, aw9610x_get_satu, aw9610x_set_satu);
static DEVICE_ATTR(prox, 0644, aw9610x_power_prox_get, NULL);
static DEVICE_ATTR(operation_mode, 0644, aw9610x_operation_mode_get,
						aw9610x_operation_mode_set);
static DEVICE_ATTR(click_slide_mode, 0644, aw9610x_click_slide_mode_get,
						aw9610x_click_slide_mode_set);

static struct attribute *aw9610x_sar_attributes[] = {
	&dev_attr_reg.attr,
	&dev_attr_valid.attr,
	&dev_attr_baseline.attr,
	&dev_attr_diff.attr,
	&dev_attr_raw_data.attr,
	&dev_attr_psc_data.attr,
	&dev_attr_parasitic_data.attr,
	&dev_attr_awrw.attr,
	&dev_attr_factory_cali.attr,
	&dev_attr_aot_cali.attr,
	&dev_attr_update.attr,
	&dev_attr_satu.attr,
	&dev_attr_prox.attr,
	&dev_attr_operation_mode.attr,
	&dev_attr_click_slide_mode.attr,
	NULL
};

static struct attribute_group aw9610x_sar_attribute_group = {
	.attrs = aw9610x_sar_attributes
};

/*****************************************************
*
* touch info store if touch info is valid.
* realize by a simple FIFO.
*
*****************************************************/
static void aw9610x_touch_store(struct aw9610x *aw9610x,
				struct aw_touch_info this_touch)
{

	memmove(&aw9610x->touch_info[1], aw9610x->touch_info,
		(AW_SAR_FIFO_MAX - 1) * sizeof(struct aw_touch_info));
	memcpy(aw9610x->touch_info, &this_touch,
				sizeof(struct aw_touch_info));
}

/*****************************************************
*
* get the channel which had caused irq.
* this channel's last status should be 0. if not, don't return.
*
*****************************************************/
static void aw9610x_get_pressed_info(struct aw9610x *aw9610x)
{
	uint32_t ret = 0;
	uint8_t status = 0;
	uint8_t temp = 0;
	int8_t i = 0;
	struct aw_touch_info this_touch;
	dev_info(aw9610x->dev, "%s: enter\n", __func__);

	aw9610x_i2c_read(aw9610x, REG_STAT0, &ret);
	dev_info(aw9610x->dev, "%s: ret = 0x%x\n", __func__, ret);
	status = (ret&0x07000000) >> 24;
	dev_info(aw9610x->dev, "%s: ret&0x07000000 = 0x%x\n", __func__,
							ret & 0x07000000);
	dev_info(aw9610x->dev, "%s: status = 0x%x\n", __func__, status);
	for (i = 0; i < 3; i++) {
		temp = (status >> i);
		pr_info("%s: i = %d, temp = 0x%x\n", __func__, i, temp);
		if ((temp & 0x01) == 0x01) {
			aw9610x->channel_status_now[i] = 1;
			pr_info("%s: aw9610x->channel_status_now[%d]=%d\n",
				__func__,i, aw9610x->channel_status_now[i]);
			pr_info("%s: aw9610x->channel_status_last[%d]=%d\n",
				__func__, i, aw9610x->channel_status_last[i]);
			if (aw9610x->channel_status_last[i] == 0) {
				this_touch.time_pressed = ktime_get();
				switch (i) {
				case 0:
					pr_info("%s: btn 1 press\n", __func__);
					this_touch.channel = aw9610x_pad1;
					break;
				case 1:
					pr_info("%s: btn 2 press\n", __func__);
					this_touch.channel = aw9610x_pad2;
					break;
				case 2:
					pr_info("%s: btn 3 press\n", __func__);
					this_touch.channel = aw9610x_pad3;
					break;
				}
				aw9610x_touch_store(aw9610x, this_touch);
			}
			aw9610x->channel_status_last[i] =
				aw9610x->channel_status_now[i];
		}
	}
}

/*****************************************************
*
* slide init
*
*****************************************************/
static void aw9610x_slide_reset(struct aw9610x *aw9610x)
{
	//int i;
	memset(aw9610x->channel_status_last, 0, 3 * sizeof(uint8_t));
	memset(aw9610x->channel_status_now, 0, 3 * sizeof(uint8_t));
	/*for(i = 0;i < 3;i++){
		aw9610x->touch_info[i].channel = 0;
		//aw9610x->touch_info[i].time_pressed = 0;
	}*/
}

/*****************************************************
*
* report if slide info is valid
*
*****************************************************/
static void aw9610x_slide_report(struct aw9610x *aw9610x)
{
	uint32_t mir_sec = 0;
	uint32_t mir_sec1 = 0;
	int8_t i = 0;
	int32_t press_status = 0;

	pr_info("%s: enter\n", __func__);

	mir_sec = ktime_to_ms(ktime_sub(aw9610x->touch_info[0].time_pressed,aw9610x->touch_info[1].time_pressed));
	mir_sec1 = ktime_to_ms(ktime_sub(aw9610x->touch_info[1].time_pressed,aw9610x->touch_info[2].time_pressed));
	pr_info("%s: mir_sec1 = %d,mir_sec = %d\n", __func__, mir_sec1,mir_sec);
	if (((mir_sec + mir_sec1) < AW_SAR_SLIDE_DETEC_TIME_MAX) && (mir_sec > 10) && (mir_sec1 > 10)) {
		for (i = 0; i < AW_SAR_FIFO_MAX; i++) {
			press_status |= aw9610x->touch_info[i].channel << 4*i;
		}
		pr_info("%s: press_status = 0x%04x\n", __func__, press_status);
		switch (press_status) {
		case (aw9610x_pad3<<8) | (aw9610x_pad1<<4) | (aw9610x_pad2<<0):
		//case (aw9610x_pad1<<8) | (aw9610x_pad1<<4) | (aw9610x_pad2<<0):
			//input_report_abs(aw9610x->input, ABS_DISTANCE, 15);
			input_report_key(aw9610x->aw_channel[0].input, KEY_F6, 1);
		    input_sync(aw9610x->aw_channel[0].input);
			msleep(100);
			input_report_key(aw9610x->aw_channel[0].input, KEY_F6, 0);
			pr_info("%s:  a lef slide.\n", __func__);
			input_sync(aw9610x->aw_channel[0].input);
			//aw9610x_slide_reset(aw9610x);
			break;
		case (aw9610x_pad2<<8) | (aw9610x_pad1<<4) | (aw9610x_pad3<<0):
		//case (aw9610x_pad1<<8) | (aw9610x_pad1<<4) | (aw9610x_pad3<<0):
			//input_report_abs(aw9610x->input, ABS_DISTANCE, 16);
			input_report_key(aw9610x->aw_channel[0].input, KEY_F6, 1);
			input_sync(aw9610x->aw_channel[0].input);
			msleep(100);
			input_report_key(aw9610x->aw_channel[0].input, KEY_F6, 0);
			pr_info("%s:  a right slide.\n", __func__);
			input_sync(aw9610x->aw_channel[0].input);
			//aw9610x_slide_reset(aw9610x);
			break;
		default:
			pr_info("%s: not a slide, exit.\n", __func__);
			break;
		}
	} else if(mir_sec <= 10){

		aw9610x_slide_reset(aw9610x);
		pr_info("%s: mir_sec = %ld, mir_sec1 = %ld",__func__, mir_sec, mir_sec1);
	}else {
		pr_info("%s: time = %ld, over time max = %ld",
			__func__, mir_sec, AW_SAR_SLIDE_DETEC_TIME_MAX);
	}
}

static void  aw9610x_leave_detection(struct aw9610x *aw9610x)
{
	uint32_t ret = 0;
	uint8_t status = 0;
	int8_t i = 0;

	pr_info("%s: enter\n", __func__);

	aw9610x_i2c_read(aw9610x, REG_STAT0, &ret);
	status = (ret&0x07000000) >> 24;
	pr_info("%s:status = 0x%x\n",__func__, status);
	for (i = 0; i < 3; i++) {
		if (((status >> i) & 0x01) != 0x01){
			aw9610x->channel_status_now[i] = 0;
			
				if(aw9610x->channel_status_last[i] == 1) {
					aw9610x->channel_status_last[i] = aw9610x->channel_status_now[i];
					pr_info("%s:aw9610x->channel_status_now[%d] is reset\n",__func__, i);
				}
		}
	}
}

static void aw9610x_slide(struct aw9610x *aw9610x)
{
	if ((aw9610x->irq_status & 0x0002) == 0x0002) {
		dev_info(aw9610x->dev,
				"%s approach status = 0x%x\n",
				__func__, aw9610x->irq_status);

		/* sliding process */
		aw9610x_get_pressed_info(aw9610x);
		aw9610x_slide_report(aw9610x);
	} else if ((aw9610x->irq_status & 0x0004) == 0x0004) {
		dev_info(aw9610x->dev, "%s far status = 0x%x\n",
				__func__, aw9610x->irq_status);
		aw9610x_leave_detection(aw9610x);
		input_report_abs(aw9610x->aw_channel[0].input, ABS_DISTANCE, 1);
	}
}
/*****************************************************
*
* irq init
*
*****************************************************/
static void aw9610x_irq_handle(struct aw9610x *aw9610x)
{
 uint32_t curr_status = 0;

 AWLOGD(aw9610x->dev, "enter");

 aw9610x_i2c_read(aw9610x, REG_STAT0, &curr_status);
 AWLOGD(aw9610x->dev, "channel = 0x%08x", curr_status);
 if(curr_status == 0){
  if(aw9610x->curr_st == 1){
   input_report_key(aw9610x->aw_channel[0].input, KEY_F6, 0);
   input_sync(aw9610x->aw_channel[0].input);
   AWLOGD(aw9610x->dev, "curr_st = 0x%08x", aw9610x->curr_st);
  aw9610x->curr_st = 0;}	 
 }else{
  if(aw9610x->curr_st == 0){
   input_report_key(aw9610x->aw_channel[0].input, KEY_F6, 1);
   input_sync(aw9610x->aw_channel[0].input);
   AWLOGD(aw9610x->dev, "curr_st = 0x%08x", aw9610x->curr_st);
  aw9610x->curr_st = 1;}	
 }
}
static void aw9610x_farirq_handle(struct aw9610x *aw9610x)
{
	uint8_t th0_far = 0;

	th0_far = (aw9610x->irq_status >> 2) & 0x1;
	if (th0_far == 1)
		aw9610x->power_prox = AW9610X_FUNC_OFF;
}

static void aw9610x_irq_multiple_sar_select(struct aw9610x *aw9610x)
{
	/* multiple sar handle IO */
	switch (aw9610x->sar_num) {
	case AW_SAR0:
		break;
	case AW_SAR1:
		break;
	default:
		return;
	}
}

static void aw9610x_version_aw9610x_private(struct aw9610x *aw9610x)
{
	AWLOGD(aw9610x->dev, "AW9610X enter");

	if (aw9610x->satu_release == AW9610X_FUNC_ON)
		aw9610x_saturat_release_handle(aw9610x);
}

static void aw9610x_version_aw9610xA_private(struct aw9610x *aw9610x)
{
	AWLOGD(aw9610x->dev, "AW9610XA enter");
}

static void aw9610x_version_aw9610xB_private(struct aw9610x *aw9610x)
{
	uint32_t progxirq_stat = (aw9610x->irq_status >> 12) & 0x0f;

	AWLOGD(aw9610x->dev, "AW9610XB enter progxirq_stat = %d", progxirq_stat);

	switch (progxirq_stat) {
	case PROG0IRQ:
		input_report_abs(
			aw9610x->aw_channel[aw9610x->sar_num * AW_CHANNEL_MAX].input,
							ABS_DISTANCE, 5);
		input_report_abs(
			aw9610x->aw_channel[aw9610x->sar_num * AW_CHANNEL_MAX].input,
							ABS_DISTANCE, 16);
		input_sync(aw9610x->aw_channel[aw9610x->sar_num * AW_CHANNEL_MAX].input);
		AWLOGD(aw9610x->dev, "ABS_DISTANCE : 5");
		break;
	case PROG1IRQ:
		input_report_abs(
			aw9610x->aw_channel[aw9610x->sar_num * AW_CHANNEL_MAX].input,
							ABS_DISTANCE, 6);
		input_report_abs(
			aw9610x->aw_channel[aw9610x->sar_num * AW_CHANNEL_MAX].input,
							ABS_DISTANCE, 16);
		input_sync(aw9610x->aw_channel[aw9610x->sar_num * AW_CHANNEL_MAX].input);
		AWLOGD(aw9610x->dev, "ABS_DISTANCE : 6");
		break;
	case PROG2IRQ:
		input_report_abs(
			aw9610x->aw_channel[aw9610x->sar_num * AW_CHANNEL_MAX].input,
							ABS_DISTANCE, 7);
		input_report_abs(
			aw9610x->aw_channel[aw9610x->sar_num * AW_CHANNEL_MAX].input,
							ABS_DISTANCE, 16);
		input_sync(aw9610x->aw_channel[aw9610x->sar_num * AW_CHANNEL_MAX].input);
		AWLOGD(aw9610x->dev, "ABS_DISTANCE : 7");
		break;
	case PROG3IRQ:
		input_report_abs(
			aw9610x->aw_channel[aw9610x->sar_num * AW_CHANNEL_MAX].input,
							ABS_DISTANCE, 8);
		input_report_abs(
			aw9610x->aw_channel[aw9610x->sar_num * AW_CHANNEL_MAX].input,
							ABS_DISTANCE, 16);
		input_sync(aw9610x->aw_channel[aw9610x->sar_num * AW_CHANNEL_MAX].input);
		AWLOGD(aw9610x->dev, "ABS_DISTANCE : 8");
		break;
	default:
		AWLOGD(aw9610x->dev, "There is no current state");
		break;
	}
}

static void aw9610x_interrupt_clear(struct aw9610x *aw9610x)
{
	int32_t ret = 0;

	AWLOGD(aw9610x->dev, "enter");

	ret = aw9610x_i2c_read(aw9610x, REG_HOSTIRQSRC, &aw9610x->irq_status);
	if (ret < 0) {
		AWLOGE(aw9610x->dev, "i2c IO error");
		return;
	}
	AWLOGI(aw9610x->dev, "IRQSRC = 0x%x", aw9610x->irq_status);

	if (aw9610x->pwprox_dete == true)
		aw9610x_farirq_handle(aw9610x);

	switch (aw9610x->vers) {
	case AW9610X:
		aw9610x_version_aw9610x_private(aw9610x);
		break;
	case AW9610XA:
		aw9610x_version_aw9610xA_private(aw9610x);
		break;
	case AW9610XB:
		aw9610x_version_aw9610xB_private(aw9610x);
		break;
	default:
		return;
	}

	aw9610x_irq_multiple_sar_select(aw9610x);
	if(aw9610x->click_slide_flag)
		aw9610x_slide(aw9610x);
	else
		aw9610x_irq_handle(aw9610x);
}

static irqreturn_t aw9610x_irq(int32_t irq, void *data)
{
	struct aw9610x *aw9610x = data;

	AWLOGD(aw9610x->dev, "enter");

	aw9610x_interrupt_clear(aw9610x);
	AWLOGD(aw9610x->dev, "exit");

	return IRQ_HANDLED;
}

#ifdef AW_PINCTRL_ON
void aw9610x_int_output(struct aw9610x *aw9610x, int32_t level)
{
	pr_info("%s enter aw9610x int level:%d\n", __func__, level);
	if (level == 0) {
		if (aw9610x->pinctrl.pinctrl) {
			pinctrl_select_state(aw9610x->pinctrl.pinctrl,
						aw9610x->pinctrl.int_out_low);
		} else {
			pr_info("%s Failed set int pin output low\n", __func__);
		}
	} else if (level == 1) {
		if (aw9610x->pinctrl.pinctrl) {
			pinctrl_select_state(aw9610x->pinctrl.pinctrl,
						aw9610x->pinctrl.int_out_high);
		} else {
			pr_info("%s Failed set int pin output high\n", __func__);
		}
	}
}

static int32_t aw9610x_pinctrl_init(struct aw9610x *aw9610x)
{
	struct aw9610x_pinctrl *pinctrl = &aw9610x->pinctrl;
	uint8_t pin_default_name[50] = { 0 };
	uint8_t pin_output_low_name[50] = { 0 };
	uint8_t pin_output_high_name[50] = { 0 };

	AWLOGD(aw9610x->dev, "enter");

	pinctrl->pinctrl = devm_pinctrl_get(aw9610x->dev);
	if (IS_ERR_OR_NULL(pinctrl->pinctrl)) {
		pr_info("%s:No pinctrl found\n", __func__);
		pinctrl->pinctrl = NULL;
		return -EINVAL;
	}

	snprintf(pin_default_name, sizeof(pin_default_name),
					"aw_default_sar%d", aw9610x->sar_num);
	AWLOGD(aw9610x->dev, "pin_default_name = %s", pin_default_name);
	pinctrl->default_sta = pinctrl_lookup_state(pinctrl->pinctrl,
							pin_default_name);
	if (IS_ERR_OR_NULL(pinctrl->default_sta)) {
		AWLOGE(aw9610x->dev, "Failed get pinctrl state:default state");
		goto exit_pinctrl_init;
	}

	snprintf(pin_output_high_name, sizeof(pin_output_high_name),
				"aw_int_output_high_sar%d", aw9610x->sar_num);
	AWLOGD(aw9610x->dev, "pin_output_high_name = %s", pin_output_high_name);
	pinctrl->int_out_high = pinctrl_lookup_state(pinctrl->pinctrl,
							pin_output_high_name);
	if (IS_ERR_OR_NULL(pinctrl->int_out_high)) {
		AWLOGE(aw9610x->dev, "Failed get pinctrl state:output_high");
		goto exit_pinctrl_init;
	}

	snprintf(pin_output_low_name, sizeof(pin_output_low_name),
				"aw_int_output_low_sar%d", aw9610x->sar_num);
	AWLOGD(aw9610x->dev, "pin_output_low_name = %s", pin_output_low_name);
	pinctrl->int_out_low = pinctrl_lookup_state(pinctrl->pinctrl,
							pin_output_low_name);
	if (IS_ERR_OR_NULL(pinctrl->int_out_low)) {
		AWLOGE(aw9610x->dev, "Failed get pinctrl state:output_low");
		goto exit_pinctrl_init;
	}

	pr_info("%s: Success init pinctrl\n", __func__);
	return 0;
exit_pinctrl_init:
	devm_pinctrl_put(pinctrl->pinctrl);
	pinctrl->pinctrl = NULL;
	return -EINVAL;
}

static void aw9610x_pinctrl_deinit(struct aw9610x *aw9610x)
{
	if (aw9610x->pinctrl.pinctrl)
		devm_pinctrl_put(aw9610x->pinctrl.pinctrl);
}
#endif

static int32_t aw9610x_interrupt_init(struct aw9610x *aw9610x)
{
	int32_t irq_flags = 0;
	int32_t ret = 0;
	uint8_t i = 0;
	int8_t irq_gpio_name[100] = { 0 };

	AWLOGD(aw9610x->dev, "enter");

	for (i = 0; i < AW_CHANNEL_MAX; i++)
		aw9610x->satu_flag[i] = 0;

	snprintf(irq_gpio_name, sizeof(irq_gpio_name),
					"aw9610x_irq_gpio%d", aw9610x->sar_num);

	if (gpio_is_valid(aw9610x->irq_gpio)) {
		aw9610x->to_irq = gpio_to_irq(aw9610x->irq_gpio);

		ret = devm_gpio_request_one(aw9610x->dev,
					aw9610x->irq_gpio,
					GPIOF_DIR_IN | GPIOF_INIT_HIGH,
					irq_gpio_name);

		if (ret) {
			AWLOGE(aw9610x->dev,
				"request irq gpio failed, ret = %d", ret);
			ret = -AW_IRQIO_FAILED;
		} else {
			/* register irq handler */
			irq_flags = IRQF_TRIGGER_LOW | IRQF_ONESHOT;
			ret = devm_request_threaded_irq(&aw9610x->i2c->dev,
							aw9610x->to_irq, NULL,
							aw9610x_irq, irq_flags,
							"aw9610x_irq", aw9610x);
			if (ret != 0) {
				AWLOGE(aw9610x->dev,
						"failed to request IRQ %d: %d",
						aw9610x->to_irq, ret);
				ret = -AW_IRQ_REQUEST_FAILED;
			} else {
				AWLOGI(aw9610x->dev,
					"IRQ request successfully!");
				ret = AW_SAR_SUCCESS;
			}
		}
	} else {
		AWLOGE(aw9610x->dev, "irq gpio invalid!");
		return -AW_IRQIO_FAILED;
	}

	return ret;
}

/*****************************************************
 *
 * parse dts
 *
 *****************************************************/
static int32_t aw9610x_parse_dt(struct device *dev, struct aw9610x *aw9610x,
			   struct device_node *np)
{
	uint32_t val = 0;

	val = of_property_read_u32(np, "sar-num", &aw9610x->sar_num);
	if (val != 0) {
		AWLOGE(aw9610x->dev, "multiple sar failed!");
		return -AW_MULTIPLE_SAR_FAILED;
	} else {
		AWLOGI(aw9610x->dev, "sar num = %d", aw9610x->sar_num);
	}

	aw9610x->irq_gpio = of_get_named_gpio(np, "irq-gpio", 0);
	if (aw9610x->irq_gpio < 0) {
		aw9610x->irq_gpio = -1;
		AWLOGE(aw9610x->dev, "no irq gpio provided.");
		return -AW_IRQGPIO_FAILED;
	} else {
		AWLOGI(aw9610x->dev, "irq gpio provided ok.");
	}

	aw9610x->firmware_flag =
			of_property_read_bool(np, "aw9610x,using-firmware");
	AWLOGI(aw9610x->dev, "firmware_flag = <%d>", aw9610x->firmware_flag);

	aw9610x->pwprox_dete =
		of_property_read_bool(np, "aw9610x,using-pwon-prox-dete");
	AWLOGI(aw9610x->dev, "pwprox_dete = <%d>", aw9610x->pwprox_dete);

	aw9610x->satu_release =
		of_property_read_bool(np, "aw9610x,using-satu");
	AWLOGI(aw9610x->dev, "satu_release = <%d>", aw9610x->satu_release);

	return AW_SAR_SUCCESS;
}

#ifdef AW_POWER_ON
static int32_t aw9610x_power_init(struct aw9610x *aw9610x)
{
	int32_t rc;
	uint8_t vcc_name[20] = { 0 };

	AWLOGD(aw9610x->dev, "aw9610x power init enter");

	snprintf(vcc_name, sizeof(vcc_name), "vcc%d", aw9610x->sar_num);
	AWLOGD(aw9610x->dev, "vcc_name = %s", vcc_name);

	aw9610x->vcc = regulator_get(aw9610x->dev, vcc_name);
	if (IS_ERR(aw9610x->vcc)) {
		rc = PTR_ERR(aw9610x->vcc);
		AWLOGE(aw9610x->dev, "regulator get failed vcc rc = %d", rc);
		return rc;
	}

	if (regulator_count_voltages(aw9610x->vcc) > 0) {
		rc = regulator_set_voltage(aw9610x->vcc,
					AW_VCC_MIN_UV, AW_VCC_MAX_UV);
		if (rc) {
			AWLOGE(aw9610x->dev,
				"regulator set vol failed rc = %d", rc);
			goto reg_vcc_put;
		}
	}

	return rc;

reg_vcc_put:
	regulator_put(aw9610x->vcc);
	return rc;
}

static void aw9610x_power_enable(struct aw9610x *aw9610x, bool on)
{
	int32_t rc = 0;

	AWLOGD(aw9610x->dev, "aw9610x power enable enter");

	if (on) {
		rc = regulator_enable(aw9610x->vcc);
		if (rc) {
			AWLOGE(aw9610x->dev,
				"regulator_enable vol failed rc = %d", rc);
		} else {
			aw9610x->power_enable = true;
			msleep(20);
		}
	} else {
		rc = regulator_disable(aw9610x->vcc);
		if (rc)
			AWLOGE(aw9610x->dev,
				"regulator_disable vol failed rc = %d", rc);
		else
			aw9610x->power_enable = false;
	}
}

static int32_t regulator_is_get_voltage(struct aw9610x *aw9610x)
{
	uint32_t cnt = 10;
	int32_t voltage_val = 0;

	AWLOGD(aw9610x->dev, "enter");

	while(cnt--) {
		voltage_val = regulator_get_voltage(aw9610x->vcc);
		AWLOGD(aw9610x->dev, "aw9610x voltage is : %d uv", voltage_val);
		if (voltage_val >= AW9610X_CHIP_MIN_VOLTAGE)
			return AW_SAR_SUCCESS;
		mdelay(1);
	}

	return -AW_VERS_ERR;
}

static int32_t aw9610x_wait_chip_init(struct aw9610x *aw9610x)
{
	uint32_t cnt = 20;
	uint32_t reg_data = 0;
	uint32_t chip_init_flag = 0;

	AWLOGD(aw9610x->dev, "enter");

	while (cnt--) {
		aw9610x_i2c_read(aw9610x, REG_HOSTIRQSRC, &reg_data);
		AWLOGE(aw9610x->dev, "REG_HOSTIRQSRC = 0x%x", reg_data);
		chip_init_flag = reg_data & 0x01;
		if (chip_init_flag == 1) {
			AWLOGE(aw9610x->dev, "chip init success cnt = %d", cnt);
			return AW_SAR_SUCCESS;
		}
		mdelay(1);
	}
	AWLOGE(aw9610x->dev, "hardware has trouble!");

	return -AW_IRQIO_FAILED;
}

#endif

/*****************************************************
 *
 * check chip id
 *
 *****************************************************/
static int32_t aw9610x_read_chipid(struct aw9610x *aw9610x)
{
	int32_t ret = -1;
	uint8_t cnt = 0;
	uint32_t reg_val = 0;

	while (cnt < AW_READ_CHIPID_RETRIES) {
		ret = aw9610x_i2c_read(aw9610x, REG_CHIP_ID, &reg_val);
		if (ret < 0) {
			AWLOGE(aw9610x->dev, "read CHIP ID failed: %d", ret);
		} else {
			reg_val = reg_val >> 16;
			break;
		}

		cnt++;
		usleep_range(2000, 3000);
	}

	if (reg_val == AW9610X_CHIP_ID) {
		AWLOGI(aw9610x->dev, "aw9610x detected");
		return AW_SAR_SUCCESS;
	} else {
		AWLOGE(aw9610x->dev,
			"unsupport dev, chipid is (0x%04x)", reg_val);
	}

	return -AW_CHIPID_FAILED;
}

static void aw9610x_i2c_set(struct i2c_client *i2c,
						struct aw9610x *aw9610x)
{
	aw9610x->dev = &i2c->dev;
	aw9610x->i2c = i2c;
	i2c_set_clientdata(i2c, aw9610x);
}

static int32_t aw9610x_version_init(struct aw9610x *aw9610x)
{
	uint32_t firmvers = 0;

	aw9610x_i2c_read(aw9610x, REG_FIRMVERSION, &firmvers);
	AWLOGD(aw9610x->dev, "REG_FIRMVERSION = 0x%x", firmvers);
	aw9610x->channel = firmvers >> 28;
	switch (aw9610x->channel) {
	case 4:
		aw9610x->channel -= 1;
		break;
	case 5:
		break;
	case 6:
		break;
	default:
		AWLOGD(aw9610x->dev, "firmvers err, firmvers >> 28 = %d",
					firmvers >> 28);
		return -AW_VERS_ERR;
	}
	snprintf(aw9610x->chip_name, sizeof(aw9610x->chip_name),
						"AW9610%d", aw9610x->channel);
	aw9610x->chip_name[7] = '\0';

	aw9610x->vers = (firmvers >> 24) & 0xf;
	if (aw9610x->vers == AW9610XA) {
		memcpy(aw9610x->chip_name + strlen(aw9610x->chip_name), "A", 1);
	} else if (aw9610x->vers == AW9610XB) {
		memcpy(aw9610x->chip_name + strlen(aw9610x->chip_name), "B", 1);
	} else if (aw9610x->vers == AW9610X) {
		aw9610x->chip_name[7] = '\0';
	} else {
		AWLOGD(aw9610x->dev, "vers err, firmvers >> 24 = %d",
				firmvers >> 24);
		return -AW_VERS_ERR;
	}

	aw9610x->chip_name[8] = '\0';

	AWLOGI(aw9610x->dev, "the IC is = %s", aw9610x->chip_name);

	return AW_SAR_SUCCESS;
}

//yulei add
static ssize_t aw9610x_file_ioctl(struct file *filp, unsigned int cmd,
             			unsigned long arg)
{
	struct aw9610x *aw9610x = (struct aw9610x *)filp->private_data;

	AWLOGI(aw9610x->dev, "cmd = %d\n", cmd);

	switch (cmd)
	{
	case SINGLE_CLICK:
		AWLOGI(aw9610x->dev, "SINGLE_CLICK\n");
		input_report_abs(aw9610x->aw_channel[0].input,ABS_DISTANCE, 1);
		input_report_abs(aw9610x->aw_channel[0].input,ABS_DISTANCE, 0);
		break;
	case DOUBLE_CLICK:
		AWLOGI(aw9610x->dev, "DOUBLE_CLICK\n");
		break;
	case TREBLE_CLICK:
		AWLOGI(aw9610x->dev, "TREBLE_CLICK\n");
		break;
	case LONG_PRESS:
		AWLOGI(aw9610x->dev, "LONG_PRESS\n");
		break;
	case LONG_PRESS_PLUS:
		AWLOGI(aw9610x->dev, "LONG_PRESS_PLUS\n");
		break;
	case LEFT_SILDE:
		AWLOGI(aw9610x->dev, "LEFT_SILDE\n");
		input_report_abs(aw9610x->aw_channel[1].input,ABS_DISTANCE, 1);
		input_report_abs(aw9610x->aw_channel[1].input,ABS_DISTANCE, 0);
		break;
	case LEFT_SILDE_FAST:
		AWLOGI(aw9610x->dev, "LEFT_SILDE_FAST\n");
		input_report_abs(aw9610x->aw_channel[2].input,ABS_DISTANCE, 1);
		input_report_abs(aw9610x->aw_channel[2].input,ABS_DISTANCE, 0);
		break;
	case RIGHT_SILDE:
		AWLOGI(aw9610x->dev, "RIGHT_SILDE\n");
		input_report_abs(aw9610x->aw_channel[3].input,ABS_DISTANCE, 1);
		input_report_abs(aw9610x->aw_channel[3].input,ABS_DISTANCE, 0);
		break;
	case RIGHT_SILDE_FAST:
		AWLOGI(aw9610x->dev, "RIGHT_SILDE_FAST\n");
		input_report_abs(aw9610x->aw_channel[4].input,ABS_DISTANCE, 1);
		input_report_abs(aw9610x->aw_channel[4].input,ABS_DISTANCE, 0);
		break;
	case WEAR_ON:
		AWLOGI(aw9610x->dev, "WEAR_ON\n");
		break;
	case T_WEAR_ON:
		AWLOGI(aw9610x->dev, "T_WEAR_ON\n");
		break;
	case WEAR_OFF:
		AWLOGI(aw9610x->dev, "WEAR_OFF\n");
		break;
	default:
		AWLOGE(aw9610x->dev, "NO such event\n");
		return -EINVAL;
	}
	return 0;
}

static ssize_t aw9610x_file_read(struct file *filp, char *buff, size_t count,
				 loff_t *offset)
{
	int i = 0;
	int32_t reg_val[AW_CHANNEL_MAX + 1] = {0};
	struct aw9610x *aw9610x = (struct aw9610x *)filp->private_data;

	if(count > sizeof(reg_val)) {
		AWLOGE(aw9610x->dev, "%s, read count = %zu, \
					more than sizeof reg_val = %d.\n",
					__func__, count, sizeof(reg_val));
		return -EFAULT;
	}

	for (i = 0; i < AW_CHANNEL_MAX; i++) {
		aw9610x_i2c_read(aw9610x, REG_DIFF_CH0 + i * 4, &reg_val[i]);
		// reg_val[i] >>= 10;
	}
	aw9610x_i2c_read(aw9610x, 0x0090, &reg_val[AW_CHANNEL_MAX]);
	
	if(copy_to_user(buff, reg_val, count)) {
		AWLOGE(aw9610x->dev, "Failed copy to user\n");
		return -EFAULT;
	}
	return count;
}
static ssize_t aw9610x_file_write(struct file *filp,
				     const char __user *buffer,
				     size_t count, loff_t *off) {
	struct aw9610x *aw9610x = (struct aw9610x *)filp->private_data;
	int ret = 0;
	char buf[6];
	uint16_t addr = 0;
	uint32_t data = 0;

	if(count > sizeof(buf)) {
		AWLOGI(aw9610x->dev, "count err, count = %zu", count);
		return -EFAULT;
	}
	AWLOGI(aw9610x->dev, "file write enter");

	ret = copy_from_user(buf, buffer, count);
	addr = (uint16_t)((((uint16_t)buf[1]) << 8) | buf[0]);
	data = (uint32_t)((((uint32_t)buf[5]) << 24) |
			(((uint32_t)buf[4]) << 16) |
			(((uint32_t)buf[3]) << 8) |
			((uint32_t)buf[2]));
	
	AWLOGI(aw9610x->dev, "addr = 0x%02x, data = 0x%04x", addr, data);
	aw9610x_i2c_write(aw9610x, addr, data);
	AWLOGI(aw9610x->dev, "file write leave");
	return count;
}
static int aw9610x_file_open(struct inode *inode, struct file *filp)
{
	if (!try_module_get(THIS_MODULE))
		return -ENODEV;
	AWLOGI(g_aw9610x->dev, "aw9610x_file_open enter");
	filp->private_data = (void *)g_aw9610x;
	return 0;
}

static int aw9610x_file_release(struct inode *inode, struct file *filp)
{
	filp->private_data = (void *)NULL;

	module_put(THIS_MODULE);

	return 0;
}

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.read = aw9610x_file_read,
	.unlocked_ioctl = aw9610x_file_ioctl,
	.open = aw9610x_file_open,
	.write = aw9610x_file_write,
	.release = aw9610x_file_release,
};

static struct miscdevice aw9610x_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "aw9610x_diff",
	.fops = &fops,
};

static int aw9610x_file_init(struct aw9610x *aw9610x)
{
	int ret = 0;
	g_aw9610x = aw9610x;
	ret = misc_register(&aw9610x_misc);
	if (ret) {
		dev_err(aw9610x->dev, "%s: misc fail: %d\n", __func__, ret);
		return ret;
	}
	return 0;
}
//yulei add end

static int32_t
aw9610x_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct aw9610x *aw9610x;
	struct device_node *np = i2c->dev.of_node;
	int32_t ret = 0;
	uint32_t i = 0;
	uint32_t j = 0;
	uint32_t err_num = 0;

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C)) {
		AWLOGE(&i2c->dev, "check_functionality failed");
		return -EIO;
	}

	aw9610x = devm_kzalloc(&i2c->dev, sizeof(struct aw9610x), GFP_KERNEL);
	if (aw9610x == NULL) {
		AWLOGE(&i2c->dev, "failed to malloc memory!");
		ret = -AW_MALLOC_FAILED;
		goto err_malloc;
	}

	aw9610x_i2c_set(i2c, aw9610x);

#ifdef AW_POWER_ON
	/* aw9610x power init */
	ret = aw9610x_power_init(aw9610x);
	if (ret)
		AWLOGE(&i2c->dev, "aw9610x power init failed");
	else
		aw9610x_power_enable(aw9610x, true);

	ret = regulator_is_get_voltage(aw9610x);
	if (ret != AW_SAR_SUCCESS) {
		AWLOGE(aw9610x->dev, "get_voltage failed");
		goto err_get_voltage;
	}

	ret = aw9610x_wait_chip_init(aw9610x);
	if (ret != AW_SAR_SUCCESS) {
		AWLOGE(aw9610x->dev, "_wait_chip_inite failed");
		goto err_wait_chip_init;
	}
#endif

	/* aw9610x chip id */
	ret = aw9610x_read_chipid(aw9610x);
	if (ret != AW_SAR_SUCCESS) {
		AWLOGE(aw9610x->dev, "read chipid failed, ret=%d", ret);
		goto err_chipid;
	}

	ret = aw9610x_version_init(aw9610x);
	if (ret < 0) {
		AWLOGE(aw9610x->dev, "read version failed, ret=%d", ret);
		goto err_vers_load;
	}
	ret = aw9610x_parse_dt(&i2c->dev, aw9610x, np);
	if (ret != AW_SAR_SUCCESS) {
		AWLOGE(aw9610x->dev, "irq gpio error!, ret = %d", ret);
		goto err_pase_dt;
	}
    aw9610x->curr_st =0;
	aw9610x_sw_reset(aw9610x);

	ret = aw9610x_init_irq_handle(aw9610x);
	if (ret != AW_SAR_SUCCESS) {
		AWLOGE(aw9610x->dev, "the trouble ret = %d", ret);
		goto err_first_irq;
	}

#ifdef AW_PINCTRL_ON
	ret = aw9610x_pinctrl_init(aw9610x);
	if (ret < 0) {
		/* if define pinctrl must define the following state
		 * to let int-pin work normally: default, int_output_high,
		 * int_output_low, int_input
		 */
		pr_err("%s: Failed get wanted pinctrl state\n", __func__);
		goto err_pinctrl;
	}

	aw9610x_int_output(aw9610x, 1);
#endif
	ret = aw9610x_interrupt_init(aw9610x);
	if (ret == -AW_IRQ_REQUEST_FAILED) {
		AWLOGE(aw9610x->dev, "request irq failed ret = %d", ret);
		goto err_requst_irq;
	}
	
	aw9610x->click_slide_flag = 0;
	
	aw9610x->aw_channel = channel_event;
	/* input device */
	j = aw9610x->sar_num;
	for (i = 0; i < AW_CHANNEL_MAX; i++) {
		aw9610x->aw_channel[j * AW_CHANNEL_MAX + i].input = input_allocate_device();
		if (!(aw9610x->aw_channel[j * AW_CHANNEL_MAX + i].input)) {
			err_num = i;
			goto exit_input_alloc_failed;
		}
		aw9610x->aw_channel[j * AW_CHANNEL_MAX + i].input->name =
						channel_event[j * AW_CHANNEL_MAX + i].name;
		__set_bit(EV_KEY, aw9610x->aw_channel[j * AW_CHANNEL_MAX + i].input->evbit);
		__set_bit(EV_SYN, aw9610x->aw_channel[j * AW_CHANNEL_MAX + i].input->evbit);
		__set_bit(KEY_F1, aw9610x->aw_channel[j * AW_CHANNEL_MAX + i].input->keybit);
		__set_bit(KEY_F6,aw9610x->aw_channel[i].input->keybit);
		input_set_abs_params(aw9610x->aw_channel[j * AW_CHANNEL_MAX + i].input,
						ABS_DISTANCE, -1, 100, 0, 0);

		ret = input_register_device(aw9610x->aw_channel[j * AW_CHANNEL_MAX + i].input);
		if (ret) {
			AWLOGE(aw9610x->dev, "failed to register input device");
			input_free_device(aw9610x->aw_channel[j * AW_CHANNEL_MAX + i].input);
			err_num = i;
			goto exit_input_register_device_failed;
		}
	}

	/* attribute */
	ret = sysfs_create_group(&i2c->dev.kobj, &aw9610x_sar_attribute_group);
	if (ret < 0) {
		AWLOGE(aw9610x->dev, "error creating sysfs attr files");
		goto err_sysfs;
	}
	
	//hrtimer_init(&aw9610x->timer, CLOCK_MONOTONIC,  HRTIMER_MODE_REL);
	//aw9610x->timer.function = aw9610x_vibrator_timer_func;
	//INIT_WORK(&aw9610x->timer_work, aw9610x_timer_work_routine);
	aw9610x->mode = aw9610x->old_mode = AW9610X_SLEEP_MODE;
	ret = aw9610x_sar_cfg_init(aw9610x, AW_CFG_UNLOAD);
	if (ret < 0) {
		AWLOGE(aw9610x->dev, "cfg situation not confirmed!");
		goto err_cfg;
	}
	//yulei add
	aw9610x_file_init(aw9610x);
	//yulei add end
	return AW_SAR_SUCCESS;

err_cfg:
err_sysfs:
	sysfs_remove_group(&i2c->dev.kobj, &aw9610x_sar_attribute_group);
exit_input_register_device_failed:
	for (i = 0; i < err_num; i++)
		input_unregister_device(aw9610x->aw_channel[j * AW_CHANNEL_MAX + i].input);
exit_input_alloc_failed:
	for (i = 0; i < err_num; i++)
		input_free_device(aw9610x->aw_channel[j * AW_CHANNEL_MAX + i].input);
err_requst_irq:
	if (gpio_is_valid(aw9610x->irq_gpio))
		devm_gpio_free(&i2c->dev, aw9610x->irq_gpio);
#ifdef AW_PINCTRL_ON
err_pinctrl:
	aw9610x_pinctrl_deinit(aw9610x);
#endif
err_first_irq:
err_pase_dt:
err_vers_load:
err_chipid:
#ifdef AW_POWER_ON
err_wait_chip_init:
err_get_voltage:
	if (aw9610x->power_enable) {
		regulator_disable(aw9610x->vcc);
		regulator_put(aw9610x->vcc);
	}
#endif
err_malloc:
	return ret;
}

static int32_t aw9610x_i2c_remove(struct i2c_client *i2c)
{
	struct aw9610x *aw9610x = i2c_get_clientdata(i2c);
	uint32_t i = 0;
	uint32_t j = aw9610x->sar_num;

#ifdef AW_POWER_ON
	if (aw9610x->power_enable) {
		regulator_disable(aw9610x->vcc);
		regulator_put(aw9610x->vcc);
	}
#endif

#ifdef AW_PINCTRL_ON
	aw9610x_pinctrl_deinit(aw9610x);
#endif
	if (gpio_is_valid(aw9610x->irq_gpio))
		devm_gpio_free(&i2c->dev, aw9610x->irq_gpio);

	for (i = 0; i < AW_CHANNEL_MAX; i++)
		input_free_device(aw9610x->aw_channel[j * AW_CHANNEL_MAX + i].input);

	for (i = 0; i < AW_CHANNEL_MAX; i++)
		input_unregister_device(aw9610x->aw_channel[j * AW_CHANNEL_MAX + i].input);

	sysfs_remove_group(&i2c->dev.kobj, &aw9610x_sar_attribute_group);
	return 0;
}

static int aw9610x_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct aw9610x *aw9610x = i2c_get_clientdata(client);
	uint32_t reg_val = 0;

	AWLOGD(aw9610x->dev, "suspend enter");

	disable_irq(aw9610x->to_irq);
	aw9610x_i2c_read(aw9610x, REG_HOSTIRQSRC, &reg_val);
	aw9610x_i2c_write(aw9610x, REG_CMD, AW9610X_SLEEP_MODE);

	return 0;
}

static int aw9610x_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct aw9610x *aw9610x = i2c_get_clientdata(client);

	AWLOGD(aw9610x->dev, "resume enter");

	aw9610x_i2c_write(aw9610x, REG_CMD, AW9610X_ACTIVE_MODE);
	enable_irq(aw9610x->to_irq);

	return 0;
}

static void aw9610x_i2c_shutdown(struct i2c_client *i2c)
{
	struct aw9610x *aw9610x = i2c_get_clientdata(i2c);
	uint32_t irq_status_temp = 0;

	pr_info("%s enter", __func__);

	disable_irq(aw9610x->to_irq);
	/* read clear interrupt*/
	aw9610x_i2c_read(aw9610x, REG_HOSTIRQSRC, &irq_status_temp);
	aw9610x_i2c_write(aw9610x, REG_CMD, AW9610X_SLEEP_MODE);
}

static const struct dev_pm_ops aw9610x_pm_ops = {
	.suspend = aw9610x_suspend,
	.resume = aw9610x_resume,
};

static const struct of_device_id aw9610x_dt_match[] = {
	{ .compatible = "awinic,aw9610x_sar_0" },
	{ .compatible = "awinic,aw9610x_sar_1" },
	{ },
};

static const struct i2c_device_id aw9610x_i2c_id[] = {
	{ AW9610X_I2C_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aw9610x_i2c_id);

static struct i2c_driver aw9610x_i2c_driver = {
	.driver = {
		.name = AW9610X_I2C_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(aw9610x_dt_match),
		.pm = &aw9610x_pm_ops,
	},
	.probe = aw9610x_i2c_probe,
	.remove = aw9610x_i2c_remove,
	.shutdown = aw9610x_i2c_shutdown,
	.id_table = aw9610x_i2c_id,
};

static int32_t __init aw9610x_i2c_init(void)
{
	int32_t ret = 0;

	pr_info("aw9610x driver version %s\n", AW9610X_DRIVER_VERSION);

	ret = i2c_add_driver(&aw9610x_i2c_driver);
	if (ret) {
		pr_err("fail to add aw9610x device into i2c\n");
		return ret;
	}

	return 0;
}

late_initcall(aw9610x_i2c_init);
static void __exit aw9610x_i2c_exit(void)
{
	i2c_del_driver(&aw9610x_i2c_driver);
}
module_exit(aw9610x_i2c_exit);
MODULE_DESCRIPTION("AW9610X SAR Driver");

MODULE_LICENSE("GPL v2");
