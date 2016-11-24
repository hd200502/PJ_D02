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
#include <linux/regulator/consumer.h>
#include <mt-plat/mt_gpio.h>
#include <mt-plat/upmu_common.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>

#define LYBAND_TAG                "<LYBAND-NEW> "
#define LYBAND_ERR(fmt, args...)  pr_err(LYBAND_TAG fmt, ##args)
#define LYBAND_LOG(fmt, args...)  pr_err(LYBAND_TAG fmt, ##args)
#define LYBAND_VER(fmt, args...)  pr_err(LYBAND_TAG fmt, ##args)
#define LYBAND_DEVICE             "LYBAND-NEW"

struct lyband_context
{
	struct mutex             mutex;
	struct miscdevice        mdev;
	struct platform_device*  pdev;

	struct work_struct       process;
	struct workqueue_struct* workq;

	atomic_t                 step;
	atomic_t                 mot_evt;

	unsigned int             acc_irq;
	unsigned int             mot_irq;

	struct regulator*        acc_reg;
	struct regulator*        acc_io_reg;

	struct
	{
		struct pinctrl*          pin_ctrl;
		struct pinctrl_state*    pin_cs_high;
		struct pinctrl_state*    pin_cs_low;
		struct pinctrl_state*    pin_aeint_as_int;
		struct pinctrl_state*    pin_aeint_out_high;
		struct pinctrl_state*    pin_aeint_out_low;
		struct pinctrl_state*    pin_meint_as_int;
		struct pinctrl_state*    pin_meint_out_high;
		struct pinctrl_state*    pin_meint_out_low;
		struct pinctrl_state*    pin_sda_high;
		struct pinctrl_state*    pin_sda_low;
		struct pinctrl_state*    pin_scl_high;
		struct pinctrl_state*    pin_scl_low;
	}pins;
};

static struct lyband_context _lyband_context;

static void lyband_dev_enable(struct lyband_context* cxt, u8 step, u8 mot);
static void lyband_read_reg(struct lyband_context* cxt, u8 reg, u8* data, u8 len);
static void lyband_write_reg(struct lyband_context* cxt, u8 reg, u8* data, u8 len);

static ssize_t lyband_show_enable(struct device *dev, struct device_attribute *attr, char *buf)
{
	int reg=0;
	struct lyband_context *cxt = (struct lyband_context*)dev->driver_data;
	u8 set;
	lyband_read_reg(cxt, 0xc3, &set, sizeof(set));
	if (set & 0x80)
		reg = 0;
	else
	{
		if (set & 0x02)
			reg = 1;
		else
			reg = 3;
	}
	LYBAND_LOG("lyband_show_enable not support now\n");
	return snprintf(buf, PAGE_SIZE, "%d\n", reg);
}

static ssize_t lyband_store_enable(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct lyband_context *cxt = (struct lyband_context*)dev->driver_data;
	LYBAND_LOG("lyband_store_enable nodata buf=%s\n", buf);
	mutex_lock(&cxt->mutex);
	switch (buf[0])
	{
		case '1':
			lyband_dev_enable(cxt, 1, 0);
			break;
		case '2':
			//lyband_dev_enable(cxt, 0, 1);
			//break;
		case '3':
			lyband_dev_enable(cxt, 1, 1);
			break;
		case '0':
			lyband_dev_enable(cxt, 0, 0);
			smp_mb();
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
	u8  i, data[3];
	u32 step=0;
	struct lyband_context *cxt = (struct lyband_context*)dev->driver_data;
	for (i=0; i<3; i++)
	{
		memset(data, 0, sizeof(data));
		lyband_read_reg(cxt, 0xc4, data, sizeof(data));
		step = (data[2]<<16)|(data[1]<<8)|data[0];
		if (step!=0 || atomic_read(&cxt->step)==0)
			break;
	}
	atomic_set(&cxt->step, step);
	return snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&cxt->step));
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
		atomic_set(&cxt->step, (int)step);
		LYBAND_LOG("lyband_store_step %lld ms\n", step);
	}

	mutex_unlock(&cxt->mutex);
	LYBAND_LOG(" lyband_store_active done\n");
	return count;
}

static ssize_t lyband_store_delay(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	return 0;
}

static ssize_t lyband_show_delay(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", 0);
}

