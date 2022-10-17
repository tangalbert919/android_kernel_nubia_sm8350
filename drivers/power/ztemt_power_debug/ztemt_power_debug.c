#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

#define POWER_MONITOR_PERIOD_MS	20000
#define DRV_NAME "nubia_power_debug"

int power_debug_switch=0;
static int pre_power_enable=0;
static int rpmh_count=0;

struct delayed_work power_debug_work;

extern int msm_show_resume_irq_mask; //used to print the resume irq

#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_COMMON_CLK_QCOM_DEBUG)
extern void nubia_clock_print_enabled(int enable);
#endif
extern void global_print_active_locks( void );
extern void nubia_rpmh_master_stats_get(void);
extern void nubia_timerfd_print_enabled(int enable);
extern void nubia_alarm_print_enabled(int enable);

static void power_debug_work_func(struct work_struct *work)
{

	//print wakelocks
	global_print_active_locks();
	//wakelock_stats_show_debug();
	if(pre_power_enable != power_debug_switch){
#if defined(CONFIG_DEBUG_FS) && defined(CONFIG_COMMON_CLK_QCOM_DEBUG)	
	   nubia_clock_print_enabled(power_debug_switch);
#endif
	   nubia_timerfd_print_enabled(power_debug_switch);
	   nubia_alarm_print_enabled(power_debug_switch);
	   pre_power_enable = power_debug_switch;
	}

    if(rpmh_count > 3){
	   nubia_rpmh_master_stats_get();
	   rpmh_count=0;
	}
	rpmh_count++;

	schedule_delayed_work(&power_debug_work,
			  round_jiffies_relative(msecs_to_jiffies
						(POWER_MONITOR_PERIOD_MS)));

}

static int power_debug_work_control(int on)
{
	int ret=0;
	if(1==on)
	{
		if(1==power_debug_switch)
		{
			ret=1;
		}
		else
		{
			power_debug_switch=1;
			msm_show_resume_irq_mask=1;
			INIT_DELAYED_WORK(&power_debug_work,  power_debug_work_func);
			schedule_delayed_work(&power_debug_work,
			  round_jiffies_relative(msecs_to_jiffies
						(POWER_MONITOR_PERIOD_MS)));
			printk("%s:enable power_debug_work.\n",__func__);
		}
	}
	else
	{
		if(0==power_debug_switch)
		{
			ret=1;
		}
		else
		{
			power_debug_switch=0;
			msm_show_resume_irq_mask=0;
			cancel_delayed_work(&power_debug_work);         
			printk("%s:disable power_debug_work.\n",__func__);
		}

	}
	return ret;
}

static ssize_t po_enable_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{

	sprintf(buf, "%u\n", power_debug_switch);
	return 1;
}

static ssize_t po_enable_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t count)
{

	unsigned int val;

	if (sscanf(buf, "%u", &val) == 1) {
		if (power_debug_work_control(val))
			return count;
	}
	return -EINVAL;
}


static struct kobject *po_kobject = NULL;


static struct kobj_attribute nubia_power_attrs[] = {
	__ATTR(enable, 0664, po_enable_show, po_enable_store),
};

static int power_debug_init(void)
{
	int ret;
	int attr_count = 0;
	
	po_kobject = kobject_create_and_add(DRV_NAME, NULL);
	if(po_kobject == NULL) {
		ret = -ENOMEM;
		goto err1;
	}

	for (attr_count = 0; attr_count < ARRAY_SIZE(nubia_power_attrs); attr_count++) {
		ret = sysfs_create_file(po_kobject, &nubia_power_attrs[attr_count].attr);
		if (ret < 0) {
			printk("failed to create sysfs attributes\n");
			goto err;
		}
	}


	if(power_debug_switch) {
	  msm_show_resume_irq_mask=1; //on in default, deleted is allow.
	}
    
	INIT_DELAYED_WORK(&power_debug_work,  power_debug_work_func);

	if(power_debug_switch) {
	  msm_show_resume_irq_mask=1; //on in default, deleted is allow.
	  schedule_delayed_work(&power_debug_work,
			  round_jiffies_relative(msecs_to_jiffies
						(POWER_MONITOR_PERIOD_MS)));
	}    
	return 0;

err:
	kobject_del(po_kobject);
err1:
	printk(DRV_NAME": Failed to create sys file\n");
	return ret;
}

static void __exit power_debug_exit(void)
{
	int attr_count = 0;

	power_debug_work_control(0);

	for (attr_count = 0; attr_count < ARRAY_SIZE(nubia_power_attrs); attr_count++)
		sysfs_remove_file(po_kobject, &nubia_power_attrs[attr_count].attr);

	kobject_put(po_kobject);
  
}

module_init(power_debug_init);

module_exit(power_debug_exit);

MODULE_AUTHOR("ztemt-hjliao");
MODULE_DESCRIPTION("ztemt power debug driver");
MODULE_LICENSE("GPL");

