#ifndef __EC_COLOR__
#define __EC_COLOR__

//#define NUBIA_EC_COLOR


#ifdef NUBIA_EC_COLOR
struct ec_color_pinctrl {
    struct pinctrl *pinctrl;
    struct pinctrl_state *pin_active;
    struct pinctrl_state *pin_suspend;
};
#endif

struct ec_color {
    struct device dev;
    struct thermal_zone_device *tzd;
    struct hrtimer ec_color_charge_timer;    //if timeout, set the gpio pull down
    struct hrtimer wait_next_charge_timer;    /*wait next charge timer*/
	struct hrtimer update_wait_charge_timer;
	struct workqueue_struct *ec_color_workqueue;
	struct work_struct ec_color_set_wait_time;
    struct work_struct ec_color_charge_work;
	struct work_struct update_wait_charge_work;
    int enable_gpio;
	int last_enable_gpio_status;
    int sel_gpio;
	int last_sel_gpio_status;
	int wait_temp;
	//unsigned int disable_but_hold_last_status;
    unsigned int ec_color_charge_time;
	unsigned int wait_next_charge_time;
	unsigned int update_wait_charge_time;
#ifdef NUBIA_EC_COLOR
	const char		*pinctrl_name;
	struct pinctrl		*pinctrl;
    //struct ec_color_pinctrl pinctrl_info;
#endif
};
#endif