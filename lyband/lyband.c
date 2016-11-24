#include <linux/wakelock.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/wakelock.h>
#include <linux/irq.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/kobject.h>
#include <linux/atomic.h>
#include <linux/ioctl.h>

#include <mt-plat/upmu_common.h>

#include "lybandlib.h"

#define LYBAND_TAG                "<LYBAND> "
#define LYBAND_ERR(fmt, args...)  pr_err(LYBAND_TAG fmt, ##args)
#define LYBAND_LOG(fmt, args...)  pr_err(LYBAND_TAG fmt, ##args)
#define LYBAND_VER(fmt, args...)  pr_err(LYBAND_TAG fmt, ##args)

struct lyband_data_ctrl
{
	int (*enable)(int en);
	int (*get_data)(int *x, int *y, int *z, int *status);
	int (*set_delay)(u64 delay);
};

struct lyband_band_lib
{
	void*    bandhandle;
	atomic_t step;
	atomic_t mot_enable;
	atomic_t step_enable;
	unsigned long pwrkeytime;
};

struct lyband_context
{
	struct mutex             mutex;
	struct miscdevice        mdev;

	struct work_struct       process;
	struct workqueue_struct* workq;

	struct lyband_data_ctrl  data_ctrl;
	struct lyband_band_lib   band_lib;

	atomic_t                 polling;
	atomic_t                 delay;
	struct hrtimer           hrtimer;
	ktime_t                  ktime;
	struct wake_lock         lyband_suspend_lock;
};

static struct lyband_context _lyband_context;

static ssize_t lyband_show_enable(struct device *dev, struct device_attribute *attr, char *buf)
{
	int len = 0;
	LYBAND_LOG("lyband_show_enable not support now\n");
	return len;
}

static ssize_t lyband_store_enable(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct lyband_context *cxt = (struct lyband_context*)dev->driver_data;
	LYBAND_LOG("lyband_store_enable nodata buf=%s\n", buf);
	mutex_lock(&cxt->mutex);
	switch (buf[0])
	{
		case '1':
			atomic_set(&cxt->band_lib.mot_enable, 0);
			atomic_set(&cxt->band_lib.step_enable, 1);
			atomic_set(&cxt->polling, 1);
			wake_lock(&cxt->lyband_suspend_lock);
			if (cxt->data_ctrl.enable)
				cxt->data_ctrl.enable(1);
			cxt->ktime = ktime_add_ns(ktime_get(), (int64_t)atomic_read(&cxt->delay)*1000000);
			hrtimer_start(&cxt->hrtimer, cxt->ktime, HRTIMER_MODE_ABS);
			cxt->band_lib.pwrkeytime = get_seconds();
			break;
		case '2':
			atomic_set(&cxt->band_lib.mot_enable, 1);
			atomic_set(&cxt->band_lib.step_enable, 0);
			atomic_set(&cxt->polling, 1);
			wake_lock(&cxt->lyband_suspend_lock);
			if (cxt->data_ctrl.enable)
				cxt->data_ctrl.enable(1);
			cxt->ktime = ktime_add_ns(ktime_get(), (int64_t)atomic_read(&cxt->delay)*1000000);
			hrtimer_start(&cxt->hrtimer, cxt->ktime, HRTIMER_MODE_ABS);
			cxt->band_lib.pwrkeytime = get_seconds();
			break;
		case '3':
			atomic_set(&cxt->band_lib.mot_enable, 1);
			atomic_set(&cxt->band_lib.step_enable, 1);
			atomic_set(&cxt->polling, 1);
			wake_lock(&cxt->lyband_suspend_lock);
			if (cxt->data_ctrl.enable)
				cxt->data_ctrl.enable(1);
			cxt->ktime = ktime_add_ns(ktime_get(), (int64_t)atomic_read(&cxt->delay)*1000000);
			hrtimer_start(&cxt->hrtimer, cxt->ktime, HRTIMER_MODE_ABS);
			cxt->band_lib.pwrkeytime = get_seconds();
			break;
		case '0':
			atomic_set(&cxt->band_lib.mot_enable, 0);
			atomic_set(&cxt->band_lib.step_enable, 0);
			atomic_set(&cxt->polling, 0);
			wake_unlock(&cxt->lyband_suspend_lock);
			if (cxt->data_ctrl.enable)
				cxt->data_ctrl.enable(0);
			smp_mb();
			hrtimer_cancel(&cxt->hrtimer);
			smp_mb();
			cancel_work_sync(&cxt->process);
			break;
		default:
			LYBAND_ERR("lyband_store_enable cmd error !!\n");
			break;
	}
	mutex_unlock(&cxt->mutex);
	return count;
}

