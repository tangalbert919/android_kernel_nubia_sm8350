/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/soc/zte/usb_switch_dp.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include "bypass_acl.h"


#define SPLIT_CHAR      ','
#define SPLIT_STRING    ","
#define INVALID_VALUE   (-1)
#define INCREASE_STEP   (5)
#define ALL_MATCH       (99999999)

static int  enable = 0;
static int* bypass_acl_uid_list = NULL;
static int  bypass_acl_uid_list_count = 0;
static int* nb_app_list = NULL;
static int  nb_app_list_count = 0;
static int* debug_app_list = NULL;
static int  debug_app_list_count = 0;
static struct mutex mMutex;

int is_in_nb_app_list(int current_uid, int inode_uid, int debug){
    int i = 0;
    int is_bypass_uid = 0;
    int is_nb_app = 0;

#if 0   // temp test
    if((1023 == current_uid) || (10138 == current_uid) || (2000 == current_uid) || (10103 == current_uid)){
        debug = true;
    }
#endif

    mutex_lock(&mMutex);
    for(i = 0; i < debug_app_list_count; ++i){
        if(current_uid == debug_app_list[i]){
            debug = 1;
            break;
        }
    }
    
    if(!enable){
        mutex_unlock(&mMutex);
        return 0;
    }

    for(i = 0; i < bypass_acl_uid_list_count; ++i){
        if(inode_uid == bypass_acl_uid_list[i]){
            is_bypass_uid = 1;
            break;
        }
        // +for disable fuse
        else if(ALL_MATCH == bypass_acl_uid_list[i]){
            is_bypass_uid = 1;
            break;
        }
        // -for disable fuse
    }
    
    for(i = 0; i < nb_app_list_count; ++i){
        if(current_uid == nb_app_list[i]){
            is_nb_app = 1;
            break;
        }
        // +for disable fuse
        else if(ALL_MATCH == nb_app_list[i]){
            is_nb_app = 1;
            break;
        }
        // -for disable fuse
    }
    if(debug){
        printk(KERN_ERR "%s(),current_uid=%d,inode_uid=%d,is_bypass_uid=%d,is_nb_app=%d\n"
            , __func__, current_uid, inode_uid, is_bypass_uid, is_nb_app);
    }
    mutex_unlock(&mMutex);
    return (is_bypass_uid && is_nb_app);
}

static int convert_string_to_array(const char *buf, int** pArrary, int* pArrayCount)
{
    int i = 0;
	int len = strlen(buf);
	int count = 0;
	
	if ((len > 0) && buf && pArrary && pArrayCount){
        printk(KERN_ERR "%s(),%d:buf=%s\n", __func__, __LINE__, buf);
        
        if(strspn(buf, SPLIT_STRING"0123456789\n") != len){
            printk(KERN_ERR "%s(),%d:format error, invalid char\n", __func__, __LINE__);
        }else{
            char *pBuf = NULL;
            char *pBuf2 = NULL;
            // clean list
            if(*pArrary){
                kfree(*pArrary);
                *pArrary = NULL;
                *pArrayCount = 0;
            }
            // set list = null
            if((1 == len) && ('\n' == buf[0])){
                goto exit;
            }
            // dup string
            pBuf = pBuf2 = (char*)kmalloc(len, GFP_KERNEL);
            if(!pBuf){
                printk(KERN_ERR "%s(),%d:kmalloc pBuf fail\n", __func__, __LINE__);
                goto exit;
            }
            memcpy(pBuf, buf, len);
            if('\n' == pBuf[len - 1]){
                pBuf[len - 1] = 0;
            }
            // calc and malloc memory of nb_app_list
            for(i = 0; pBuf[i]; ++i){
                if(SPLIT_CHAR == pBuf[i]){
                    ++count;
                }
            }
            count += 1;
            *pArrary = (int*)kmalloc(sizeof((*pArrary)[0]) * count, GFP_KERNEL);
            if(!(*pArrary)){
                printk(KERN_ERR "%s(),%d:kmalloc nb_app_list fail\n", __func__, __LINE__);
                kfree(pBuf);
                goto exit;
            }
            memset(*pArrary, 0xFF, sizeof((*pArrary)[0]) * count);
            {   // set nb_app_list
                char *p = NULL;
                i = 0;
                while(NULL != (p = strsep(&pBuf, SPLIT_STRING))){
                    printk(KERN_ERR "%s(),%d:p=%s,strlen=%d\n", __func__, __LINE__, p, strlen(p));
                    if(0 == strlen(p)){
                        continue;
                    }
                    (*pArrary)[i] = simple_strtoul(p, NULL, 0);
                    printk(KERN_ERR "%s(),%d:(*pArrary)[%d]=%d\n", __func__, __LINE__, i, (*pArrary)[i]);
                    if((*pArrary)[i] >= 0){
                        ++i;
                    }
                }
            }

            *pArrayCount = count;
            // free memory
            if(pBuf2){
                kfree(pBuf2);
            }
        }
	}else{
        printk(KERN_ERR "%s(),%d:param invalid,len=%d,buf=%p,pArray=%p,pArrayCount=%p\n"
            , __func__, __LINE__, len, buf, pArrary, pArrayCount);
	}
exit:
    printk(KERN_ERR "%s(),%d:pArrayCount=%d\n", __func__, __LINE__, *pArrayCount);
	return count;
}

