/*
 * Touchkey driver for CYPRESS4000 controller
 *
 * Copyright (C) 2018 nubia
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/i2c.h>
//#include <linux/i2c/mcs.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/pm.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/of_gpio.h>
#include <linux/reboot.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/consumer.h>
#include "cy8c4014l_touchkey.h"
#include "cy8c4014l_touchkey_StringImage.h"
#include "cy8c4014l_touchkey_firmware_update.h"

static struct cypress_info *g_cypress_touch_key_info = NULL;
int last_value_left= 0;
static int cypress_get_mode_left() {
	struct i2c_client *client = g_cypress_touch_key_info->i2c;
	return i2c_smbus_read_byte_data(client, CYPRESS4000_TOUCHKEY_MODE);
}

static int cypress_set_mode_left(int mode) {
	int ret = -1;
	int cur_mode = 0;
	int i = 0;
	struct i2c_client *client = g_cypress_touch_key_info->i2c;
	struct cypress_info *info = g_cypress_touch_key_info;
	struct input_dev *input = info->platform_data->input_dev;

	cur_mode = cypress_get_mode_left();
	if(cur_mode == mode){
        pr_err("cypress_touchkey_left:[%s] mode already is %d now!!\n", __func__, mode);
		return mode;
	}
	if(mode == CYPRESS4000_SLEEP_MODE) {
	if(last_value_left) {
	input_report_key(input, KEY_F8, 0);
    input_sync(input);
    pr_err("cypress_touchkey_left:[%s] Send right up\n", __func__);
		}
	}
	msleep(50);
	ret = i2c_smbus_write_byte_data(client,CYPRESS4000_TOUCHKEY_MODE, mode);
	if(ret<0){
		while(i < 10){
		    i++;

			msleep(50);
			ret = i2c_smbus_write_byte_data(client,CYPRESS4000_TOUCHKEY_MODE, mode);
			pr_err("cypress_touchkey_left:[%s] Set mode=%d success!ret=0x%x\n", __func__, mode, ret);
			if(ret >= 0)
			{
				pr_err("cypress_touchkey_left:[%s] Set5 mode=%d success!ret=0x%x\n", __func__, mode, ret);
				break;
			}

		}
	}

	pr_err("cypress_touchkey_left:[%s] set touchkey to %s mode!!\n", __func__, (mode == CYPRESS4000_SLEEP_MODE)?"sleep":"wake");
	return mode;
}

static ssize_t touchkey_command_store(struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned int input;
    int ret;
    struct i2c_client *client;
	int ret_right = 0;
	if(!g_cypress_touch_key_info->bUpdateOver_left){
		dev_warn(dev, "cypress_touchkey_left:%s:touchkey_key in update, don't react\n", __func__);
		return ret_right;
	}
    client = container_of(dev, struct i2c_client, dev);
    if (sscanf(buf, "%u", &input) != 1)
    {
        dev_info(dev, "cypress_touchkey_left:Failed to get input message!\n");
        return -EINVAL;
    }
    /* 0x1 enter read cp mode
        0x2 soft reset IC ,other command are not supported for now*/
    if(input == 0x1 || input == 0x2)
    {
        ret = i2c_smbus_write_byte_data(client,CYPRESS4000_TOUCHKEY_CMD, input);
        if(ret<0)
        {
            dev_err(&client->dev, "cypress_touchkey_left:Failed to set command 0x%x!\n",input);
        }
    }
    else
    {
        dev_info(dev, "cypress_touchkey_left:Command 0x%x not support!\n",input);
    }
    return count;
}
static ssize_t touchkey_command_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    int cmd = 0;
    struct i2c_client *client;
	int ret_right = 0;
	if(!g_cypress_touch_key_info->bUpdateOver_left){
		dev_warn(dev, "cypress_touchkey_left:%s:touchkey_key in update, don't react\n", __func__);
		return ret_right;
	}
    client = container_of(dev, struct i2c_client, dev);
    cmd = i2c_smbus_read_byte_data(client, CYPRESS4000_TOUCHKEY_CMD);
    if(cmd < 0)
    {
        dev_info(dev, "cypress_touchkey_left:Failed to get command state(0x%x)!\n",cmd);
        return scnprintf(buf,CYPRESS_BUFF_LENGTH, "cmd: error\n");
    }
    return scnprintf(buf,CYPRESS_BUFF_LENGTH, "cmd: 0x%x\n", cmd);
}

