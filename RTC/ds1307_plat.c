// SPDX-License-Identifier: GPL-2.0
/*
 * Platform driver cho module RTC DS1307 tren Raspberry Pi
 *
 * Mo hinh "platform driver" (Device Tree + probe()/remove()):
 *
 *   Device Tree overlay (ds1307-plat-overlay.dts) khai bao mot node
 *   platform:
 *
 *       ds1307-plat {
 *           compatible  = "edu,ds1307-platform";
 *           i2c-bus     = <&i2c_arm>;   // bus I2C1: GPIO2 = SDA, GPIO3 = SCL
 *           i2c-address = <0x68>;       // dia chi 7-bit cua DS1307
 *       };
 *
 *   Kernel tao struct platform_device tu node nay; khi compatible khop voi
 *   of_match_table ben duoi, ham probe() duoc goi. Trong probe() driver:
 *     1. lay bus I2C qua phandle "i2c-bus"  (chua san sang -> -EPROBE_DEFER)
 *     2. chiem dia chi 0x68 tren bus (i2c-dev/i2cdetect se thay "UU")
 *     3. kiem tra chip co tra loi, canh bao neu bo dao dong dang dung (CH=1)
 *     4. dang ky thiet bi RTC -> /dev/rtcN, /sys/class/rtc/rtcN
 *     5. dang ky 56 byte NVRAM nuoi bang pin -> /sys/bus/nvmem/devices/...
 *     6. them thuoc tinh sysfs: "sqw" (dau ra xung vuong) va "regs" (dump thanh ghi)
 *
 *   remove() duoc goi khi rmmod hoac unbind; moi tai nguyen deu cap phat
 *   bang devm_* nen duoc giai phong tu dong theo thu tu nguoc.
 *
 * Ban do thanh ghi DS1307 (tat ca o dang BCD):
 *   0x00 giay  (bit7 = CH: 1 = bo dao dong dung)
 *   0x01 phut
 *   0x02 gio   (bit6 = 1: che do 12h, bit5 = PM)
 *   0x03 thu   (1..7)
 *   0x04 ngay  (1..31)
 *   0x05 thang (1..12)
 *   0x06 nam   (00..99 -> 2000..2099)
 *   0x07 control: OUT(bit7) SQWE(bit4) RS1:RS0(bit1:0)
 *   0x08..0x3F 56 byte RAM duoc nuoi bang pin
 */

#include <linux/bcd.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/version.h>

#define DRV_NAME		"ds1307-plat"
#define DS1307_DEFAULT_ADDR	0x68

#define DS1307_REG_SECS		0x00
#define DS1307_REG_MIN		0x01
#define DS1307_REG_HOUR		0x02
#define DS1307_REG_WDAY		0x03
#define DS1307_REG_MDAY		0x04
#define DS1307_REG_MONTH	0x05
#define DS1307_REG_YEAR		0x06
#define DS1307_REG_CONTROL	0x07
#define DS1307_NUM_TIME_REGS	7

#define DS1307_BIT_CH		0x80	/* Clock Halt - trong thanh ghi giay */
#define DS1307_BIT_12H		0x40	/* trong thanh ghi gio */
#define DS1307_BIT_PM		0x20	/* trong thanh ghi gio (che do 12h) */

#define DS1307_BIT_OUT		0x80	/* trong thanh ghi control */
#define DS1307_BIT_SQWE		0x10
#define DS1307_RS_MASK		0x03

#define DS1307_NVRAM_BASE	0x08
#define DS1307_NVRAM_SIZE	56

struct ds1307_plat {
	struct device		*dev;		/* &pdev->dev */
	struct i2c_adapter	*adap;		/* bus I2C lay tu Device Tree */
	struct i2c_client	*client;	/* client "dummy" chiem dia chi 0x68 */
	struct rtc_device	*rtc;
	struct mutex		lock;		/* bao ve chuoi doc-sua-ghi */
};

/* ------------------------------------------------------------------ */
/* Truy cap I2C muc thap                                              */
/* ------------------------------------------------------------------ */