static ssize_t lyband_show_step(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lyband_context *cxt = (struct lyband_context*)dev->driver_data;
	return snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&cxt->band_lib.step));
}

static ssize_t lyband_store_step(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	int64_t step;
	struct lyband_context *cxt = (struct lyband_context*)dev->driver_data;

	LYBAND_LOG("lyband_store_active buf=%s\n", buf);
	mutex_lock(&cxt->mutex);

	ret = kstrtoll(buf, 10, &step);
	if (ret != 0)
		LYBAND_ERR("invalid format!!\n");
	else
	{
		atomic_set(&cxt->band_lib.step, (int)step);
		LYBAND_LOG("lyband_store_step %lld ms\n", step);
	}

	mutex_unlock(&cxt->mutex);
	LYBAND_LOG(" lyband_store_active done\n");
	return count;
}

static ssize_t lyband_store_delay(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	int64_t delay = 0;
	int64_t mdelay = 0;
	struct lyband_context *cxt = (struct lyband_context*)dev->driver_data;

	mutex_lock(&cxt->mutex);

	ret = kstrtoll(buf, 10, &delay);
	if (ret != 0)
		LYBAND_ERR("invalid format!!\n");
	else
	{
		mdelay = delay;
		atomic_set(&cxt->delay, (int)mdelay);
		LYBAND_LOG("lyband_store_delay %lld ms\n", delay);
	}
	mutex_unlock(&cxt->mutex);
	return count;
}

static ssize_t lyband_show_delay(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lyband_context *cxt = (struct lyband_context*)dev->driver_data;
	LYBAND_LOG("lyband_show_delay delay:%d ms\n", atomic_read(&cxt->delay));
	return snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&cxt->delay));
}

static ssize_t lyband_show_devnum(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = 0;
	const char *devname = NULL;
	unsigned int devnum;
	struct lyband_context *cxt = (struct lyband_context*)dev->driver_data;

	devname = dev_name(cxt->mdev.this_device);
	ret = sscanf(devname+5, "%d", &devnum);
	return snprintf(buf, PAGE_SIZE, "%d\n", devnum);
}

DEVICE_ATTR(lyband_enable, S_IWUSR|S_IRUGO, lyband_show_enable, lyband_store_enable);
DEVICE_ATTR(lyband_delay,  S_IWUSR|S_IRUGO, lyband_show_delay,  lyband_store_delay);
DEVICE_ATTR(lyband_step,   S_IWUSR|S_IRUGO, lyband_show_step,   lyband_store_step);
DEVICE_ATTR(lyband_devnum, S_IWUSR|S_IRUGO, lyband_show_devnum, NULL);

static struct attribute *lyband_attributes[] = {
	&dev_attr_lyband_enable.attr,
	&dev_attr_lyband_step.attr,
	&dev_attr_lyband_delay.attr,
	&dev_attr_lyband_devnum.attr,
	NULL
};

static struct attribute_group lyband_attribute_group = {
	.attrs = lyband_attributes
};

extern void kpd_pwrkey_pmic_handler(unsigned long pressed);

static void lyband_work_func(struct work_struct *work)
{
	int level;
	int ax,ay,az,status;
	short x,y,z;
	unsigned char step;
	struct lyband_context* cxt = (struct lyband_context *)container_of(work, struct lyband_context, process);

	if (atomic_read(&cxt->polling))
	{
		cxt->ktime = ktime_add_ns(cxt->ktime, (int64_t)atomic_read(&cxt->delay)*1000000);
		hrtimer_start(&cxt->hrtimer, cxt->ktime, HRTIMER_MODE_ABS);
	}

	if (cxt->data_ctrl.get_data)
		cxt->data_ctrl.get_data(&ax, &ay, &az, &status);

	x = ax&0xffff;
	y = ay&0xffff;
	z = az&0xffff;

	level = pmic_get_register_value(PMIC_ISINK_CH0_EN);

	if (atomic_read(&cxt->band_lib.mot_enable))
	{

		if (!level)
		{
			if (BandHandOver(x, y, z))
			{
				unsigned long now = get_seconds();
				if ((now - cxt->band_lib.pwrkeytime) > 3)
				{
					LYBAND_LOG("hand over.\n");
					kpd_pwrkey_pmic_handler(1);
					kpd_pwrkey_pmic_handler(0);
					cxt->band_lib.pwrkeytime = now;
				}
			}
		}
	}
	if (atomic_read(&cxt->band_lib.step_enable))
	{
		BandProcess((short)x, (short)y, (short)z, cxt->band_lib.bandhandle, &step);
		atomic_add(step, &cxt->band_lib.step);
	}
	LYBAND_ERR("x:%d, y:%d, z:%d, step:%d, step:%d, level:%d\n", 
		x, y, z, step, atomic_read(&cxt->band_lib.step), level);
}