static ssize_t touchkey_mode_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    int mode = 0;
//    struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	if(!g_cypress_touch_key_info->bUpdateOver_left) {
		mode = g_cypress_touch_key_info->new_mode_left;
        dev_err(dev, "cypress_touchkey_left:firmware updating, get touchkey mode=%s!\n", (mode == CYPRESS4000_SLEEP_MODE)?"sleep":"wake");
	} else {
	    mode = cypress_get_mode_left();
	}
    //dev_info(dev, "touchkey_mode_show [0x%x]\n", mode);
    return scnprintf(buf,CYPRESS_BUFF_LENGTH, "mode: %s(0x%x)\n",
        (mode == CYPRESS4000_WAKEUP_MODE)?"wakeup":((mode == CYPRESS4000_SLEEP_MODE)?"sleep":"unknown"),
        mode);
}

static ssize_t touchkey_mode_store(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
    int ret = -1;
    int mode = -1;

    if(buf[0] == '0'){
        mode = CYPRESS4000_SLEEP_MODE;
    }else if(buf[0] == '1'){
        mode = CYPRESS4000_WAKEUP_MODE;
    }else{
        dev_err(dev, "cypress_touchkey_left:mode set failed, 0: sleep, 1: wakeup, %c unknown\n", buf[0]);
        return count;
    }
	g_cypress_touch_key_info->new_mode_left = mode;
	if(!g_cypress_touch_key_info->bUpdateOver_left) {
        dev_err(dev, "cypress_touchkey_left:firmware updating, set touchkey to %s mode delayed!\n", (mode == CYPRESS4000_SLEEP_MODE)?"sleep":"wake");
        return count;
	}
    ret = cypress_set_mode_left(mode);
    return count;
}

static ssize_t touchkey_firmversion_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
    int version = 0;
    struct i2c_client *client;
	int ret_right = 0;
	if(!g_cypress_touch_key_info->bUpdateOver_left){
		dev_warn(dev, "cypress_touchkey_left:%s:touchkey_key in update, don't react\n", __func__);
		return ret_right;
	}
    client = container_of(dev, struct i2c_client, dev);
    version = i2c_smbus_read_byte_data(client, CYPRESS4000_TOUCHKEY_FW);
	//dev_info(dev, "touchkey_firmversion_show [0x%x]\n", version);
	return scnprintf(buf,CYPRESS_BUFF_LENGTH, "firmware version: 0x%x\n", version);
}

static ssize_t touchkey_firmversion_store(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
	int version = 0;
	struct i2c_client *client;
	int ret_right = 0;
	if(!g_cypress_touch_key_info->bUpdateOver_left){
		dev_warn(dev, "cypress_touchkey_left:%s:touchkey_key in update, don't react\n", __func__);
		return ret_right;
	}
    client = container_of(dev, struct i2c_client, dev);

    dev_err(&client->dev, "cypress_touchkey_left:buf: %s \n", buf);

	version = i2c_smbus_read_byte_data(client, CYPRESS4000_TOUCHKEY_FW);

	if ((buf[0] == 'f') || (((version != CURRENT_NEW_FIRMWARE_VER_4024) && (g_cypress_touch_key_info->siliconId == CYPRESS_CHIP_ID_4024))
		|| (((version != CURRENT_NEW_FIRMWARE_VER_4025)) && (g_cypress_touch_key_info->siliconId == CYPRESS_CHIP_ID_4024))))
	{
		int ret = -1; //0:success
		disable_irq(client->irq);
		dev_info(&client->dev, "cypress_touchkey_left:Ready to update firmware\n");
		if (g_cypress_touch_key_info->siliconId == CYPRESS_CHIP_ID_4024)
			ret = cypress_left_firmware_update(client,stringImage_left_0, LINE_CNT_0);
		else if (g_cypress_touch_key_info->siliconId == CYPRESS_CHIP_ID_4025)
			ret = cypress_left_firmware_update(client,stringImage_left_1, LINE_CNT_1);
		else
			printk(KERN_ERR"cypress_touchkey_left::siliconId error, please make sure pass the right id\n");

		if (ret < 0)
			dev_err(&client->dev, "cypress_touchkey_left:cypress Firmware update fail,cur ver is :0x%x,ret=%x\n", version,ret);
		else
			dev_err(&client->dev, "cypress_touchkey_left:cypress Firmware update success, cur ver is :0x%x\n", version);
	
		enable_irq(client->irq);
	}else{
		dev_err(&client->dev,"cypress_touchkey_left:cypress Firmware version(0x%x) is newest!!", version);
	}

	return count;
}