static ssize_t lyband_show_debug(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 id, set, step[3];
	struct lyband_context *cxt = (struct lyband_context*)dev->driver_data;
	lyband_read_reg(cxt, 1, &id, sizeof(id));
	lyband_read_reg(cxt, 0xc3, &set, sizeof(set));
	lyband_read_reg(cxt, 0xc4, step, sizeof(step));
	return snprintf(buf, PAGE_SIZE, " 01:%02X\n C3:%02X\n C4:%02X\n C5:%02X\n C6:%02X\n", id, set, step[0], step[1], step[2]);
}

DEVICE_ATTR(lyband_enable, S_IWUSR|S_IRUGO, lyband_show_enable, lyband_store_enable);
DEVICE_ATTR(lyband_delay,  S_IWUSR|S_IRUGO, lyband_show_delay,  lyband_store_delay);
DEVICE_ATTR(lyband_step,   S_IWUSR|S_IRUGO, lyband_show_step,   lyband_store_step);
DEVICE_ATTR(lyband_debug,  S_IWUSR|S_IRUGO, lyband_show_debug, NULL);

static struct attribute *lyband_attributes[] = {
	&dev_attr_lyband_enable.attr,
	&dev_attr_lyband_step.attr,
	&dev_attr_lyband_delay.attr,
	&dev_attr_lyband_debug.attr,
	NULL
};

#define PIN_CS  (53|0x80000000)

#define CS_OUT   {mt_set_gpio_dir(PIN_CS, GPIO_DIR_OUT);}
#define CS_HIGH  {mt_set_gpio_out(PIN_CS, GPIO_OUT_ONE);}
#define CS_LOW   {mt_set_gpio_out(PIN_CS, GPIO_OUT_ZERO);}

static struct attribute_group lyband_attribute_group = {
	.attrs = lyband_attributes
};

static void lyband_cs_high(struct lyband_context* cxt)
{
	CS_OUT;
	CS_HIGH;
}

static void lyband_cs_low(struct lyband_context* cxt)
{
	CS_OUT;
	CS_LOW;
}

#define PIN_SDA (55|0x80000000)
#define PIN_SCL (54|0x80000000)

#define SDA_OUT  {mt_set_gpio_dir(PIN_SDA, GPIO_DIR_OUT);}
#define SCL_OUT  {mt_set_gpio_dir(PIN_SCL, GPIO_DIR_OUT);}
#define SDA_IN   {mt_set_gpio_dir(PIN_SDA, GPIO_DIR_IN);}
#define SCL_IN   {mt_set_gpio_dir(PIN_SCL, GPIO_DIR_IN);}
#define SDA_VAL  mt_get_gpio_in(PIN_SDA)

#define SDA_HIGH {mt_set_gpio_out(PIN_SDA, GPIO_OUT_ONE);}
#define SDA_LOW  {mt_set_gpio_out(PIN_SDA, GPIO_OUT_ZERO);}
#define SCL_HIGH {mt_set_gpio_out(PIN_SCL, GPIO_OUT_ONE);}
#define SCL_LOW  {mt_set_gpio_out(PIN_SCL, GPIO_OUT_ZERO);}

#define DELAY 2

static void i2c_start(void)  
{
	SDA_OUT;
	SCL_OUT;
	SDA_HIGH;
	udelay(DELAY);
	SCL_HIGH;
	udelay(DELAY);

	SDA_LOW;
	udelay(DELAY);

	SCL_LOW;
	udelay(DELAY);
}

static void i2c_stop(void)
{
	SCL_LOW;
	udelay(DELAY);
	SDA_LOW;
	udelay(DELAY);

	SCL_HIGH;
	udelay(DELAY);
	SDA_HIGH;
	udelay(DELAY);
	SDA_IN;
	SCL_IN;
}

static void i2c_send_ack(char ack)
{
	if(ack)
	{
		SDA_HIGH;
	}
	else
	{
		SDA_LOW;
	}
	udelay(DELAY);

	SCL_HIGH;
	udelay(DELAY);

	SCL_LOW;
	udelay(DELAY);
}

static char i2c_receive_ack(void)
{
	char rc = 0;

	SDA_IN;
	SCL_HIGH;
	udelay(DELAY);

	if(SDA_VAL)
		rc = 1;

	//udelay(DELAY);

	SCL_LOW;
	//udelay(DELAY);
	SDA_OUT;
	return rc;
}