enum hrtimer_restart lyband_poll(struct hrtimer *timer)
{
	struct lyband_context* cxt = (struct lyband_context *)container_of(timer, struct lyband_context, hrtimer);
	queue_work(cxt->workq, &cxt->process);
	return HRTIMER_NORESTART;
}

static int lyband_misc_init(struct lyband_context *cxt)
{
	int err = 0;
	cxt->mdev.minor = MISC_DYNAMIC_MINOR;
	cxt->mdev.name  = "m_lyband";
	err = misc_register(&cxt->mdev);
	if (err)
		LYBAND_ERR("unable to register acc misc device!!\n");
	else
		dev_set_drvdata(cxt->mdev.this_device, cxt);
	return err;
}

static int lyband_probe(void)
{
	int err;
	struct lyband_context *cxt = &_lyband_context;

	err = lyband_misc_init(cxt);
	if (err)
	{
		LYBAND_ERR("unable to register misc device\n");
		return -2;
	}
	err = sysfs_create_group(&cxt->mdev.this_device->kobj, &lyband_attribute_group);
	if (err < 0)
	{
		LYBAND_ERR("unable to create attribute file\n");
		return -3;
	}

	kobject_uevent(&cxt->mdev.this_device->kobj, KOBJ_ADD);
	mutex_init(&cxt->mutex);
	INIT_WORK(&cxt->process, lyband_work_func);
	cxt->workq = create_workqueue("lyband_polling");
	if (!cxt->workq)
	{
		LYBAND_ERR("unable to create workqueue\n");
		return -4;
	}

	hrtimer_init(&cxt->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	cxt->hrtimer.function = lyband_poll;
	atomic_set(&cxt->polling, 0);
	atomic_set(&cxt->delay, 40);

	cxt->band_lib.bandhandle= BandInit(0);
	atomic_set(&cxt->band_lib.step, 0);	
	wake_lock_init(&cxt->lyband_suspend_lock, WAKE_LOCK_SUSPEND, "lyband suspend wakelock");
	return 0;
}

void lyband_register_data_ctrl(struct lyband_data_ctrl* data)
{
	if (data)
	{
		_lyband_context.data_ctrl.enable      = data->enable;
		_lyband_context.data_ctrl.get_data    = data->get_data;
		_lyband_context.data_ctrl.set_delay   = data->set_delay;
	}
}
EXPORT_SYMBOL(lyband_register_data_ctrl);

static int lyband_remove(void)
{
	sysfs_remove_group(&_lyband_context.mdev.this_device->kobj, &lyband_attribute_group);
	kobject_uevent(&_lyband_context.mdev.this_device->kobj, KOBJ_REMOVE);
	wake_lock_destroy(&_lyband_context.lyband_suspend_lock);
	mutex_destroy(&_lyband_context.mutex);
	destroy_workqueue(_lyband_context.workq);
	if (misc_deregister(&_lyband_context.mdev))
		LYBAND_ERR("misc_deregister fail\n");
	//memset(&_lyband_context, 0 , sizeof(_lyband_context));
	return 0;
}

static int __init lyband_init(void)
{
	LYBAND_LOG("lyband_init\n");
	memset(&_lyband_context, 0 , sizeof(_lyband_context));
	if (lyband_probe()) {
		LYBAND_ERR("failed to register acc driver\n");
		return -ENODEV;
	}
	return 0;
}

static void __exit lyband_exit(void)
{
	lyband_remove();
}

late_initcall(lyband_init);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LYBAND device driver");
MODULE_AUTHOR("LianYun");