static ssize_t touchkey_reglist_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    u8 regval = 0;
    u8 regaddr = CYPRESS4000_KEY0_RAWDATA0;
    int size = 0;
    struct i2c_client *client;
	int ret_right = 0;
	if(!g_cypress_touch_key_info->bUpdateOver_left){
		dev_warn(dev, "cypress_touchkey_left:%s:touchkey_key in update, don't react\n", __func__);
		return ret_right;
	}
    client = container_of(dev, struct i2c_client, dev);

    for(regaddr = CYPRESS4000_KEY0_RAWDATA0;regaddr <= CYPRESS4000_KEY1_CP3;regaddr++)
    {
        regval = i2c_smbus_read_byte_data(client, regaddr);
        size += scnprintf(buf + size , CYPRESS_BUFF_LENGTH - size, "Reg[0x%x] :%d\n",regaddr,regval);
        //dev_info(dev, "size=%d,reg[0x%x]=%d\n", size,regaddr,regval);
    }
    return size;
}
static ssize_t touchkey_signal_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    u8 raw0 = 0;
    u8 raw1 = 0;
    u8 base0 = 0;
    u8 base1 = 0;
    int size = 0;

    struct i2c_client *client;
	int ret_right = 0;
	if(!g_cypress_touch_key_info->bUpdateOver_left){
		dev_warn(dev, "cypress_touchkey_left:%s:touchkey_key in update, don't react\n", __func__);
		return ret_right;
	}
    client = container_of(dev, struct i2c_client, dev);

    //raw0 = i2c_smbus_read_byte_data(client, CYPRESS4000_KEY0_RAWDATA0);
    //raw1 = i2c_smbus_read_byte_data(client, CYPRESS4000_KEY0_RAWDATA1);
    //base0 = i2c_smbus_read_byte_data(client, CYPRESS4000_KEY0_BASELINE0);
    //base1 = i2c_smbus_read_byte_data(client, CYPRESS4000_KEY0_BASELINE1);
    //size += scnprintf(buf + size, CYPRESS_BUFF_LENGTH - size,
    //    "[Key0 signal %d]:%d %d %d %d\n",
    //    (((raw0 << 8) | raw1) - ((base0 << 8) | base1)),raw0,raw1,base0,base1);


    raw0 = i2c_smbus_read_byte_data(client, CYPRESS4000_KEY1_RAWDATA0);
    raw1 = i2c_smbus_read_byte_data(client, CYPRESS4000_KEY1_RAWDATA1);
    base0 = i2c_smbus_read_byte_data(client, CYPRESS4000_KEY1_BASELINE0);
    base1 = i2c_smbus_read_byte_data(client, CYPRESS4000_KEY1_BASELINE1);
    size += scnprintf(buf + size, CYPRESS_BUFF_LENGTH - size,
        "[Key1 signal %d]:%d %d %d %d\n",
        (((raw0 << 8) | raw1) - ((base0 << 8) | base1)),raw0,raw1,base0,base1);

	return size;
}

static ssize_t touchkey_Lsensitivity_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
    int level = 0;
    int r_level = 0;
    struct i2c_client *client;
	int ret_right = 0;
	if(!g_cypress_touch_key_info->bUpdateOver_left){
		dev_warn(dev, "cypress_touchkey_left:%s:touchkey_key in update, don't react\n", __func__);
		return ret_right;
	}
    client = container_of(dev, struct i2c_client, dev);
    r_level = i2c_smbus_read_byte_data(client, CYPRESS4000_TOUCHKEY_LSENSITIVITY);
    dev_err(&client->dev, "cypress_touchkey_left:%s:r_level: %x \n", __func__,r_level);
    level = (r_level > 6)?(r_level - 6):r_level;
    return scnprintf(buf,CYPRESS_BUFF_LENGTH, "Left sensitivity level: %d\n", level);
}

static ssize_t touchkey_Rsensitivity_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
    int level = 0;
    int r_level = 0;
    struct i2c_client *client;
	int ret_right = 0;
	if(!g_cypress_touch_key_info->bUpdateOver_left){
		dev_warn(dev, "cypress_touchkey_left:%s:touchkey_key in update, don't react\n", __func__);
		return ret_right;
	}
    client = container_of(dev, struct i2c_client, dev);
    r_level = i2c_smbus_read_byte_data(client, CYPRESS4000_TOUCHKEY_RSENSITIVITY);
    dev_err(&client->dev, "cypress_touchkey_left:%s:r_level: %x \n", __func__,r_level);
    level = (r_level > 6)?(r_level - 6):r_level;
    return scnprintf(buf,CYPRESS_BUFF_LENGTH, "Right sensitivity level: %d\n", level);
}

static int touchkey_sensitivity_isvalid(const char *buf)
{
    int level;

    if(buf[0] > '0' && buf[0] <= '3'){
        level = buf[0] - '0';
    }else{
        level = -1;
    }

    return level;
}

