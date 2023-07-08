// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Sysfs interface for the universal power supply monitor class
 *
 *  Copyright © 2007  David Woodhouse <dwmw2@infradead.org>
 *  Copyright © 2007  Anton Vorontsov <cbou@mail.ru>
 *  Copyright © 2004  Szabolcs Gyurko
 *  Copyright © 2003  Ian Molton <spyro@f2s.com>
 *
 *  Modified: 2004, Oct     Szabolcs Gyurko
 */

#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/delay.h>
#include "power_supply.h"

#ifdef CONFIG_NBLOG

#include "nblog/nblog.h"
struct _power_supply_nblog_info{
	int battery_soc;
	int battery_voltage;
	int battery_tempature;
	int battery_health;
	int charger_state;
	int charger_current;
	int full_charge;
	int battery_charge_count;
	int charger_type;
};

struct _power_supply_nblog_info power_supply_nblog_info;
char *power_noblog_buf;

#endif

#define MAX_PROP_NAME_LEN 30

struct power_supply_attr {
	const char *prop_name;
	char attr_name[MAX_PROP_NAME_LEN + 1];
	struct device_attribute dev_attr;
	const char * const *text_values;
	int text_values_len;
};

#ifdef CONFIG_FEATURE_NUBIA_BATTERY_1S_2S
extern ssize_t nubia_set_charge_bypass(const char *buf);
extern int inline qti_battery_get_battery_type(void);
extern ssize_t nubia_set_charger(int value);

u16 charge_bypass_flag = 0;
int set_en_charger = 2;
int set_disenable_charger= 3;
int fix_bypass_count = 0;
int charge_current = 0;
int charger_online = 0;
int current_count = 0;
int battery_soc = 0;
int battery_temp = 0;
int battery_voltage = 0;
#endif