static char i2c_send_byte(char send_byte)
{
	char rc = 0;
	char out_mask = 0x80;
	char value;
	while(out_mask)
	{
		value = ((send_byte & out_mask) ? 1 : 0);     
		if (value == 1)
		{
			SDA_HIGH;
		}
		else                             
		{
			SDA_LOW;
		}
		//udelay(DELAY);
		SCL_HIGH;
		udelay(DELAY);

		SCL_LOW;
		udelay(DELAY);

		out_mask >>= 1;
	}

	SDA_HIGH;
	udelay(DELAY);
	rc = i2c_receive_ack();
	return rc;
}

static void i2c_read_byte(char *buffer, char ack)
{
	char count = 0x08;
	char data = 0x00; 
	char temp = 0;

	SDA_IN;
	while(count > 0) {
		SCL_HIGH;
		udelay(DELAY);
		temp = SDA_VAL;
		data <<= 1;
		if (temp)
		    data |= 0x01;
		SCL_LOW;
		udelay(DELAY);
		count--;
	}
	SDA_OUT;
	i2c_send_ack(ack);//0 = ACK    1 = NACK
	*buffer = data;
}

static char i2c_read(char device_id, char reg_address, char *buffer, char len)
{
    char rc = 0;
    char i;

    unsigned long flags = 0;
    local_irq_save(flags);
    i2c_start();
    rc |= i2c_send_byte( (device_id << 1) | 0x00 );
    if(rc) {
        LYBAND_ERR("ERROR!   i2c_read failed 1.\n");
	goto i2c_read_end;
    }
    rc |= i2c_send_byte(reg_address);
    if(rc) {
        LYBAND_ERR("ERROR!   i2c_read failed 2.\n");
	goto i2c_read_end;
    }
    i2c_start();//restart I2C
    rc |= i2c_send_byte( (device_id << 1) | 0x01 );

    if(rc) {
        LYBAND_ERR("ERROR!   i2c_read failed 3.\n");
	goto i2c_read_end;
    }
    for(i=0;i<len;i++) {
        i2c_read_byte(buffer++, !(len-i-1));//  !(len-i-1)  这个用来保证在读到每个字节后发送一个ACK并能在最后一个字节读完后发送一个NACK
    }
    if(rc) {
        LYBAND_ERR("ERROR!   i2c_read failed 4.\n");
    }
i2c_read_end:
    i2c_stop();
    local_irq_restore(flags);
    return rc;
}

static char i2c_write(char device_id, char reg_address, char* data, char len)
{
    char rc = 0;
    char i;
    unsigned long flags = 0;
    local_irq_save(flags);
    i2c_start();
    rc |= i2c_send_byte( (device_id << 1) | 0x00 );
    if(rc) {
        LYBAND_ERR("ERROR!  i2c_write failed 1.\n");
	goto i2c_write_end;
    }
    rc |= i2c_send_byte(reg_address);
    if(rc) {
        LYBAND_ERR("ERROR!  i2c_write failed 2.\n");
	goto i2c_write_end;
    }
    if(data==NULL ||0==len) {
	goto i2c_write_end;
    }
    for(i=0; i<len; i++) {
        rc |= i2c_send_byte(*data);
        data++;
    } 
    if(rc) {
        LYBAND_ERR("ERROR!  i2c_write failed 3.\n");
    }
i2c_write_end:
    i2c_stop();
    local_irq_restore(flags);
    return rc;
}

static void lyband_dev_deinit(struct lyband_context* cxt)
{
	regulator_disable(cxt->acc_reg);
	regulator_disable(cxt->acc_io_reg);
	regulator_put(cxt->acc_reg);
	regulator_put(cxt->acc_io_reg);
}

static void lyband_read_reg(struct lyband_context* cxt, u8 reg, u8* data, u8 len)
{
	LYBAND_LOG("lyband_read_reg :%x.\n", reg);
	lyband_cs_low(cxt);
	mdelay(10);
	i2c_read(0x27, reg, data, len);
	lyband_cs_high(cxt);
	mdelay(10);
}

static void lyband_write_reg(struct lyband_context* cxt, u8 reg, u8* data, u8 len)
{
	LYBAND_LOG("lyband_write_reg :%x.\n", reg);
	lyband_cs_low(cxt);
	mdelay(10);
	i2c_write(0x27, reg, data, len);
	lyband_cs_high(cxt);
	mdelay(10);
}