static ssize_t touchkey_Lsensitivity_store(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
    int ret = -1;
    int level;
    int new_level;
    struct i2c_client *client;
	int ret_right = 0;
	if(!g_cypress_touch_key_info->bUpdateOver_left){
		dev_warn(dev, "cypress_touchkey_left:%s:touchkey_key in update, don't react\n", __func__);
		return ret_right;
	}
    client = container_of(dev, struct i2c_client, dev);

    level = i2c_smbus_read_byte_data(client, CYPRESS4000_TOUCHKEY_LSENSITIVITY);

    dev_err(&client->dev, "cypress_touchkey_left:buf: %s \n", buf);
    new_level = touchkey_sensitivity_isvalid(buf);

    if(new_level == level || new_level < 0)
    {
        return count;
    }

    ret = i2c_smbus_write_byte_data(client,CYPRESS4000_TOUCHKEY_LSENSITIVITY, new_level);
    if(ret < 0){
        dev_err(&client->dev, "cypress_touchkey_left:Set %d Lsensitivity error!\n", new_level);
    }

    dev_err(&client->dev, "cypress_touchkey_left:Set Lsensitivity level: %d OK\n", new_level);
    return count;
}

static ssize_t touchkey_Rsensitivity_store(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
    int ret = -1;
    int level;
    int new_level;
    struct i2c_client *client;
	int ret_right = 0;
	if(!g_cypress_touch_key_info->bUpdateOver_left){
		dev_warn(dev, "cypress_touchkey_left:%s:touchkey_key in update, don't react\n", __func__);
		return ret_right;
	}
    client = container_of(dev, struct i2c_client, dev);

    level = i2c_smbus_read_byte_data(client, CYPRESS4000_TOUCHKEY_RSENSITIVITY);

    dev_err(&client->dev, "cypress_touchkey_left:buf: %s \n", buf);
    new_level = touchkey_sensitivity_isvalid(buf);

    if(new_level == level || new_level < 0)
    {
        return count;
    }

    ret = i2c_smbus_write_byte_data(client,CYPRESS4000_TOUCHKEY_RSENSITIVITY, new_level);
    if(ret < 0){
        dev_err(&client->dev, "cypress_touchkey_left:Set %d Rsensitivity error!\n", new_level);
    }

    dev_err(&client->dev, "cypress_touchkey_left:Set Rsensitivity level: %d OK\n", new_level);
    return count;
}

static struct device_attribute attrs[] = {
    __ATTR(mode, S_IRUGO|S_IWUSR|S_IXUGO,touchkey_mode_show, touchkey_mode_store),
    __ATTR(firm_version, S_IRUGO|S_IWUSR|S_IXUGO,touchkey_firmversion_show, touchkey_firmversion_store),
    __ATTR(reg_list, S_IRUGO|S_IXUGO,touchkey_reglist_show, NULL),
    __ATTR(key_signal, S_IRUGO|S_IXUGO,touchkey_signal_show, NULL),
    __ATTR(command, S_IRUGO|S_IXUGO|S_IWUSR|S_IWGRP,touchkey_command_show, touchkey_command_store),
    __ATTR(L_sensitivity, S_IRUGO|S_IXUGO|S_IWUSR|S_IWGRP,touchkey_Lsensitivity_show, touchkey_Lsensitivity_store),
    __ATTR(R_sensitivity, S_IRUGO|S_IXUGO|S_IWUSR|S_IWGRP,touchkey_Rsensitivity_show, touchkey_Rsensitivity_store)
};

//#define CYPRESS_AVDD_VOL 2955000
static int cypress_power_on(struct cypress_platform_data *pdata, bool val)
{
    int ret = 0;
    if (val)
    {
        if ( pdata->avdd_ldo && !(IS_ERR(pdata->avdd_ldo))) {
            pr_err("cypress_touchkey_left:%s: enable _left_avdd_ldo\n", __func__);
          //  ret = regulator_set_voltage(pdata->avdd_ldo, CYPRESS_AVDD_VOL, CYPRESS_AVDD_VOL);
			
           // if(ret){
             //   pr_err("%s: set avdd ldo to %d error(%d)\n", __func__, CYPRESS_AVDD_VOL, ret);
              //  return -1;
            //}

//            pr_err("%s: avdd ldo state1: %d\n", __func__, regulator_is_enabled(pdata->avdd_ldo));

            ret = regulator_enable(pdata->avdd_ldo);
            if(ret){
                pr_err("cypress_touchkey_left:%s: enable avdd _left_ldo error(%d)\n", __func__, ret);
                return -1;
            }

  //          pr_err("%s: avdd ldo state2: %d\n", __func__, regulator_is_enabled(pdata->avdd_ldo));
        }else{
            pr_err("cypress_touchkey_left:%s: avdd_ldo not exist!\n", __func__);
        }
        gpio_set_value(pdata->power1_gpio, 0);
		pr_err("cypress_touchkey_left:%s: cpress power1_gpio to set gpio %d to %d\n", pdata->power1_gpio, pdata->power_on_flag);
        //power on 1p8
        if (pdata->power_gpio < 0) {
            pr_err("cypress_touchkey_left:%s:power_gpio _left_not set!!!\n", __func__);
            return -1;
        }

        //pr_info("%s:start power on 1v8\n", __func__);
        pr_info("cypress_touchkey_left:%s: set gpio %d to %d\n", __func__, pdata->power_gpio, pdata->power_on_flag);
        ret = gpio_request(pdata->power_gpio, "cypress_touchkey");
        //if (ret) {
          //  pr_err("%s: Failed to get gpio %d (ret: %d)\n",__func__, pdata->power_gpio, ret);
          //  return ret;
       // }
//        ret = regulator_enable(pdata->power_gpio);
		gpio_set_value(pdata->power_gpio, 0);
		pr_err("cypress_touchkey_left:%s: power-gpio to set gpio %d to %d\n", pdata->power_gpio, pdata->power_on_flag);
				

        ret = gpio_direction_output(pdata->power_gpio, pdata->power_on_flag);
        //if (ret) {
          //  pr_err("%s: Failed to set gpio %d to %d\n", pdata->power_gpio, pdata->power_on_flag);
            //return ret;
        //}

       // gpio_free(pdata->power_gpio);
		//gpio_set_value(pdata->power_gpio, 0);
		pr_err("cypress_touchkey_left:%s: power-gpio to set gpio %d to %d\n", pdata->power_gpio, pdata->power_on_flag);
    }
    else
    {
    //tp use the power,we not need power off it
    //add power off func
    }
    return ret;
}

