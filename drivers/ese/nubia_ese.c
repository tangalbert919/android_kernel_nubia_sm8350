/*
 * Nubia ese driver support for Sansung&Thalse ese device.
 *
 * Copyright (C) 2020-2021 Synaptics Incorporated. All rights reserved.
 *
 * Copyright (C) 2020 Niuxiaojun <xiaojun.niu@nubia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/types.h>
#include <linux/of_gpio.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#define ESE_COM_TEST

#define SPI_READ 0x80
#define ADDRESS_LEN (2)

#define MASK_16BIT 0xFFFF
#define MASK_8BIT 0xFF

#define GTO_IOC_WR_RESET 0x12

#define SPI_DRIVER_NAME "nubia,ese-spi"
#define ESE_LOG(fmt, args...) printk(KERN_ERR "[NB_ESE] [%s: %d] "fmt,  __func__, __LINE__, ##args)

struct mutex ese_mutex;
static struct spi_transfer *xfer=NULL;
struct spi_device *ese_spi = NULL;

extern int trig_cold_reset(void) ;
extern int trig_nfc_wakeup(void);
extern int trig_nfc_sleep(void);

char g_trig_nfc = 0;

static int ese_open(struct inode *inode, struct file *file)
{
    int ret = 0;

	if(g_trig_nfc)
		ret = trig_nfc_wakeup();
	
	ESE_LOG("end.ret=%d", ret);

	return ret;
}

static int ese_close(struct inode *inode, struct file *file)
{
    int ret = 0;
	
	if(g_trig_nfc)
	    ret = trig_nfc_sleep();
	
	ESE_LOG("end.ret=%d", ret);

	return ret;
}

static ssize_t ese_read(struct file *file, char __user *buf,
		size_t count, loff_t *offset)
{
	int retval = 0;
	char *kbuf = NULL;
	//unsigned char txbuf[ADDRESS_LEN];
	struct spi_message msg;
	//unsigned short addr = 0x01;	//default address

	//ESE_LOG("enter.");
	kbuf = kzalloc(count, GFP_KERNEL);
	if (!kbuf) {
		return  -ENOMEM;
	}

	//txbuf[0] = (addr >> 8) | SPI_READ;
	//txbuf[1] = addr & MASK_8BIT;

	mutex_lock(&ese_mutex);
	
	memset(xfer, 0, sizeof(struct spi_transfer));
	spi_message_init(&msg);
	//xfer[0].len = ADDRESS_LEN;
	//xfer[0].tx_buf = &txbuf[0];
	//spi_message_add_tail(&xfer[0], &msg);
	xfer[0].len = count;
	xfer[0].rx_buf = &kbuf[0];
	xfer[0].speed_hz = ese_spi->max_speed_hz;
	spi_message_add_tail(&xfer[0], &msg);
	
	//ESE_LOG("Send read cmd with hz=%d mode=%d!", ese_spi->max_speed_hz, ese_spi->mode);
	retval = spi_sync(ese_spi, &msg);
	if (retval == 0) {
		retval = count;
		ESE_LOG("Read data byte=%d from ese success!", count);
	} else {
		ESE_LOG("Read data from ese failed!ret=%d", retval);
	}
	mutex_unlock(&ese_mutex);
	
	if (copy_to_user(buf, kbuf, count))
		retval = -EFAULT;
	
	/*ESE_LOG("Read data=%d trace:", count);
	while(retval){
		printk(KERN_ERR "%02x ", kbuf[retval]);
		retval--;
	}*/

	kfree(kbuf);

	return retval;
}
#ifdef ESE_COM_TEST
 static void ese_spi_com_teset(void)
 {
	int n =0;
	int wr_len=4;
	int rd_len=5;
	int retval = 0;
	char cmd[] = {0x12,0xc5,0x00,0xd7};
	char kbuf[32] = {0};
	struct spi_message msg;

	/*here send the cmd*/
	memset(xfer, 0, sizeof(struct spi_transfer));
	spi_message_init(&msg);
	xfer[0].len = wr_len;
	xfer[0].tx_buf = cmd;
	xfer[0].speed_hz = ese_spi->max_speed_hz;
	spi_message_add_tail(&xfer[0], &msg);
	xfer[1].len = rd_len;
	xfer[1].rx_buf = &kbuf[0];
	xfer[1].speed_hz = ese_spi->max_speed_hz;
	spi_message_add_tail(&xfer[1], &msg);

	ESE_LOG("Send test cmd with hz=%d mode=%d!", ese_spi->max_speed_hz, ese_spi->mode);
	retval = spi_sync(ese_spi, (struct spi_message*)&msg);
	if (retval == 0) {
		ESE_LOG("Send data byte=%d to ese success!", wr_len);
	} else {
		ESE_LOG("Send data to ese Fail!ret=%d", retval);
	}

	ESE_LOG("Send data_len=%d trace:", wr_len);
	for(n=0; n<wr_len; n++){
		printk(KERN_ERR "%02x ", cmd[n]);
	}

	ESE_LOG("Read data len=%d trace:", rd_len);
	for(n=0; n<rd_len; n++){
		printk(KERN_ERR "%02x ", kbuf[n]);
	}
 }
		