/* Doc @len byte bat dau tu thanh ghi @reg (ghi con tro + repeated START + doc) */
static int ds_read_regs(struct ds1307_plat *ds, u8 reg, u8 *buf, size_t len)
{
	struct i2c_msg msgs[2] = {
		{
			.addr  = ds->client->addr,
			.flags = 0,
			.len   = 1,
			.buf   = &reg,
		}, {
			.addr  = ds->client->addr,
			.flags = I2C_M_RD,
			.len   = len,
			.buf   = buf,
		},
	};
	int ret;

	ret = i2c_transfer(ds->client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0)
		return ret;
	return ret == ARRAY_SIZE(msgs) ? 0 : -EIO;
}

/* Ghi @len byte bat dau tu thanh ghi @reg trong mot giao dich I2C */
static int ds_write_regs(struct ds1307_plat *ds, u8 reg, const u8 *buf,
			 size_t len)
{
	u8 tmp[1 + DS1307_NVRAM_SIZE];
	int ret;

	if (len > DS1307_NVRAM_SIZE)
		return -EINVAL;

	tmp[0] = reg;
	memcpy(&tmp[1], buf, len);

	ret = i2c_master_send(ds->client, tmp, len + 1);
	if (ret < 0)
		return ret;
	return ret == len + 1 ? 0 : -EIO;
}

/* ------------------------------------------------------------------ */
/* rtc_class_ops                                                      */
/* ------------------------------------------------------------------ */

static int ds_read_time(struct device *dev, struct rtc_time *tm)
{
	struct ds1307_plat *ds = dev_get_drvdata(dev);
	u8 regs[DS1307_NUM_TIME_REGS];
	int hour, ret;

	mutex_lock(&ds->lock);
	/*
	 * Doc 7 thanh ghi trong 1 lan: DS1307 chot thoi gian luc START nen
	 * khong bi loi "nhay giay" giua cac byte.
	 */
	ret = ds_read_regs(ds, DS1307_REG_SECS, regs, sizeof(regs));
	mutex_unlock(&ds->lock);

	if (ret) {
		dev_err_ratelimited(dev, "read time failed: %d\n", ret);
		return ret;
	}

	/* CH = 1: bo dao dong dung -> thoi gian khong dang tin cay */
	if (regs[DS1307_REG_SECS] & DS1307_BIT_CH) {
		dev_warn_ratelimited(dev,
				     "time not valid (oscillator stopped), set it first\n");
		return -EINVAL;
	}

	tm->tm_sec  = bcd2bin(regs[DS1307_REG_SECS] & 0x7f);
	tm->tm_min  = bcd2bin(regs[DS1307_REG_MIN] & 0x7f);

	if (regs[DS1307_REG_HOUR] & DS1307_BIT_12H) {
		hour = bcd2bin(regs[DS1307_REG_HOUR] & 0x1f);	/* 1..12 */
		if (hour == 12)
			hour = 0;
		if (regs[DS1307_REG_HOUR] & DS1307_BIT_PM)
			hour += 12;
	} else {
		hour = bcd2bin(regs[DS1307_REG_HOUR] & 0x3f);	/* 0..23 */
	}
	tm->tm_hour = hour;

	tm->tm_wday = bcd2bin(regs[DS1307_REG_WDAY] & 0x07) - 1;  /* 1..7 -> 0..6 */
	if (tm->tm_wday < 0 || tm->tm_wday > 6)
		tm->tm_wday = 0;
	tm->tm_mday = bcd2bin(regs[DS1307_REG_MDAY] & 0x3f);
	tm->tm_mon  = bcd2bin(regs[DS1307_REG_MONTH] & 0x1f) - 1; /* 1..12 -> 0..11 */
	tm->tm_year = bcd2bin(regs[DS1307_REG_YEAR]) + 100;       /* 00 -> 2000 */

	dev_dbg(dev, "read %ptR (raw %*ph)\n", tm, (int)sizeof(regs), regs);

	/* rtc core se kiem tra tinh hop le (rtc_valid_tm) sau ham nay */
	return 0;
}