// need caller to kfree the return value
static char* convert_array_to_string(int *array, int count)
{
    int i = 0;
    char s[80];
    int len = 0;
    char* string = NULL;
    
    printk("%s(),%d:count=%d\n", __func__, __LINE__, count);
    if(count > 0){
        for(i = 0; i < count; ++i){
            sprintf(s, "%d,", array[i]);
            len += strlen(s);
        }
        string = (char*) kmalloc(len + 1, GFP_KERNEL);
        memset(string, 0, len+1);
        if(string){
            for(i = 0; i < count; ++i){
                sprintf(s, "%d,", array[i]);
                strcat(string, s);
            }
        }
    }
	return string;
}


static ssize_t enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    int ret = 0;
    mutex_lock(&mMutex);
	ret = sprintf(buf, "%d\n", enable);
	mutex_unlock(&mMutex);
	return ret;
}
static ssize_t enable_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned long val = 0;
	int ret = kstrtoul(buf, 0, &val);
	if (ret){
		printk("%s is not in hex or decimal form.\n", buf);
	}else{
        mutex_lock(&mMutex);
		enable = !!val;
		mutex_unlock(&mMutex);
	}
	return count;
}
static struct kobj_attribute enable_attr = __ATTR(enable, 0664, enable_show, enable_store);

static ssize_t bypass_acl_uid_list_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    int len = 0;
    char* string = NULL;
    mutex_lock(&mMutex);
    string = convert_array_to_string(bypass_acl_uid_list, bypass_acl_uid_list_count);
    mutex_unlock(&mMutex);
    len = sprintf(buf, "%s\n", string);
    if(string){
        kfree(string);
    }
	return len;
}
static ssize_t bypass_acl_uid_list_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    mutex_lock(&mMutex);
    convert_string_to_array(buf, &bypass_acl_uid_list, &bypass_acl_uid_list_count);
    mutex_unlock(&mMutex);
	return count;
}
static struct kobj_attribute bypass_acl_uid_list_attr = __ATTR(bypass_acl_uid_list, 0664, bypass_acl_uid_list_show, bypass_acl_uid_list_store);

static ssize_t nb_app_list_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    int len = 0;
    char* string = NULL;
    mutex_lock(&mMutex);
    string = convert_array_to_string(nb_app_list, nb_app_list_count);
    mutex_unlock(&mMutex);
    len = sprintf(buf, "%s\n", string);
    if(string){
        kfree(string);
    }
	return len;
}


static ssize_t nb_app_list_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    mutex_lock(&mMutex);
    convert_string_to_array(buf, &nb_app_list, &nb_app_list_count);
    mutex_unlock(&mMutex);
	return count;
}
static struct kobj_attribute nb_app_list_attr = __ATTR(app_list, 0664, nb_app_list_show, nb_app_list_store);