static void lyband_dev_enable(struct lyband_context* cxt, u8 step, u8 mot)
{
	u8 set=0, newset=0;
	
	//u8 buf[6];
	//memset(buf, 0, sizeof(buf));
	//lyband_read_reg(cxt, 0x02, buf, sizeof(buf));
	//LYBAND_LOG("lyband_dev_enable [0x02]:%02x %02x %02x %02x %02x %02x\n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);

	lyband_read_reg(cxt, 0x01, &set, sizeof(set));
	LYBAND_LOG("lyband_dev_init CHIP_ID:%x.\n", set);
	lyband_read_reg(cxt, 0xC3, &set, sizeof(set));
	LYBAND_LOG("lyband_dev_enable USER_SET:%x.\n", set);

	newset = set;
	if (!mot && !step)
		newset = newset|0x80;
	else
		newset = newset&~0x80;

	newset = newset&~0x40; //normal

	if (mot)
		newset = newset|0x20;
	else
		newset = newset&~0x20;

	if (step)
		newset = newset|0x10;
	else
		newset = newset&~0x10;

	newset = newset|0x08; //enable noise
	newset = newset&~0x04; //don't clear step
	newset = newset&~0x03; //band alg

	if (step && !mot)
		newset = newset|0x2; //step alg

	if (set != newset)
	{
		lyband_write_reg(cxt, 0xC3, &newset, sizeof(newset));
		LYBAND_LOG("lyband_dev_enable NEW_USER_SET:%x.\n", newset);

		//read set, if error, write again
		lyband_read_reg(cxt, 0xC3, &set, sizeof(set));
		LYBAND_LOG("lyband_dev_enable 2 USER_SET:%x.\n", set);
		if (set != newset)
		{
			lyband_write_reg(cxt, 0xC3, &newset, sizeof(newset));
			LYBAND_LOG("lyband_dev_enable 2 USER_SET:%x.\n", set);
		}
	}
}

static void lyband_dev_init(struct lyband_context* cxt)
{
	u8 data;
	int ret;
	mt_set_gpio_mode(PIN_SDA, GPIO_MODE_GPIO);//SDA
	mt_set_gpio_mode(PIN_SCL, GPIO_MODE_GPIO);//SCL
	mt_set_gpio_mode(PIN_CS,  GPIO_MODE_GPIO);//CS
	lyband_cs_high(cxt);
	i2c_stop();

	cxt->acc_reg    = regulator_get(&cxt->pdev->dev, "vmc");
	cxt->acc_io_reg = regulator_get(&cxt->pdev->dev, "vmch");

	if (!cxt->acc_reg)
		LYBAND_ERR("lyband_dev_init regulator_get(vmc) failed!\n");
	if (!cxt->acc_io_reg)
		LYBAND_ERR("lyband_dev_init regulator_get(vmch) failed!\n");

	ret=regulator_set_voltage(cxt->acc_reg, 3300000, 3300000);
	if (ret)
		LYBAND_ERR("lyband_dev_init regulator_set_voltage(vmc) 3.3v failed!\n");
	ret=regulator_enable(cxt->acc_reg);
	if (ret)
		LYBAND_ERR("lyband_dev_init regulator_enable(vmc) failed!\n");

	ret=regulator_set_voltage(cxt->acc_io_reg, 3300000, 3300000);
	if (ret)
		LYBAND_ERR("lyband_dev_init regulator_set_voltage(vmch) 3.3v failed!\n");
	ret=regulator_enable(cxt->acc_io_reg);
	if (ret)
		LYBAND_ERR("lyband_dev_init regulator_enable(vmch) failed!\n");

	msleep(100);

	lyband_read_reg(cxt, 0x01, &data, sizeof(data));
	LYBAND_LOG("lyband_dev_init CHIP_ID:%x.\n", data);
	lyband_read_reg(cxt, 0xC3, &data, sizeof(data));
	LYBAND_LOG("lyband_dev_init USER_SET:%x.\n", data);
	data = 0xC; //stop all & clear step
	lyband_write_reg(cxt, 0xC3, &data, sizeof(data));
	lyband_dev_enable(cxt, 0, 0);
}

static irqreturn_t lyband_meint_handler(s32 irq, void *p)
{
	struct lyband_context *cxt = (struct lyband_context *)p;
	atomic_set(&cxt->mot_evt, 1);
	queue_work(cxt->workq, &cxt->process);
	return 0;
}
/*
static irqreturn_t lyband_aeint_handler(s32 irq, void *p)
{
	struct lyband_context *cxt = (struct lyband_context *)p;
	queue_work(cxt->workq, &cxt->process);
	return 0;
}
*/
extern void kpd_pwrkey_pmic_handler(unsigned long pressed);