static irqreturn_t cypress_touchkey_interrupt(int irq, void *dev_id)
{

    struct cypress_info * info = dev_id;
    struct i2c_client *client = info->i2c;
    struct input_dev *input = info->platform_data->input_dev;
    u8 val;
    int cur_value;
    int status;

    //read key value single keys value are 0x01, 0x02, both pressed keys value is 0x03
    val = i2c_smbus_read_byte_data(client, CYPRESS4000_TOUCHKEY_KEYVAL);
    if (val < 0) {
        dev_err(&client->dev, "cypress_touchkey_left:cypress key read error [%d]\n", val);
        goto out;
    }
    cur_value = (val == 0xff ) ? 0x0 : val;
    status = last_value_left^ cur_value;
    if(status & CYPRESS_LEFT_BIT)
    {
        input_report_key(input, CYPRESS_KEY_LEFT, (cur_value & CYPRESS_LEFT_BIT)?1:0);
        input_sync(input);
    }
    if(status & CYPRESS_RIGHT_BIT)
    {
        input_report_key(input, KEY_F8, (cur_value & CYPRESS_RIGHT_BIT)?1:0);
        input_sync(input);
    }
    last_value_left = cur_value;
 out:
    return IRQ_HANDLED;
}
static int parse_dt(struct device *dev, struct cypress_platform_data *pdata)
{
    int retval;
    u32 value;
    struct device_node *np = dev->of_node;

    pdata->irq_gpio = of_get_named_gpio_flags(np,
        "touchkey,irq-gpio", 0, NULL);

    retval = of_property_read_u32(np, "touchkey,irq-flags", &value);
    if (retval < 0){
	dev_err(dev, "cypress_touchkey_left:parse irq-flags error\n");
        return retval;
    }else{
        pdata->irq_flag = value;
    }
	/*

    pdata->power_gpio = of_get_named_gpio_flags(np,
        "touchkey,power-gpio", 0, NULL);
	printk("cpress touchkey,power-gpio = %d \n", pdata->power_gpio);
	*/

    retval = of_property_read_u32(np, "touchkey,power-on-flag", &value);
    if (retval < 0){
	dev_err(dev, "cypress_touchkey_left:parse power-on-flag error\n");
        return retval;
    }else{
        pdata->power_on_flag = value;
    }
	
	/************reset*************/
	
	pdata->power1_gpio = of_get_named_gpio(np, "touchkey,power1-gpio", 0);
	if (pdata->power1_gpio < 0) {
		printk("cypress_touchkey_left:falied to get power1_gpio!\n");
		return pdata->power1_gpio;
	}
	printk("cypress_touchkey_left:cpress power1_gpio:%d\n", pdata->power1_gpio);
	gpio_free(pdata->power1_gpio);
	retval = devm_gpio_request(dev, pdata->power1_gpio, "power1-gpio");
	if (retval< 0) {
		printk("cypress_touchkey_left: cpress failed to request power1_gpio, retval = %d\n", retval);
		//return retval;
	}
	gpio_direction_output(pdata->power1_gpio, 0);
		//wzm
	
   // r = devm_gpio_request_one(dev, pdata->power1_gpio,
	//			  GPIOF_OUT_INIT_LOW, "power1-gpio");
	
    pdata->avdd_ldo = regulator_get_optional(dev, "touchkey,avdd");
	pdata->avdd_ldo = regulator_get_optional(dev, "touchkey,avdd");

    return 0;
}