#endif
static ssize_t ese_write(struct file *file, const char __user *buf,
		size_t count, loff_t *offset)
{
	int retval = 0;
	char *data = NULL;
	struct spi_message msg;

	//ESE_LOG("enter.");

	data = kzalloc(count, GFP_KERNEL);
	if (!data) {
		return -ENOMEM;
	}

	if (copy_from_user(data, buf, count)) {
		kfree(data);
		return -EFAULT;
	}

#ifdef ESE_COM_TEST
    if(!strncmp(data, "0123456789", 10)){
		ESE_LOG("Start spi communication test:");
		ese_spi_com_teset();
		kfree(data);
		return count;
	}
	if(!strncmp(data, "reset", 5)){
		ESE_LOG("Start ese reset test:");
		trig_cold_reset();
		kfree(data);
		return count;
	}

#endif

	mutex_lock(&ese_mutex);
	
	memset(xfer, 0, sizeof(struct spi_transfer));
	
	spi_message_init(&msg);
	xfer[0].len = count;
	xfer[0].tx_buf = data;
	xfer[0].speed_hz = ese_spi->max_speed_hz;
	spi_message_add_tail(&xfer[0], &msg);

	//ESE_LOG("Send write cmd with hz=%d mode=%d!", ese_spi->max_speed_hz, ese_spi->mode);
	retval = spi_sync(ese_spi, (struct spi_message*)&msg);
	if (retval == 0) {
		retval = count;
		ESE_LOG("Write data byte=%d to ese success!", count);
	} else {
		ESE_LOG("Write data to ese Fail!ret=%d", retval);
	}

	mutex_unlock(&ese_mutex);

	/*ESE_LOG("Send data=%d trace:", count);
	while(retval){
		printk(KERN_ERR "%02x ", data[retval]);
		retval--;
	}*/
	
	kfree(data);

	return retval;
}
static long ese_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	unsigned int new = (unsigned int)arg;
	
	ESE_LOG("cmd=%d arg=%d.", cmd, new);
	
	return trig_cold_reset();
}
static const struct file_operations ese_fops = {
	.owner		= THIS_MODULE,
	.read		= ese_read,
	.write		= ese_write,
	.open		= ese_open,
	.release	= ese_close,
	.unlocked_ioctl = ese_ioctl,
};
static struct miscdevice ese_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "sec-ese",
	.fops  = &ese_fops,
};

static int nubia_ese_spi_probe(struct spi_device *spi)
{
	int retval=0;

	ESE_LOG("enter.");
	if (spi->master->flags & SPI_MASTER_HALF_DUPLEX) {
		ESE_LOG("Full duplex not supported by host\n");
		return -EIO;
	}

	/*spi->cs_gpio = of_get_named_gpio(spi->dev.of_node, "spi-cs-gpio", 0);
	if(spi->cs_gpio < 0){
		ESE_LOG("Find spi CS gpio failed!");
		return -EIO;
	}

	retval = gpio_request(spi->cs_gpio, "spi-cs_gpio");
	if (retval < 0) {
		ESE_LOG("Spi CS gpio request failed!");
		return retval;
	}*/

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	spi->mode &= ~SPI_CPHA;
	spi->mode &= ~SPI_CPOL;
	spi->mode &= ~SPI_CS_HIGH;
	spi->mode &= ~SPI_LSB_FIRST;

	retval = spi_setup(spi);
	if (retval < 0) {
		ESE_LOG("Failed to perform SPI setup\n");
		return retval;
	}

	retval = misc_register(&ese_misc);
	if (retval < 0){
        return -ENODEV;
	}else{
		ESE_LOG("ese device register success!\n");
	}

	xfer = kcalloc(2, sizeof(struct spi_transfer), GFP_KERNEL);
	if (!xfer) {
			ESE_LOG("Failed to alloc mem for xfer\n");
			return -ENOMEM;
	}
	
	mutex_init(&ese_mutex);
	
	ese_spi = spi;
	
	trig_cold_reset();
	
	ESE_LOG("exit success!.");

	return 0;
}

static int nubia_ese_spi_remove(struct spi_device *spi)
{
	misc_deregister(&ese_misc);
	if(xfer){
		kfree(xfer);
		xfer = NULL;
	}

	return 0;
}

static const struct spi_device_id nubia_ese_id_table[] = {
	{SPI_DRIVER_NAME, 0},
	{},
};
MODULE_DEVICE_TABLE(spi, nubia_ese_id_table);

static struct of_device_id nubia_ese_of_match_table[] = {
	{
		.compatible = "nubia,ese-spi",
	},
	{},
};
MODULE_DEVICE_TABLE(of, nubia_ese_of_match_table);

static struct spi_driver nubia_ese_spi_driver = {
	.driver = {
		.name = SPI_DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = nubia_ese_of_match_table,
	},
	.probe = nubia_ese_spi_probe,
	.remove = nubia_ese_spi_remove,
	.id_table = nubia_ese_id_table,
};


static int __init nubia_ese_init(void)
{
	ESE_LOG("enter.");
	return spi_register_driver(&nubia_ese_spi_driver);
}

static void __exit  nubia_ese_exit(void)
{
	ESE_LOG("enter.");
	return spi_unregister_driver(&nubia_ese_spi_driver);
}
late_initcall(nubia_ese_init);
module_exit(nubia_ese_exit);

MODULE_AUTHOR("Nubia, Inc.");
MODULE_DESCRIPTION("Nubia ese SPI Bus Support Module");
MODULE_LICENSE("GPL v2");