static void lyband_work_func(struct work_struct *work)
{
	u8 i,data[3];
	u32 step;

	struct lyband_context* cxt = (struct lyband_context *)container_of(work, struct lyband_context, process);
	LYBAND_LOG("lyband_work_func. %d\n", atomic_read(&cxt->mot_evt));
	if (atomic_read(&cxt->mot_evt))
	{
		int level;
		atomic_set(&cxt->mot_evt, 0);
		level = pmic_get_register_value(PMIC_ISINK_CH0_EN);
		if (!level)
		{
			kpd_pwrkey_pmic_handler(1);
			kpd_pwrkey_pmic_handler(0);
		}
	}
	for (i=0; i<3; i++)
	{
		memset(data, 0, sizeof(data));
		lyband_read_reg(cxt, 0xc4, data, sizeof(data));
		step = (data[2]<<16)|(data[1]<<8)|data[0];
		if (step!=0 || atomic_read(&cxt->step)==0)
			break;
	}
	atomic_set(&cxt->step, step);
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

static int lyband_get_gpio_info(struct platform_device *pdev)
{
	int ret = -1;
	struct lyband_context *cxt;
	if (!pdev)
		return ret;
	cxt = (struct lyband_context *)dev_get_drvdata(&pdev->dev);

	cxt->pins.pin_ctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(cxt->pins.pin_ctrl))
	{
		ret = PTR_ERR(cxt->pins.pin_ctrl);
		LYBAND_ERR("Cannot find lyband pinctrl!\n");
		return ret;
	}
	cxt->pins.pin_cs_high = pinctrl_lookup_state(cxt->pins.pin_ctrl, "cs_high");
	if (IS_ERR(cxt->pins.pin_cs_high))
	{
		ret = PTR_ERR(cxt->pins.pin_cs_high);
		LYBAND_ERR("Cannot find lyband pinctrl cs_high!\n");
		return ret;
	}
	cxt->pins.pin_cs_low  = pinctrl_lookup_state(cxt->pins.pin_ctrl, "cs_low");
	if (IS_ERR(cxt->pins.pin_cs_low))
	{
		ret = PTR_ERR(cxt->pins.pin_cs_low);
		LYBAND_ERR("Cannot find lyband pinctrl cs_low!\n");
		return ret;
	}
	cxt->pins.pin_aeint_as_int = pinctrl_lookup_state(cxt->pins.pin_ctrl, "acc_eint_as_int");
	if (IS_ERR(cxt->pins.pin_aeint_as_int))
	{
		ret = PTR_ERR(cxt->pins.pin_aeint_as_int);
		LYBAND_ERR("Cannot find lyband pinctrl acc_eint_as_int!\n");
		return ret;
	}
	cxt->pins.pin_aeint_out_high = pinctrl_lookup_state(cxt->pins.pin_ctrl, "acc_eint_output1");
	if (IS_ERR(cxt->pins.pin_aeint_out_high))
	{
		ret = PTR_ERR(cxt->pins.pin_aeint_out_high);
		LYBAND_ERR("Cannot find lyband pinctrl acc_eint_output1!\n");
		return ret;
	}
	cxt->pins.pin_aeint_out_low  = pinctrl_lookup_state(cxt->pins.pin_ctrl, "acc_eint_output0");
	if (IS_ERR(cxt->pins.pin_aeint_out_low))
	{
		ret = PTR_ERR(cxt->pins.pin_aeint_out_low);
		LYBAND_ERR("Cannot find lyband pinctrl acc_eint_output0!\n");
		return ret;
	}

	cxt->pins.pin_meint_as_int = pinctrl_lookup_state(cxt->pins.pin_ctrl, "mot_eint_as_int");
	if (IS_ERR(cxt->pins.pin_meint_as_int))
	{
		ret = PTR_ERR(cxt->pins.pin_meint_as_int);
		LYBAND_ERR("Cannot find lyband pinctrl acc_eint_as_int!\n");
		return ret;
	}
	cxt->pins.pin_meint_out_high = pinctrl_lookup_state(cxt->pins.pin_ctrl, "mot_eint_output1");
	if (IS_ERR(cxt->pins.pin_meint_out_high))
	{
		ret = PTR_ERR(cxt->pins.pin_meint_out_high);
		LYBAND_ERR("Cannot find lyband pinctrl acc_eint_output1!\n");
		return ret;
	}
	cxt->pins.pin_meint_out_low  = pinctrl_lookup_state(cxt->pins.pin_ctrl, "mot_eint_output0");
	if (IS_ERR(cxt->pins.pin_meint_out_low))
	{
		ret = PTR_ERR(cxt->pins.pin_meint_out_low);
		LYBAND_ERR("Cannot find lyband pinctrl mot_eint_output0!\n");
		return ret;
	}

	LYBAND_LOG("lyband pinctrl get.");
	return 0;
}