static void cypress_touch_key_update_work_func_left(struct work_struct *work)
{
    struct cypress_info *info = g_cypress_touch_key_info;
//    struct cypress_platform_data *pdata = NULL;
    struct i2c_client *client = NULL;
    int fw_ver = -1;
	int ret = -1; //0:success
	int sleep;

    //read fw version and update

    if(info == NULL){
        pr_err("cypress_touchkey_left:%s: g_cypress_touch_key_info_left is null\n", __func__);
        goto exit;
    }

    client = info->i2c;
//    pdata = info->platform_data;
//    if((client == NULL) || (pdata == NULL)){
    if(client == NULL){
        pr_err("cypress_touchkey_left:%s: i2c client_left is null\n", __func__);
        goto exit;
    }

    pr_info("cypress_touchkey_left:%s: start check_left fw version\n", __func__);
	
	ret = cypress_left_read_chip_id(info);
	dev_err(&client->dev, "cypress_touchkey_left:cypress_left_read_chip_id,ret=%x\n", ret);
	if (!ret)
		printk(KERN_ERR"cypress_touchkey_left:: read chip id success, siliconId = 0x%x, siliconRev = 0x%x\n", info->siliconId, info->siliconRev);
	else
		printk(KERN_ERR"cypress_touchkey_left:: read chip id error\n");
	
	if (info->siliconId == CYPRESS_CHIP_ID_4024)
		dev_err(&client->dev, "cypress_touchkey_left:current_left fw version:[0x%x] , new_left fw version [0x%x]\n", info->fw_ver,CURRENT_NEW_FIRMWARE_VER_4024);
	else if(info->siliconId == CYPRESS_CHIP_ID_4025)
		dev_err(&client->dev, "cypress_touchkey_left:current_left fw version:[0x%x] , new_left fw version [0x%x]\n", info->fw_ver,CURRENT_NEW_FIRMWARE_VER_4025);
	else if (info->siliconId == CYPRESS_CHIP_ID_4024_S412)
		dev_err(&client->dev, "cypress_touchkey_left:current fw version:[0x%x] , new_left fw version [0x%x]\n", info->fw_ver,CURRENT_NEW_FIRMWARE_VER_4024_S412);
    if (((info->siliconId == CYPRESS_CHIP_ID_4024) && (info->fw_ver != CURRENT_NEW_FIRMWARE_VER_4024))
		|| ((info->siliconId == CYPRESS_CHIP_ID_4025) && (info->fw_ver != CURRENT_NEW_FIRMWARE_VER_4025))
		|| ((info->siliconId == CYPRESS_CHIP_ID_4024_S412) && (info->fw_ver != CURRENT_NEW_FIRMWARE_VER_4024_S412)))
    {

		ret = -1;
        dev_info(&client->dev, "cypress_touchkey_left:Ready to update firmware\n");
        disable_irq_nosync(info->irq);

		if (info->siliconId == CYPRESS_CHIP_ID_4024)
			ret = cypress_left_firmware_update(client,stringImage_left_0, LINE_CNT_0);
		else if (info->siliconId == CYPRESS_CHIP_ID_4025)
			ret = cypress_left_firmware_update(client,stringImage_left_1, LINE_CNT_1);
		else if (info->siliconId == CYPRESS_CHIP_ID_4024_S412)
			ret = cypress_left_firmware_update(client,stringImage_left_2, LINE_CNT_2);
		else
			printk(KERN_ERR"cypress_touchkey_left::siliconId error, please make sure pass the right id\n");

        if (ret){
            dev_err(&client->dev, "cypress_touchkey_left:cypress_left Firmware update fail,cur ver is :%x,ret=%x\n", info->fw_ver,ret);
		    g_cypress_touch_key_info->bUpdateOver_left = true;
		} else {
            msleep(100);
            fw_ver = i2c_smbus_read_byte_data(client, CYPRESS4000_TOUCHKEY_FW);
			info->fw_ver = fw_ver;
			g_cypress_touch_key_info->bUpdateOver_left = true;
            dev_err(&client->dev, "cypress_touchkey_left:cypress Firmware update success,new version: 0x%x\n", fw_ver);
		}
        enable_irq(info->irq);
		complete(&info->update_completion);
    }else{
		if ((info->siliconId == CYPRESS_CHIP_ID_4024) || ((info->siliconId == CYPRESS_CHIP_ID_4025)) || ((info->siliconId == CYPRESS_CHIP_ID_4024_S412)))
			pr_info("cypress_touchkey_left:%s: fw version is newest, not need update fw\n", __func__);
		else
			pr_info("cypress_touchkey_left:%s: siliconId error\n", __func__);
		
		g_cypress_touch_key_info->bUpdateOver_left = true;
		complete(&info->update_completion);
    }
	sleep = cypress_set_mode_left(CYPRESS4000_SLEEP_MODE);
exit:
    cypress_set_mode_left(g_cypress_touch_key_info->new_mode_left);
}