static int ds_set_time(struct device *dev, struct rtc_time *tm)
{
	struct ds1307_plat *ds = dev_get_drvdata(dev);
	u8 regs[DS1307_NUM_TIME_REGS];
	int ret;

	/* rtc core da kiem tra tm nam trong [range_min, range_max] */
	regs[DS1307_REG_SECS]  = bin2bcd(tm->tm_sec);	/* CH = 0 -> chay dao dong */
	regs[DS1307_REG_MIN]   = bin2bcd(tm->tm_min);
	regs[DS1307_REG_HOUR]  = bin2bcd(tm->tm_hour);	/* luon ghi che do 24h */
	regs[DS1307_REG_WDAY]  = bin2bcd(tm->tm_wday + 1);
	regs[DS1307_REG_MDAY]  = bin2bcd(tm->tm_mday);
	regs[DS1307_REG_MONTH] = bin2bcd(tm->tm_mon + 1);
	regs[DS1307_REG_YEAR]  = bin2bcd(tm->tm_year - 100);

	mutex_lock(&ds->lock);
	ret = ds_write_regs(ds, DS1307_REG_SECS, regs, sizeof(regs));
	mutex_unlock(&ds->lock);

	if (ret) {
		dev_err(dev, "set time failed: %d\n", ret);
		return ret;
	}

	dev_dbg(dev, "set %ptR\n", tm);
	return 0;
}

static const struct rtc_class_ops ds_rtc_ops = {
	.read_time = ds_read_time,
	.set_time  = ds_set_time,
};

/* ------------------------------------------------------------------ */
/* NVRAM 56 byte (nvmem)                                              */
/* ------------------------------------------------------------------ */

static int ds_nvram_read(void *priv, unsigned int offset, void *val,
			 size_t bytes)
{
	struct ds1307_plat *ds = priv;
	int ret;

	if (offset >= DS1307_NVRAM_SIZE || bytes > DS1307_NVRAM_SIZE - offset)
		return -EINVAL;

	mutex_lock(&ds->lock);
	ret = ds_read_regs(ds, DS1307_NVRAM_BASE + offset, val, bytes);
	mutex_unlock(&ds->lock);
	return ret;
}

static int ds_nvram_write(void *priv, unsigned int offset, void *val,
			  size_t bytes)
{
	struct ds1307_plat *ds = priv;
	int ret;

	if (offset >= DS1307_NVRAM_SIZE || bytes > DS1307_NVRAM_SIZE - offset)
		return -EINVAL;

	mutex_lock(&ds->lock);
	ret = ds_write_regs(ds, DS1307_NVRAM_BASE + offset, val, bytes);
	mutex_unlock(&ds->lock);
	return ret;
}

/* ------------------------------------------------------------------ */
/* sysfs: /sys/class/rtc/rtcN/{sqw,regs}                              */
/* ------------------------------------------------------------------ */

static const unsigned int ds_sqw_rates[] = { 1, 4096, 8192, 32768 };

/* Cac thuoc tinh nam tren thiet bi rtc, parent cua no la platform device */
static struct ds1307_plat *attr_to_ds(struct device *dev)
{
	return dev_get_drvdata(dev->parent);
}

static ssize_t sqw_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct ds1307_plat *ds = attr_to_ds(dev);
	u8 ctrl;
	int ret;

	mutex_lock(&ds->lock);
	ret = ds_read_regs(ds, DS1307_REG_CONTROL, &ctrl, 1);
	mutex_unlock(&ds->lock);
	if (ret)
		return ret;

	if (!(ctrl & DS1307_BIT_SQWE))
		return sysfs_emit(buf, "off\n");
	return sysfs_emit(buf, "%u\n", ds_sqw_rates[ctrl & DS1307_RS_MASK]);
}