static struct of_device_id lyband_of_match[] = {
	{ .compatible = "mediatek, lyband", },
	{},
};

static int lyband_irq_registration(struct lyband_context *cxt)
{
	struct device_node *node = NULL;
	int ret = 0;

	node = of_find_matching_node(node, lyband_of_match);
	if (!node)
	{
		LYBAND_ERR("[%s] tpd request_irq can not find touch eint device node!.", __func__);
		return -1;
	}
	else
	{
		cxt->mot_irq = irq_of_parse_and_map(node, 0);
		ret = request_irq(cxt->mot_irq, (irq_handler_t)lyband_meint_handler,
			IRQF_TRIGGER_RISING|IRQF_DISABLED, "LYBAND meint", cxt);
		if (ret>0)
		{
			LYBAND_ERR("LYBAND meint IRQ not available!");
			return -2;
		}
	}
	return 0;
}


static int lyband_probe(struct platform_device *pdev)
{
	int err;
	struct lyband_context *cxt = &_lyband_context;

	cxt->pdev = pdev;
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

	dev_set_drvdata(&pdev->dev, (void*)cxt);
	lyband_get_gpio_info(pdev);
	lyband_dev_init(cxt);

	kobject_uevent(&cxt->mdev.this_device->kobj, KOBJ_ADD);
	mutex_init(&cxt->mutex);
	INIT_WORK(&cxt->process, lyband_work_func);
	cxt->workq = create_workqueue("lyband_polling");
	if (!cxt->workq)
	{
		LYBAND_ERR("unable to create workqueue\n");
		return -4;
	}

	pinctrl_select_state(cxt->pins.pin_ctrl, cxt->pins.pin_meint_as_int);
	if(!lyband_irq_registration(cxt))
		enable_irq(cxt->mot_irq);
/*
	pinctrl_select_state(cxt->pins.pin_ctrl, cxt->pins.pin_aeint_as_int);
	cxt->mot_irq = 7;
	err = request_irq(cxt->mot_irq, (irq_handler_t)lyband_aeint_handler,
		IRQF_TRIGGER_RISING|IRQF_DISABLED, "TLYBAND aeint", cxt);
	if (err>0)
		LYBAND_ERR("LYBAND aeint IRQ not available!");
*/
	atomic_set(&cxt->mot_evt, 0);
	atomic_set(&cxt->step, 0);
	return 0;
}

void lyband_register_data_ctrl(void* data)
{

}
EXPORT_SYMBOL(lyband_register_data_ctrl);

static int lyband_remove(struct platform_device *pdev)
{
	struct lyband_context *cxt = (struct lyband_context*)dev_get_drvdata(&pdev->dev);
	sysfs_remove_group(&cxt->mdev.this_device->kobj, &lyband_attribute_group);
	lyband_dev_deinit(cxt);
	free_irq(cxt->mot_irq, (void*)cxt);
	free_irq(cxt->acc_irq, (void*)cxt);
	return 0;
}


static const struct dev_pm_ops lyband_pm_ops = {
	.suspend = NULL,
	.resume = NULL,
};

static struct platform_driver lyband_driver = {
	.remove = lyband_remove,
	.shutdown = NULL,
	.probe = lyband_probe,
	.driver = {
			.name = LYBAND_DEVICE,
			.pm = &lyband_pm_ops,
			.owner = THIS_MODULE,
			.of_match_table = lyband_of_match,
	},
};

static int __init lyband_init(void)
{
	LYBAND_LOG("lyband_init\n");
	if (platform_driver_register(&lyband_driver) != 0) {
		LYBAND_ERR("failed to register acc driver\n");
		return -ENODEV;
	}
	return 0;
}

static void __exit lyband_exit(void)
{
	lyband_remove(_lyband_context.pdev);
	platform_driver_unregister(&lyband_driver);
}

module_init(lyband_init);
module_exit(lyband_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LYBAND device driver");
MODULE_AUTHOR("LianYun");