static ssize_t nb_app_list_del_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned long val = 0;
	int ret = kstrtoul(buf, 0, &val);
	if (ret){
		printk("%s is not in hex or decimal form.\n", buf);
	}else{
		int i = 0;
		mutex_lock(&mMutex);
		for(i = 0; i < nb_app_list_count; ++i){
		    if(val == nb_app_list[i]){
		        nb_app_list[i] = INVALID_VALUE;
		        break;
		    }
		}
		mutex_unlock(&mMutex);
	}
	return count;
}
static struct kobj_attribute nb_app_list_del_attr = __ATTR(app_list_del, 0200, NULL, nb_app_list_del_store);

static ssize_t nb_app_list_add_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned long val = 0;
	int ret = kstrtoul(buf, 0, &val);
	if (ret){
		printk("%s is not in hex or decimal form.\n", buf);
	}else{
		int i = 0;
		int new_index = INVALID_VALUE;
		// find in exist list
		mutex_lock(&mMutex);
		for(i = 0; i < nb_app_list_count; ++i){
		    if(val == nb_app_list[i]){
		        break;
		    }
		    if((INVALID_VALUE == new_index) && (INVALID_VALUE == nb_app_list[i])){
		        new_index = i;
		    }
		}
		if(i == nb_app_list_count){
		    // need to add
		    if(INVALID_VALUE == new_index){
		        // list is empty or full, need to realloc
		        int new_nb_app_list_count = nb_app_list_count + INCREASE_STEP;
		        int* p = (int*)kmalloc(sizeof(int) * new_nb_app_list_count, GFP_KERNEL);
		        memset(p, 0xFF, sizeof(int) * new_nb_app_list_count);
		        if(nb_app_list_count){
    		        memcpy(p, nb_app_list, sizeof(int) * nb_app_list_count);
    		        if(nb_app_list){
    		            kfree(nb_app_list);
    		        }
		        }
		        nb_app_list = p;
		        nb_app_list[nb_app_list_count] = val;
		        nb_app_list_count = new_nb_app_list_count;
		    }else{
		        nb_app_list[new_index] = val;
		    }
		}
		mutex_unlock(&mMutex);
	}
	return count;
}
static struct kobj_attribute nb_app_list_add_attr = __ATTR(app_list_add, 0200, NULL, nb_app_list_add_store);

static ssize_t debug_nb_app_list_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    int len = 0;
    char* string = NULL;
    mutex_lock(&mMutex);
    string = convert_array_to_string(debug_app_list, debug_app_list_count);
    mutex_unlock(&mMutex);
    len = sprintf(buf, "%s\n", string);
    if(string){
        kfree(string);
    }
	return len;
}

static ssize_t debug_nb_app_list_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    mutex_lock(&mMutex);
    convert_string_to_array(buf, &debug_app_list, &debug_app_list_count);
    mutex_unlock(&mMutex);
	return count;
}
static struct kobj_attribute debug_nb_app_list_attr = __ATTR(debug_nb_app_list, 0664, debug_nb_app_list_show, debug_nb_app_list_store);


static struct attribute *bypass_acl_attributes[] = {
    &bypass_acl_uid_list_attr.attr,
    &enable_attr.attr,
    &nb_app_list_attr.attr,
    &nb_app_list_del_attr.attr,
    &nb_app_list_add_attr.attr,
    &debug_nb_app_list_attr.attr,
    NULL
};

static struct attribute_group bypass_acl_attribute_group = {
        .attrs = bypass_acl_attributes
};

static struct kobject *bypass_acl_kobj;
static int __init nubia_bypass_acl_init(void)
{
    int rc = 0;
    mutex_init(&mMutex);
	bypass_acl_kobj = kobject_create_and_add("nubia_bypass_acl", NULL);
	if (!bypass_acl_kobj)
	{
		printk(KERN_ERR "%s: bypass_acl kobj create error\n", __func__);
		return -ENOMEM;
	}

	rc = sysfs_create_group(bypass_acl_kobj, &bypass_acl_attribute_group);
	if(rc)
		printk(KERN_ERR "%s: failed to create bypass_acl group attributes\n", __func__);

	return rc;
}

static void __exit nubia_bypass_acl_exit(void)
{
	sysfs_remove_group(bypass_acl_kobj, &bypass_acl_attribute_group);
	kobject_put(bypass_acl_kobj);
}

module_init(nubia_bypass_acl_init);
module_exit(nubia_bypass_acl_exit);

MODULE_DESCRIPTION("bypass_acl driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bypass_acl" );