#define _POWER_SUPPLY_ATTR(_name, _text, _len)	\
[POWER_SUPPLY_PROP_ ## _name] =			\
{						\
	.prop_name = #_name,			\
	.attr_name = #_name "\0",		\
	.text_values = _text,			\
	.text_values_len = _len,		\
}

#define POWER_SUPPLY_ATTR(_name) _POWER_SUPPLY_ATTR(_name, NULL, 0)
#define _POWER_SUPPLY_ENUM_ATTR(_name, _text)	\
	_POWER_SUPPLY_ATTR(_name, _text, ARRAY_SIZE(_text))
#define POWER_SUPPLY_ENUM_ATTR(_name)	\
	_POWER_SUPPLY_ENUM_ATTR(_name, POWER_SUPPLY_ ## _name ## _TEXT)

static const char * const POWER_SUPPLY_TYPE_TEXT[] = {
	[POWER_SUPPLY_TYPE_UNKNOWN]		= "Unknown",
	[POWER_SUPPLY_TYPE_BATTERY]		= "Battery",
	[POWER_SUPPLY_TYPE_UPS]			= "UPS",
	[POWER_SUPPLY_TYPE_MAINS]		= "Mains",
	[POWER_SUPPLY_TYPE_USB]			= "USB",
	[POWER_SUPPLY_TYPE_USB_DCP]		= "USB_DCP",
	[POWER_SUPPLY_TYPE_USB_CDP]		= "USB_CDP",
	[POWER_SUPPLY_TYPE_USB_ACA]		= "USB_ACA",
	[POWER_SUPPLY_TYPE_USB_TYPE_C]		= "USB_C",
	[POWER_SUPPLY_TYPE_USB_PD]		= "USB_PD",
	[POWER_SUPPLY_TYPE_USB_PD_DRP]		= "USB_PD_DRP",
	[POWER_SUPPLY_TYPE_APPLE_BRICK_ID]	= "BrickID",
	[POWER_SUPPLY_TYPE_WIRELESS]		= "Wireless",
};

static const char * const POWER_SUPPLY_USB_TYPE_TEXT[] = {
	[POWER_SUPPLY_USB_TYPE_UNKNOWN]		= "Unknown",
	[POWER_SUPPLY_USB_TYPE_SDP]		= "SDP",
	[POWER_SUPPLY_USB_TYPE_DCP]		= "DCP",
	[POWER_SUPPLY_USB_TYPE_CDP]		= "CDP",
	[POWER_SUPPLY_USB_TYPE_ACA]		= "ACA",
	[POWER_SUPPLY_USB_TYPE_C]		= "C",
	[POWER_SUPPLY_USB_TYPE_PD]		= "PD",
	[POWER_SUPPLY_USB_TYPE_PD_DRP]		= "PD_DRP",
	[POWER_SUPPLY_USB_TYPE_PD_PPS]		= "PD_PPS",
	[POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID]	= "BrickID",
};

static const char * const POWER_SUPPLY_STATUS_TEXT[] = {
	[POWER_SUPPLY_STATUS_UNKNOWN]		= "Unknown",
	[POWER_SUPPLY_STATUS_CHARGING]		= "Charging",
	[POWER_SUPPLY_STATUS_DISCHARGING]	= "Discharging",
	[POWER_SUPPLY_STATUS_NOT_CHARGING]	= "Not charging",
	[POWER_SUPPLY_STATUS_FULL]		= "Full",
};

static const char * const POWER_SUPPLY_CHARGE_TYPE_TEXT[] = {
	[POWER_SUPPLY_CHARGE_TYPE_UNKNOWN]	= "Unknown",
	[POWER_SUPPLY_CHARGE_TYPE_NONE]		= "N/A",
	[POWER_SUPPLY_CHARGE_TYPE_TRICKLE]	= "Trickle",
	[POWER_SUPPLY_CHARGE_TYPE_FAST]		= "Fast",
	[POWER_SUPPLY_CHARGE_TYPE_STANDARD]	= "Standard",
	[POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE]	= "Adaptive",
	[POWER_SUPPLY_CHARGE_TYPE_CUSTOM]	= "Custom",
};

static const char * const POWER_SUPPLY_HEALTH_TEXT[] = {
	[POWER_SUPPLY_HEALTH_UNKNOWN]		    = "Unknown",
	[POWER_SUPPLY_HEALTH_GOOD]		    = "Good",
	[POWER_SUPPLY_HEALTH_OVERHEAT]		    = "Overheat",
	[POWER_SUPPLY_HEALTH_DEAD]		    = "Dead",
	[POWER_SUPPLY_HEALTH_OVERVOLTAGE]	    = "Over voltage",
	[POWER_SUPPLY_HEALTH_UNSPEC_FAILURE]	    = "Unspecified failure",
	[POWER_SUPPLY_HEALTH_COLD]		    = "Cold",
	[POWER_SUPPLY_HEALTH_WATCHDOG_TIMER_EXPIRE] = "Watchdog timer expire",
	[POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE]   = "Safety timer expire",
	[POWER_SUPPLY_HEALTH_OVERCURRENT]	    = "Over current",
	[POWER_SUPPLY_HEALTH_CALIBRATION_REQUIRED]  = "Calibration required",
	[POWER_SUPPLY_HEALTH_WARM]		    = "Warm",
	[POWER_SUPPLY_HEALTH_COOL]		    = "Cool",
	[POWER_SUPPLY_HEALTH_HOT]		    = "Hot",
};

static const char * const POWER_SUPPLY_TECHNOLOGY_TEXT[] = {
	[POWER_SUPPLY_TECHNOLOGY_UNKNOWN]	= "Unknown",
	[POWER_SUPPLY_TECHNOLOGY_NiMH]		= "NiMH",
	[POWER_SUPPLY_TECHNOLOGY_LION]		= "Li-ion",
	[POWER_SUPPLY_TECHNOLOGY_LIPO]		= "Li-poly",
	[POWER_SUPPLY_TECHNOLOGY_LiFe]		= "LiFe",
	[POWER_SUPPLY_TECHNOLOGY_NiCd]		= "NiCd",
	[POWER_SUPPLY_TECHNOLOGY_LiMn]		= "LiMn",
};

static const char * const POWER_SUPPLY_CAPACITY_LEVEL_TEXT[] = {
	[POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN]	= "Unknown",
	[POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL]	= "Critical",
	[POWER_SUPPLY_CAPACITY_LEVEL_LOW]	= "Low",
	[POWER_SUPPLY_CAPACITY_LEVEL_NORMAL]	= "Normal",
	[POWER_SUPPLY_CAPACITY_LEVEL_HIGH]	= "High",
	[POWER_SUPPLY_CAPACITY_LEVEL_FULL]	= "Full",
};

static const char * const POWER_SUPPLY_SCOPE_TEXT[] = {
	[POWER_SUPPLY_SCOPE_UNKNOWN]	= "Unknown",
	[POWER_SUPPLY_SCOPE_SYSTEM]	= "System",
	[POWER_SUPPLY_SCOPE_DEVICE]	= "Device",
};

static struct power_supply_attr power_supply_attrs[] = {
	/* Properties of type `int' */
	POWER_SUPPLY_ENUM_ATTR(STATUS),
	POWER_SUPPLY_ENUM_ATTR(CHARGE_TYPE),
	POWER_SUPPLY_ENUM_ATTR(HEALTH),
	POWER_SUPPLY_ATTR(PRESENT),
	POWER_SUPPLY_ATTR(ONLINE),
	POWER_SUPPLY_ATTR(AUTHENTIC),
	POWER_SUPPLY_ENUM_ATTR(TECHNOLOGY),
	POWER_SUPPLY_ATTR(CYCLE_COUNT),
	POWER_SUPPLY_ATTR(VOLTAGE_MAX),
	POWER_SUPPLY_ATTR(VOLTAGE_MIN),
	POWER_SUPPLY_ATTR(VOLTAGE_MAX_DESIGN),
	POWER_SUPPLY_ATTR(VOLTAGE_MIN_DESIGN),
	POWER_SUPPLY_ATTR(VOLTAGE_NOW),
	POWER_SUPPLY_ATTR(VOLTAGE_AVG),
	POWER_SUPPLY_ATTR(VOLTAGE_OCV),
	POWER_SUPPLY_ATTR(VOLTAGE_BOOT),
	POWER_SUPPLY_ATTR(CURRENT_MAX),
	POWER_SUPPLY_ATTR(CURRENT_NOW),
	POWER_SUPPLY_ATTR(CURRENT_AVG),
	POWER_SUPPLY_ATTR(CURRENT_BOOT),
	POWER_SUPPLY_ATTR(POWER_NOW),
	POWER_SUPPLY_ATTR(POWER_AVG),
#ifdef CONFIG_FEATURE_NUBIA_BATTERY_1S_2S
	POWER_SUPPLY_ATTR(LCD_ON),
#endif
	POWER_SUPPLY_ATTR(CHARGE_FULL_DESIGN),
	POWER_SUPPLY_ATTR(CHARGE_EMPTY_DESIGN),
	POWER_SUPPLY_ATTR(CHARGE_FULL),
	POWER_SUPPLY_ATTR(CHARGE_EMPTY),
	POWER_SUPPLY_ATTR(CHARGE_NOW),
	POWER_SUPPLY_ATTR(CHARGE_AVG),
	POWER_SUPPLY_ATTR(CHARGE_COUNTER),
	POWER_SUPPLY_ATTR(CONSTANT_CHARGE_CURRENT),
	POWER_SUPPLY_ATTR(CONSTANT_CHARGE_CURRENT_MAX),
	POWER_SUPPLY_ATTR(CONSTANT_CHARGE_VOLTAGE),
	POWER_SUPPLY_ATTR(CONSTANT_CHARGE_VOLTAGE_MAX),
	POWER_SUPPLY_ATTR(CHARGE_CONTROL_LIMIT),
	POWER_SUPPLY_ATTR(CHARGE_CONTROL_LIMIT_MAX),
	POWER_SUPPLY_ATTR(CHARGE_CONTROL_START_THRESHOLD),
	POWER_SUPPLY_ATTR(CHARGE_CONTROL_END_THRESHOLD),
	POWER_SUPPLY_ATTR(INPUT_CURRENT_LIMIT),
	POWER_SUPPLY_ATTR(INPUT_VOLTAGE_LIMIT),
	POWER_SUPPLY_ATTR(INPUT_POWER_LIMIT),
	POWER_SUPPLY_ATTR(ENERGY_FULL_DESIGN),
	POWER_SUPPLY_ATTR(ENERGY_EMPTY_DESIGN),
	POWER_SUPPLY_ATTR(ENERGY_FULL),
	POWER_SUPPLY_ATTR(ENERGY_EMPTY),
	POWER_SUPPLY_ATTR(ENERGY_NOW),
	POWER_SUPPLY_ATTR(ENERGY_AVG),
	POWER_SUPPLY_ATTR(CAPACITY),
	POWER_SUPPLY_ATTR(CAPACITY_ALERT_MIN),
	POWER_SUPPLY_ATTR(CAPACITY_ALERT_MAX),
	POWER_SUPPLY_ATTR(CAPACITY_ERROR_MARGIN),
	POWER_SUPPLY_ENUM_ATTR(CAPACITY_LEVEL),
	POWER_SUPPLY_ATTR(TEMP),
	POWER_SUPPLY_ATTR(TEMP_MAX),
	POWER_SUPPLY_ATTR(TEMP_MIN),
	POWER_SUPPLY_ATTR(TEMP_ALERT_MIN),
	POWER_SUPPLY_ATTR(TEMP_ALERT_MAX),
	POWER_SUPPLY_ATTR(TEMP_AMBIENT),
	POWER_SUPPLY_ATTR(TEMP_AMBIENT_ALERT_MIN),
	POWER_SUPPLY_ATTR(TEMP_AMBIENT_ALERT_MAX),
	POWER_SUPPLY_ATTR(TIME_TO_EMPTY_NOW),
	POWER_SUPPLY_ATTR(TIME_TO_EMPTY_AVG),
	POWER_SUPPLY_ATTR(TIME_TO_FULL_NOW),
	POWER_SUPPLY_ATTR(TIME_TO_FULL_AVG),
	POWER_SUPPLY_ENUM_ATTR(TYPE),
	POWER_SUPPLY_ATTR(USB_TYPE),
	POWER_SUPPLY_ENUM_ATTR(SCOPE),
	POWER_SUPPLY_ATTR(PRECHARGE_CURRENT),
	POWER_SUPPLY_ATTR(CHARGE_TERM_CURRENT),
	POWER_SUPPLY_ATTR(CALIBRATE),
	POWER_SUPPLY_ATTR(MANUFACTURE_YEAR),
	POWER_SUPPLY_ATTR(MANUFACTURE_MONTH),
	POWER_SUPPLY_ATTR(MANUFACTURE_DAY),
	/* Properties of type `const char *' */
	POWER_SUPPLY_ATTR(MODEL_NAME),
	POWER_SUPPLY_ATTR(MANUFACTURER),
	POWER_SUPPLY_ATTR(SERIAL_NUMBER),
	POWER_SUPPLY_ATTR(QUICK_CHARGE_TYPE),
	POWER_SUPPLY_ATTR(TX_ADAPTER),
	POWER_SUPPLY_ATTR(SIGNAL_STRENGTH),
	POWER_SUPPLY_ATTR(REVERSE_CHG_MODE),
};

static struct attribute *
__power_supply_attrs[ARRAY_SIZE(power_supply_attrs) + 1];

static struct power_supply_attr *to_ps_attr(struct device_attribute *attr)
{
	return container_of(attr, struct power_supply_attr, dev_attr);
}

static enum power_supply_property dev_attr_psp(struct device_attribute *attr)
{
	return  to_ps_attr(attr) - power_supply_attrs;
}

static ssize_t power_supply_show_usb_type(struct device *dev,
					  const struct power_supply_desc *desc,
					  union power_supply_propval *value,
					  char *buf)
{
	enum power_supply_usb_type usb_type;
	ssize_t count = 0;
	bool match = false;
	int i;

	for (i = 0; i < desc->num_usb_types; ++i) {
		usb_type = desc->usb_types[i];

		if (value->intval == usb_type) {
			count += sprintf(buf + count, "[%s] ",
					 POWER_SUPPLY_USB_TYPE_TEXT[usb_type]);
			match = true;
		} else {
			count += sprintf(buf + count, "%s ",
					 POWER_SUPPLY_USB_TYPE_TEXT[usb_type]);
		}
	}

	if (!match) {
		dev_warn(dev, "driver reporting unsupported connected type\n");
		return -EINVAL;
	}

	if (count)
		buf[count - 1] = '\n';

	return count;
}

static ssize_t power_supply_show_property(struct device *dev,
					  struct device_attribute *attr,
					  char *buf) {
	ssize_t ret;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct power_supply_attr *ps_attr = to_ps_attr(attr);
	enum power_supply_property psp = dev_attr_psp(attr);
	union power_supply_propval value;

	if (psp == POWER_SUPPLY_PROP_TYPE) {
		value.intval = psy->desc->type;
	} else {
		ret = power_supply_get_property(psy, psp, &value);

		if (ret < 0) {
			if (ret == -ENODATA)
				dev_dbg(dev, "driver has no data for `%s' property\n",
					attr->attr.name);
			else if (ret != -ENODEV && ret != -EAGAIN)
				dev_err_ratelimited(dev,
					"driver failed to report `%s' property: %zd\n",
					attr->attr.name, ret);
			return ret;
		}
	}

	if (ps_attr->text_values_len > 0 &&
	    value.intval < ps_attr->text_values_len && value.intval >= 0) {
#ifdef CONFIG_NBLOG
		if(POWER_SUPPLY_PROP_HEALTH == psp)
			power_supply_nblog_info.battery_health = value.intval;
		if(POWER_SUPPLY_PROP_STATUS == psp)
			power_supply_nblog_info.charger_state = value.intval;
#endif
		return sprintf(buf, "%s\n", ps_attr->text_values[value.intval]);
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_USB_TYPE:
		ret = power_supply_show_usb_type(dev, psy->desc,
						&value, buf);
		printk("%s: usb type[%s]-->function name = %s,  \n", __func__, buf, ps_attr->attr_name);
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME ... POWER_SUPPLY_PROP_SERIAL_NUMBER:
		ret = sprintf(buf, "%s\n", value.strval);
		printk("%s: function[%s]-->%s\n", __func__, ps_attr->attr_name, value.strval);
		break;
	default:
		#ifdef CONFIG_FEATURE_NUBIA_BATTERY_1S_2S
		if((psp == POWER_SUPPLY_PROP_CURRENT_NOW) && (qti_battery_get_battery_type()==0)){
			value.intval *= -1;
		}
		/*used for judge in charge or not*/
		if(psp == POWER_SUPPLY_PROP_CURRENT_NOW)
			charge_current = value.intval;

		if(psp == POWER_SUPPLY_PROP_ONLINE)
			charger_online = value.intval;

		if(psp == POWER_SUPPLY_PROP_CAPACITY)
			battery_soc = value.intval;
		
		if(psp == POWER_SUPPLY_PROP_TEMP)
			battery_temp = (int)(value.intval/10);
		
		if(psp == POWER_SUPPLY_PROP_VOLTAGE_NOW)
			battery_voltage = (int)(value.intval/1000);
		/*if not in bypass charge, and then to check usb online or not, if online and the current is > 0mA
		  so, we think it's stop charge issue, beside the soc is 100%, it is ok when battery temp > 55 stop charge
		  no need nubia_set_charger, (battery_temp > 47)&&(battery_voltage > 8100) is also
		*/
		if((!charge_bypass_flag) && (charger_online==1) && (charge_current > 0) 
			&& (POWER_SUPPLY_PROP_CAPACITY ==psp) && (battery_soc < 99)
			&&(battery_temp < 55)&&(!((battery_temp > 47)&&(battery_voltage > 8100)))) {	
			current_count++;// to check ten time
			if(current_count > 10){
				current_count = 0;
				printk("%s: charge_bypass_flag = %d\n", __func__, charge_bypass_flag);
				nubia_set_charger(set_en_charger);
				msleep(1000);
				nubia_set_charger(set_disenable_charger);
				/**
				**  fix no current or current just about 200mA when plugin pd
				**/
				fix_bypass_count++;
				if(fix_bypass_count >= 5){
					nubia_set_charger(1);
					msleep(2000);
					nubia_set_charger(0);
					fix_bypass_count = 0;
					printk("%s: fix no current or current just about 200mA when plugin pd \n", __func__);
				}
			}
			printk("%s: charge_bypass_flag = %d, charge_current=%d, charger_online=%d, psp=%d, current_count=%d, battery_temp = %d, battery_voltage = %d\n", __func__, charge_bypass_flag, charge_current, charger_online, psp, current_count, battery_temp, battery_voltage);
		}
		#endif
		ret = sprintf(buf, "%d\n", value.intval);
		printk("%s: function[%s]-->%d\n", __func__, ps_attr->attr_name, value.intval);
	}

	/**
	** if battery soc is different with last report soc,
	** we will get power supply info and to report to nblog system
	**/
#ifdef CONFIG_NBLOG
	switch(psp){
		case POWER_SUPPLY_PROP_CURRENT_NOW:
			power_supply_nblog_info.charger_current = value.intval;
			break;
		case POWER_SUPPLY_PROP_USB_TYPE:
			power_supply_nblog_info.charger_type = value.intval;
			break;
		case POWER_SUPPLY_PROP_CHARGE_FULL:
			power_supply_nblog_info.full_charge = value.intval;
			break;
		case POWER_SUPPLY_PROP_STATUS:
			power_supply_nblog_info.charger_state = value.intval;
			break;
		case POWER_SUPPLY_PROP_HEALTH:
			power_supply_nblog_info.battery_health = value.intval;
			break;
		case POWER_SUPPLY_PROP_TEMP:
			power_supply_nblog_info.battery_tempature = (value.intval + 5) / 10;
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			power_supply_nblog_info.battery_voltage = value.intval;
			break;
		case POWER_SUPPLY_PROP_CYCLE_COUNT:
			power_supply_nblog_info.battery_charge_count = value.intval;
			break;
		case POWER_SUPPLY_PROP_CAPACITY:
			if(power_supply_nblog_info.battery_soc != value.intval){
				/*reduce report log, just soc arrived 100% and new soc is 99%, then to report log to big data for nubia*/
				if((power_supply_nblog_info.battery_soc == 100) && (value.intval == 99)){
					sprintf(power_noblog_buf, "%d, voltage=%d, temp=%d, health=%s, state=%s, current=%d, full_charge=%d, charge_count=%d, charge_type=%s",
							power_supply_nblog_info.battery_soc, power_supply_nblog_info.battery_voltage,
							power_supply_nblog_info.battery_tempature, POWER_SUPPLY_HEALTH_TEXT[power_supply_nblog_info.battery_health], POWER_SUPPLY_STATUS_TEXT[power_supply_nblog_info.charger_state],
							power_supply_nblog_info.charger_current, power_supply_nblog_info.full_charge, power_supply_nblog_info.battery_charge_count, POWER_SUPPLY_USB_TYPE_TEXT[power_supply_nblog_info.charger_type]);
					log_system_report_log(NUBIA_MODULE_CHARGE, "soc", power_noblog_buf, 0);

					printk("soc=%d, voltage=%d, temp=%d, health=%s, state=%s, current=%d, full_charge=%d, charge_count=%d, charge_type=%s",
							power_supply_nblog_info.battery_soc, power_supply_nblog_info.battery_voltage,
							power_supply_nblog_info.battery_tempature, POWER_SUPPLY_HEALTH_TEXT[power_supply_nblog_info.battery_health], POWER_SUPPLY_STATUS_TEXT[power_supply_nblog_info.charger_state],
							power_supply_nblog_info.charger_current, power_supply_nblog_info.full_charge, power_supply_nblog_info.battery_charge_count, POWER_SUPPLY_USB_TYPE_TEXT[power_supply_nblog_info.charger_type]);
				}
				power_supply_nblog_info.battery_soc = value.intval;
			}
			break;
		default:
			break;
	}

#endif
	return ret;
}

static ssize_t power_supply_store_property(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count) {
	ssize_t ret;
	struct power_supply *psy = dev_get_drvdata(dev);
	struct power_supply_attr *ps_attr = to_ps_attr(attr);
	enum power_supply_property psp = dev_attr_psp(attr);
	union power_supply_propval value;

	ret = -EINVAL;
	if (ps_attr->text_values_len > 0) {
		ret = __sysfs_match_string(ps_attr->text_values,
					   ps_attr->text_values_len, buf);
	}

	/*
	 * If no match was found, then check to see if it is an integer.
	 * Integer values are valid for enums in addition to the text value.
	 */
	if (ret < 0) {
		long long_val;

		ret = kstrtol(buf, 10, &long_val);
		if (ret < 0)
			return ret;

		ret = long_val;
	}

	value.intval = ret;

	ret = power_supply_set_property(psy, psp, &value);
	if (ret < 0)
		return ret;

	return count;
}

static umode_t power_supply_attr_is_visible(struct kobject *kobj,
					   struct attribute *attr,
					   int attrno)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct power_supply *psy = dev_get_drvdata(dev);
	umode_t mode = S_IRUSR | S_IRGRP | S_IROTH;
	int i;

	if (!power_supply_attrs[attrno].prop_name)
		return 0;

	if (attrno == POWER_SUPPLY_PROP_TYPE)
		return mode;

	for (i = 0; i < psy->desc->num_properties; i++) {
		int property = psy->desc->properties[i];

		if (property == attrno) {
			if (psy->desc->property_is_writeable &&
			    psy->desc->property_is_writeable(psy, property) > 0)
				mode |= S_IWUSR;

			return mode;
		}
	}

	return 0;
}

static struct attribute_group power_supply_attr_group = {
	.attrs = __power_supply_attrs,
	.is_visible = power_supply_attr_is_visible,
};

static const struct attribute_group *power_supply_attr_groups[] = {
	&power_supply_attr_group,
	NULL,
};

static void str_to_lower(char *str)
{
	while (*str) {
		*str = tolower(*str);
		str++;
	}
}

void power_supply_init_attrs(struct device_type *dev_type)
{
	int i;

	dev_type->groups = power_supply_attr_groups;

	for (i = 0; i < ARRAY_SIZE(power_supply_attrs); i++) {
		struct device_attribute *attr;

		if (!power_supply_attrs[i].prop_name) {
			pr_warn("%s: Property %d skipped because is is missing from power_supply_attrs\n",
				__func__, i);
			sprintf(power_supply_attrs[i].attr_name, "_err_%d", i);
		} else {
			str_to_lower(power_supply_attrs[i].attr_name);
		}

		attr = &power_supply_attrs[i].dev_attr;

		attr->attr.name = power_supply_attrs[i].attr_name;
		attr->show = power_supply_show_property;
		attr->store = power_supply_store_property;
		__power_supply_attrs[i] = &attr->attr;
	}
	#ifdef CONFIG_NBLOG
	power_noblog_buf = (char *)get_zeroed_page(GFP_KERNEL);
	if (!power_noblog_buf){
		pr_err("%s: get_zeroed_page for power_noblog_buf error \n", __func__);
	}
	#endif
}

static int add_prop_uevent(struct device *dev, struct kobj_uevent_env *env,
			   enum power_supply_property prop, char *prop_buf)
{
	int ret = 0;
	struct power_supply_attr *pwr_attr;
	struct device_attribute *dev_attr;
	char *line;

	pwr_attr = &power_supply_attrs[prop];
	dev_attr = &pwr_attr->dev_attr;

	ret = power_supply_show_property(dev, dev_attr, prop_buf);
	if (ret == -ENODEV || ret == -ENODATA) {
		/*
		 * When a battery is absent, we expect -ENODEV. Don't abort;
		 * send the uevent with at least the the PRESENT=0 property
		 */
		return 0;
	}

	if (ret < 0)
		return ret;

	line = strchr(prop_buf, '\n');
	if (line)
		*line = 0;

	return add_uevent_var(env, "POWER_SUPPLY_%s=%s",
			      pwr_attr->prop_name, prop_buf);
}

int power_supply_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	int ret = 0, j;
	char *prop_buf;

	if (!psy || !psy->desc) {
		dev_dbg(dev, "No power supply yet\n");
		return ret;
	}

	ret = add_uevent_var(env, "POWER_SUPPLY_NAME=%s", psy->desc->name);
	if (ret)
		return ret;

	prop_buf = (char *)get_zeroed_page(GFP_KERNEL);
	if (!prop_buf)
		return -ENOMEM;

	ret = add_prop_uevent(dev, env, POWER_SUPPLY_PROP_TYPE, prop_buf);
	if (ret)
		goto out;

	for (j = 0; j < psy->desc->num_properties; j++) {
		ret = add_prop_uevent(dev, env, psy->desc->properties[j],
				      prop_buf);
		if (ret)
			goto out;
	}

out:
	free_page((unsigned long)prop_buf);

	return ret;
}