static int cp_pinctrl_init(struct device *dev, struct cypress_platform_data *pdata)
{
	int retval = 0;

	/* Get pinctrl if target uses pinctrl */
	pdata->ts_pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR_OR_NULL(pdata->ts_pinctrl)) {
		retval = PTR_ERR(pdata->ts_pinctrl);
		pr_err("cypress_touchkey_left:%s Target does not use pinctrl %d\n", __func__, retval);
		goto err_pinctrl_get;
	}

	pdata->pinctrl_state_active
		= pinctrl_lookup_state(pdata->ts_pinctrl, "cypress_int_active_left");
	if (IS_ERR_OR_NULL(pdata->pinctrl_state_active)) {
		retval = PTR_ERR(pdata->pinctrl_state_active);
		pr_err("cypress_touchkey_left:%s Can not lookup cypress_int_active_left pinstate %d\n", __func__, retval);
		goto err_pinctrl_lookup;
	}



	return retval;

err_pinctrl_lookup:
	devm_pinctrl_put(pdata->ts_pinctrl);
err_pinctrl_get:
	pdata->ts_pinctrl = NULL;
	return retval;
}

static int cypress_touchkey_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct cypress_info *info = NULL;
    struct cypress_platform_data *pdata = client->dev.platform_data;
    struct input_dev *input_dev = NULL;
    int ret = 0;
//    int fw_ver;
    int attr_count = 0;
//    msleep(10000);
    dev_info(&client->dev, "cypress_touchkey_left:Cypress probe start!\n");
    info = kzalloc(sizeof(struct cypress_info), GFP_KERNEL);
    if (!info)
    {
        dev_err(&client->dev, "cypress_touchkey_left:kzalloc failed!\n");
        ret= -ENOMEM;
        goto info_err;
    }

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) 
    {
        dev_err(&client->dev, "cypress_touchkey_left:i2c_check_functionality error!\n");
        ret = -ENODEV;
        goto info_err;
    }
	g_cypress_touch_key_info = info;
    if (client->dev.of_node)
    {
        pdata = devm_kzalloc(&client->dev,
            sizeof(struct cypress_platform_data),
            GFP_KERNEL);
        if (!pdata)
        {
            dev_err(&client->dev, "cypress_touchkey_left:Failed to allocate memory\n");
            ret = -ENOMEM;
            goto info_err;
        }

		ret = cp_pinctrl_init(&client->dev, pdata);
		if (!ret && pdata->ts_pinctrl) {
			ret = pinctrl_select_state(
					pdata->ts_pinctrl,
					pdata->pinctrl_state_active);
			if (ret < 0) {
				pr_err("cypress_touchkey_left:%s: Failed to select cypress_int_active pinstate %d\n", __func__, ret);
			}
		}

        client->dev.platform_data = pdata;
        ret = parse_dt(&client->dev,pdata);
        if (ret)
        {
            dev_err(&client->dev, "cypress_touchkey_left:Cypress parse device tree error\n");
            goto data_err;
        }
        else
        {
            dev_info(&client->dev, "cypress_touchkey_left:Cypress_left irq gpio:%d,irg flag:0x%x\n",pdata->irq_gpio,pdata->irq_flag);
        }
    }
    else
    {
        pdata = client->dev.platform_data;
        if (!pdata)
        {
            dev_err(&client->dev, "cypress_touchkey_left:No platform data\n");
            ret = -ENODEV;
            goto info_err;
        }
    }
	init_completion(&info->update_completion);
    cypress_power_on(pdata, true);
	

    input_dev = input_allocate_device();
    if(input_dev == NULL)
    {
       dev_info(&client->dev, "cypress_touchkey_left:Failed to allocate input device !\n");
       ret= -ENOMEM;
       goto info_err;
    }
    input_dev->name = "nubia_tgk_touchkey";
    input_dev->id.bustype = BUS_I2C;
    input_dev->dev.parent = &client->dev;
    __set_bit(EV_KEY, input_dev->evbit);
    __set_bit(CYPRESS_KEY_LEFT, input_dev->keybit);
    __set_bit(KEY_F8, input_dev->keybit);
    ret = input_register_device(input_dev);
    if (ret)
    {
        dev_info(&client->dev, "cypress_touchkey_left:Failed to register input device !\n");
        goto input_err;
    }

    pdata->input_dev = input_dev;
    info->i2c = client;
    info->platform_data = pdata;

    info->irq = gpio_to_irq(pdata->irq_gpio);
    ret = gpio_direction_input(pdata->irq_gpio);
    if(ret)
    {
        dev_err(&client->dev, "cypress_touchkey_left:Failed to set gpio\n");
        goto data_err;
    }

    info->cypress_update_wq = create_singlethread_workqueue("cypress_update_work_left");
    INIT_DELAYED_WORK(&info->cypress_update_work_left, cypress_touch_key_update_work_func_left);
    i2c_set_clientdata(client, info);

    ret = request_threaded_irq(info->irq, NULL, cypress_touchkey_interrupt,
        pdata->irq_flag,client->dev.driver->name, info);
    if (ret)
    {
        dev_err(&client->dev, "cypress_touchkey_left:Failed to register interrupt\n");
        goto irq_err;
    }

    for (attr_count = 0; attr_count < ARRAY_SIZE(attrs); attr_count++)
    {
        ret = sysfs_create_file(&client->dev.kobj,&attrs[attr_count].attr);
        if (ret < 0)
        {
            dev_err(&client->dev,"cypress_touchkey_left:%s: Failed to create sysfs attributes\n",__func__);
        }
    }

	info->new_mode_left = CYPRESS4000_SLEEP_MODE;
	info->bUpdateOver_left = false;

    queue_delayed_work(info->cypress_update_wq, &info->cypress_update_work_left, 3 / 10 * HZ);
    return 0;