/* echo off|1|4096|8192|32768 > /sys/class/rtc/rtcN/sqw */
static ssize_t sqw_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct ds1307_plat *ds = attr_to_ds(dev);
	unsigned int rate;
	u8 ctrl;
	int i, ret;

	if (sysfs_streq(buf, "off") || sysfs_streq(buf, "0")) {
		ctrl = 0;			/* SQWE = 0, OUT = 0 */
	} else {
		ret = kstrtouint(buf, 0, &rate);
		if (ret)
			return ret;
		for (i = 0; i < ARRAY_SIZE(ds_sqw_rates); i++)
			if (ds_sqw_rates[i] == rate)
				break;
		if (i == ARRAY_SIZE(ds_sqw_rates))
			return -EINVAL;
		ctrl = DS1307_BIT_SQWE | i;
	}

	mutex_lock(&ds->lock);
	ret = ds_write_regs(ds, DS1307_REG_CONTROL, &ctrl, 1);
	mutex_unlock(&ds->lock);

	return ret ? ret : count;
}
static DEVICE_ATTR_RW(sqw);

/* cat /sys/class/rtc/rtcN/regs -> 8 thanh ghi 0x00..0x07 dang hex */
static ssize_t regs_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct ds1307_plat *ds = attr_to_ds(dev);
	u8 regs[DS1307_REG_CONTROL + 1];
	int ret;

	mutex_lock(&ds->lock);
	ret = ds_read_regs(ds, DS1307_REG_SECS, regs, sizeof(regs));
	mutex_unlock(&ds->lock);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%*ph\n", (int)sizeof(regs), regs);
}
static DEVICE_ATTR_RO(regs);

static struct attribute *ds_attrs[] = {
	&dev_attr_sqw.attr,
	&dev_attr_regs.attr,
	NULL,
};

static const struct attribute_group ds_attr_group = {
	.attrs = ds_attrs,
};

/* ------------------------------------------------------------------ */
/* probe / remove                                                     */
/* ------------------------------------------------------------------ */

static void ds_put_adapter(void *data)
{
	i2c_put_adapter(data);
}

static int ds_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *bus_np;
	struct nvmem_config nvmem_cfg = {
		.name      = "ds1307_nvram",
		.id        = NVMEM_DEVID_AUTO,
		.word_size = 1,
		.stride    = 1,
		.size      = DS1307_NVRAM_SIZE,
		.reg_read  = ds_nvram_read,
		.reg_write = ds_nvram_write,
	};
	struct ds1307_plat *ds;
	u8 regs[DS1307_REG_CONTROL + 1];
	u32 addr;
	int ret;

	if (!np)
		return dev_err_probe(dev, -ENODEV, "no Device Tree node\n");

	ds = devm_kzalloc(dev, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return -ENOMEM;
	ds->dev = dev;
	mutex_init(&ds->lock);
	platform_set_drvdata(pdev, ds);

	/* 1. Lay bus I2C tu thuoc tinh "i2c-bus" (phandle) trong Device Tree */
	bus_np = of_parse_phandle(np, "i2c-bus", 0);
	if (!bus_np)
		return dev_err_probe(dev, -EINVAL, "missing 'i2c-bus' phandle in DT\n");

	ds->adap = of_get_i2c_adapter_by_node(bus_np);
	of_node_put(bus_np);
	if (!ds->adap)	/* driver i2c-bcm2835 chua probe xong -> thu lai sau */
		return dev_err_probe(dev, -EPROBE_DEFER, "I2C bus not ready yet\n");

	ret = devm_add_action_or_reset(dev, ds_put_adapter, ds->adap);
	if (ret)
		return ret;

	/* 2. Chiem dia chi I2C cua DS1307 */
	if (of_property_read_u32(np, "i2c-address", &addr))
		addr = DS1307_DEFAULT_ADDR;
	if (addr > 0x7f)
		return dev_err_probe(dev, -EINVAL, "bad i2c-address 0x%x\n", addr);

	ds->client = devm_i2c_new_dummy_device(dev, ds->adap, addr);
	if (IS_ERR(ds->client))
		return dev_err_probe(dev, PTR_ERR(ds->client),
				     "cannot claim address 0x%02x on %s (used by another driver, e.g. rtc-ds1307?)\n",
				     addr, dev_name(&ds->adap->dev));

	/* 3. Kiem tra chip co tra loi khong */
	ret = ds_read_regs(ds, DS1307_REG_SECS, regs, sizeof(regs));
	if (ret)
		return dev_err_probe(dev, -ENODEV,
				     "no DS1307 answering at 0x%02x on %s (err %d): check wiring and 5V power\n",
				     addr, dev_name(&ds->adap->dev), ret);

	/* Toan 0xFF: bus khong co thiet bi (SDA bi keo len, khong ai tra loi) */
	if (!memchr_inv(regs, 0xff, sizeof(regs)))
		return dev_err_probe(dev, -ENODEV,
				     "no DS1307 at 0x%02x on %s (bus reads all 0xff): check wiring and 5V power\n",
				     addr, dev_name(&ds->adap->dev));

	dev_info(dev, "DS1307 found at 0x%02x on %s, regs: %*ph\n",
		 addr, dev_name(&ds->adap->dev), (int)sizeof(regs), regs);

	/*
	 * Pin moi lap / pin het: bo dao dong dang dung (CH = 1). Thoi gian
	 * khong hop le; lan dat gio dau tien (set_time) se xoa CH.
	 */
	if (regs[DS1307_REG_SECS] & DS1307_BIT_CH)
		dev_warn(dev, "oscillator is stopped (new/empty battery?): set the time with 'hwclock -w'\n");

	/* 4. Dang ky thiet bi RTC */
	ds->rtc = devm_rtc_allocate_device(dev);
	if (IS_ERR(ds->rtc))
		return PTR_ERR(ds->rtc);

	ds->rtc->ops       = &ds_rtc_ops;
	ds->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	ds->rtc->range_max = RTC_TIMESTAMP_END_2099;
	clear_bit(RTC_FEATURE_ALARM, ds->rtc->features);   /* DS1307 khong co bao thuc */
	clear_bit(RTC_FEATURE_UPDATE_INTERRUPT, ds->rtc->features);

	/* 6. Thuoc tinh sysfs - phai them truoc khi register */
	ret = rtc_add_group(ds->rtc, &ds_attr_group);
	if (ret)
		return ret;

	ret = devm_rtc_register_device(ds->rtc);
	if (ret)
		return ret;

	/* 5. NVRAM: khong bat buoc, loi thi chi canh bao */
	nvmem_cfg.priv = ds;
	ret = devm_rtc_nvmem_register(ds->rtc, &nvmem_cfg);
	if (ret)
		dev_warn(dev, "NVRAM not registered: %d\n", ret);

	/* rtc core tu in "registered as rtcN" */
	return 0;
}

static void ds_remove(struct platform_device *pdev)
{
	struct ds1307_plat *ds = platform_get_drvdata(pdev);

	/* rtc, nvmem, client I2C, adapter: giai phong tu dong boi devm */
	dev_info(&pdev->dev, "removing %s\n", dev_name(&ds->rtc->dev));
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
static int ds_remove_int(struct platform_device *pdev)
{
	ds_remove(pdev);
	return 0;
}
#endif

static const struct of_device_id ds_of_match[] = {
	{ .compatible = "edu,ds1307-platform" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ds_of_match);

static struct platform_driver ds_platform_driver = {
	.probe  = ds_probe,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
	.remove = ds_remove,		/* 6.11+: remove() tra ve void */
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
	.remove_new = ds_remove,
#else
	.remove = ds_remove_int,
#endif
	.driver = {
		.name           = DRV_NAME,
		.of_match_table = ds_of_match,
	},
};
module_platform_driver(ds_platform_driver);

MODULE_AUTHOR("Tuan");
MODULE_DESCRIPTION("Platform driver for DS1307 RTC (I2C) on Raspberry Pi");
MODULE_LICENSE("GPL");