irq_err:
    free_irq(info->irq, info);
data_err:
    //devm_kfree(pdata);
input_err:
    input_free_device(input_dev);
info_err:
    kfree(info);
	g_cypress_touch_key_info = NULL;
    return ret;
}


static int cypress_touchkey_remove(struct i2c_client *client)
{
#if 0
	struct cypress_touchkey_data *data = i2c_get_clientdata(client);
    int attr_count = 0;
	free_irq(client->irq, data);
	cypress_power_on(false);
	input_unregister_device(data->input_dev);
	kfree(data);
    for (attr_count = 0; attr_count < ARRAY_SIZE(attrs); attr_count++)
    {
        sysfs_remove_file(&client->dev.kobj,&attrs[attr_count].attr);
    }
#endif
	return 0;
}

static void cypress_touchkey_shutdown(struct i2c_client *client)
{

 //   cypress_power_on(false);
}

#ifdef CONFIG_PM_SLEEP
static int cypress_touchkey_suspend(struct device *dev)
{

	int ret = 0;
	int ret_right = 0;
	//printk(KERN_ERR"cypress_touchkey_left:%s enter\n", __func__);
	if(!g_cypress_touch_key_info->bUpdateOver_left){
		#if 0
		dev_warn(dev, "cypress_touchkey_left:%s:touchkey_key in update, don't react\n", __func__);
		msleep(5000);
		#endif
		wait_for_completion(&g_cypress_touch_key_info->update_completion);
		//printk(KERN_ERR"cypress_touchkey_left:%s exit because fw updating\n", __func__);
		return ret_right;

	}
	//printk(KERN_ERR"cypress_touchkey_left:%s exit\n", __func__);

    return ret;
}

static int cypress_touchkey_resume(struct device *dev)
{

	int ret = 0;
	int ret_right = 0;
	//printk(KERN_ERR"cypress_touchkey_left:%s enter\n", __func__);
	if(!g_cypress_touch_key_info->bUpdateOver_left){
		//dev_warn(dev, "cypress_touchkey_left%s:touchkey_key in update, don't react\n", __func__);
		msleep(5000);
		return ret_right;
	}
	//printk(KERN_ERR"cypress_touchkey_left:%s exit\n", __func__);
    return ret;
}



static SIMPLE_DEV_PM_OPS(cypress_touchkey_pm_ops,
    cypress_touchkey_suspend, cypress_touchkey_resume);
#endif

static const struct i2c_device_id cypress_touchkey_id[] = {
    { "cypress,touchkey", 0 },
    {},
};
MODULE_DEVICE_TABLE(i2c, cypress_touchkey_id);


static struct of_device_id cypress_touchkey_match_table[] = {
        { .compatible = "cypress,touchkey_left-i2c",},
        {},
};

static struct i2c_driver cypress_touchkey_driver = {
    .driver = {
        .name = "cypress_touchkey_left",
        .owner = THIS_MODULE,
        .of_match_table = cypress_touchkey_match_table,
        .pm = &cypress_touchkey_pm_ops,
    },
    .probe = cypress_touchkey_probe,
    .remove = cypress_touchkey_remove,
    .shutdown = cypress_touchkey_shutdown,
    .id_table = cypress_touchkey_id,
};

module_i2c_driver(cypress_touchkey_driver);

/* Module information */
MODULE_AUTHOR("nubia, Inc.");
MODULE_DESCRIPTION("Touchkey driver for cy8c4014lqi-421");
MODULE_LICENSE("GPL");
