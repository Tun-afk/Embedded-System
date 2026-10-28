// SPDX-License-Identifier: GPL-2.0
/*
 * Platform driver tu viet cho kenh 1 cua System Timer tich hop trong
 * SoC BCM2835 (Raspberry Pi Zero / Zero W).
 *
 * System Timer: bo dem tu do 64 bit, 1 MHz (1 tick = 1 us) va 4 thanh ghi
 * so sanh C0..C3. Khi 32 bit thap cua bo dem (CLO) bang Cn, bit Mn trong
 * thanh ghi CS duoc set va ngat "timer n" duoc phat.
 *   - kenh 0 va 2: GPU (firmware) dung
 *   - kenh 3     : Linux dung lam timer he thong (driver bcm2835_timer,
 *                  built-in, khoi tao bang TIMER_OF_DECLARE, khong the go)
 *   - kenh 1     : BO TRONG -> driver nay dung
 *
 * Mo hinh platform driver:
 *   Overlay bcm-timer-plat-overlay.dts them node
 *       edu-timer@7e003000 {
 *           compatible = "edu,bcm2835-timer-ch1";
 *           reg        = <0x7e003000 0x1c>;   // CS, CLO, CHI, C0..C3
 *           interrupts = <1 1>;               // ngat GPU so 1 = timer kenh 1
 *           led-gpios  = <&gpio 17 0>;        // LED nhay moi lan ngat
 *           edu,period-us = <500000>;
 *       };
 *   compatible khop of_match_table -> probe():
 *     1. anh xa thanh ghi (dung chung voi timer he thong -> KHONG request region)
 *     2. lay so IRQ va GPIO LED tu DT, dang ky trinh phuc vu ngat
 *     3. lap lich ngat dau tien, them thuoc tinh sysfs
 *   remove(): dung lap lich, xoa co ngat, tat LED; IRQ/GPIO giai phong boi devm.
 *
 * sysfs (/sys/bus/platform/drivers/bcm-timer-plat/<thiet bi>/):
 *   enable          rw  1 = chay, 0 = dung (ghi 1 se reset thong ke)
 *   period_us       rw  chu ky ngat, 100 .. 10000000 us
 *   irq_count       ro  so lan ngat tu lan enable gan nhat
 *   missed          ro  so chu ky bi lo (ngat den qua tre)
 *   max_latency_us  ro  do tre lon nhat tu luc so sanh khop den luc vao ISR
 *   counter         ro  gia tri bo dem 64 bit (us ke tu khi bat nguon)
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/version.h>

#define DRV_NAME		"bcm-timer-plat"

#define ST_CS			0x00
#define ST_CLO			0x04
#define ST_CHI			0x08
#define ST_C(n)			(0x0c + (n) * 4)

#define TIMER_CH		1
#define CS_MATCH		BIT(TIMER_CH)

#define PERIOD_MIN_US		100
#define PERIOD_MAX_US		10000000
#define PERIOD_DEFAULT_US	500000
#define ARM_MARGIN_US		10	/* compare phai o tuong lai it nhat 10 us */

struct bcm_timer {
	struct device		*dev;
	void __iomem		*base;
	int			irq;
	struct gpio_desc	*led;		/* co the NULL */
	spinlock_t		lock;		/* bao ve cac truong ben duoi */
	bool			enabled;
	u32			period_us;
	u32			next;		/* gia tri compare dang cho */
	u64			irq_count;
	u64			missed;
	u32			max_latency;
	bool			led_on;
};

static inline u32 st_read(struct bcm_timer *t, u32 reg)
{
	return readl(t->base + reg);
}

static inline void st_write(struct bcm_timer *t, u32 reg, u32 val)
{
	writel(val, t->base + reg);
}

/* Doc bo dem 64 bit nhat quan (CHI co the doi giua 2 lan doc) */
static u64 st_counter(struct bcm_timer *t)
{
	u32 hi, lo, hi2;

	do {
		hi  = st_read(t, ST_CHI);
		lo  = st_read(t, ST_CLO);
		hi2 = st_read(t, ST_CHI);
	} while (hi != hi2);

	return ((u64)hi << 32) | lo;
}

/*
 * Dat compare cho lan ngat tiep theo. Cong don tu moc truoc (next += period)
 * de khong bi troi chu ky; neu moc da qua (ngat den qua tre) thi tinh lai
 * tu thoi diem hien tai va dem 1 lan "missed".
 * Goi khi dang giu t->lock.
 */
static void st_arm(struct bcm_timer *t, bool from_now)
{
	u32 now = st_read(t, ST_CLO);

	if (from_now) {
		/* bo co khop cu (neu co) de khong sinh ngat "cu" sau khi doi moc */
		st_write(t, ST_CS, CS_MATCH);
		t->next = now + t->period_us;
	} else {
		t->next += t->period_us;
		if ((s32)(t->next - now) < ARM_MARGIN_US) {
			t->missed++;
			t->next = now + t->period_us;
		}
	}
	st_write(t, ST_C(TIMER_CH), t->next);
}

static irqreturn_t bcm_timer_isr(int irq, void *data)
{
	struct bcm_timer *t = data;
	u32 now, lat;

	if (!(st_read(t, ST_CS) & CS_MATCH))
		return IRQ_NONE;

	/* Xoa co M1: ghi 1 vao dung bit cua kenh 1 (khong anh huong kenh khac) */
	st_write(t, ST_CS, CS_MATCH);

	spin_lock(&t->lock);
	now = st_read(t, ST_CLO);
	/*
	 * Chi xu ly khi da toi moc dang cho. Neu moc vua bi doi (sysfs) sau khi
	 * co khop cu da bat, (now - next) am -> bo qua, compare moi van dang cho.
	 */
	if (t->enabled && (s32)(now - t->next) >= 0) {
		lat = now - t->next;
		if (lat > t->max_latency)
			t->max_latency = lat;
		t->irq_count++;

		if (t->led) {
			t->led_on = !t->led_on;
			gpiod_set_value(t->led, t->led_on);
		}
		st_arm(t, false);
	}
	spin_unlock(&t->lock);

	return IRQ_HANDLED;
}

/* Bat/tat lap lich. Goi khi KHONG giu lock. */
static void bcm_timer_set_enabled(struct bcm_timer *t, bool on)
{
	unsigned long flags;

	spin_lock_irqsave(&t->lock, flags);
	t->enabled = on;
	if (on) {
		t->irq_count   = 0;
		t->missed      = 0;
		t->max_latency = 0;
		st_arm(t, true);
	} else {
		st_write(t, ST_CS, CS_MATCH);	/* bo co ngat dang treo neu co */
		if (t->led) {
			t->led_on = false;
			gpiod_set_value(t->led, 0);
		}
	}
	spin_unlock_irqrestore(&t->lock, flags);
}

/* ------------------------------------------------------------------ */
/* sysfs                                                              */
/* ------------------------------------------------------------------ */

static ssize_t enable_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct bcm_timer *t = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", READ_ONCE(t->enabled));
}

static ssize_t enable_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct bcm_timer *t = dev_get_drvdata(dev);
	bool on;
	int ret;

	ret = kstrtobool(buf, &on);
	if (ret)
		return ret;
	bcm_timer_set_enabled(t, on);
	return count;
}
static DEVICE_ATTR_RW(enable);

static ssize_t period_us_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct bcm_timer *t = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", READ_ONCE(t->period_us));
}

static ssize_t period_us_store(struct device *dev, struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct bcm_timer *t = dev_get_drvdata(dev);
	unsigned long flags;
	u32 val;
	int ret;

	ret = kstrtou32(buf, 0, &val);
	if (ret)
		return ret;
	if (val < PERIOD_MIN_US || val > PERIOD_MAX_US)
		return -EINVAL;

	spin_lock_irqsave(&t->lock, flags);
	t->period_us = val;
	if (t->enabled)
		st_arm(t, true);	/* ap dung ngay, tinh tu bay gio */
	spin_unlock_irqrestore(&t->lock, flags);

	return count;
}
static DEVICE_ATTR_RW(period_us);

/* Doc nhat quan cac bo dem 64 bit (tren ARM 32 bit can khoa) */
#define BCM_TIMER_STAT_ATTR(_name, _fmt)				\
static ssize_t _name##_show(struct device *dev,				\
			    struct device_attribute *attr, char *buf)	\
{									\
	struct bcm_timer *t = dev_get_drvdata(dev);			\
	unsigned long flags;						\
	u64 v;								\
									\
	spin_lock_irqsave(&t->lock, flags);				\
	v = t->_name;							\
	spin_unlock_irqrestore(&t->lock, flags);			\
	return sysfs_emit(buf, _fmt "\n", v);				\
}									\
static DEVICE_ATTR_RO(_name)

BCM_TIMER_STAT_ATTR(irq_count, "%llu");
BCM_TIMER_STAT_ATTR(missed, "%llu");

static ssize_t max_latency_us_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct bcm_timer *t = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%u\n", READ_ONCE(t->max_latency));
}
static DEVICE_ATTR_RO(max_latency_us);

static ssize_t counter_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct bcm_timer *t = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%llu\n", st_counter(t));
}
static DEVICE_ATTR_RO(counter);

static struct attribute *bcm_timer_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_period_us.attr,
	&dev_attr_irq_count.attr,
	&dev_attr_missed.attr,
	&dev_attr_max_latency_us.attr,
	&dev_attr_counter.attr,
	NULL,
};
ATTRIBUTE_GROUPS(bcm_timer);

/* ------------------------------------------------------------------ */
/* probe / remove                                                     */
/* ------------------------------------------------------------------ */

static int bcm_timer_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bcm_timer *t;
	struct resource *res;
	u32 period;
	int ret;

	t = devm_kzalloc(dev, sizeof(*t), GFP_KERNEL);
	if (!t)
		return -ENOMEM;
	t->dev = dev;
	spin_lock_init(&t->lock);
	platform_set_drvdata(pdev, t);

	/*
	 * 1. Thanh ghi. Khoi System Timer dung chung voi timer he thong
	 * (kenh 3), nen chi anh xa, khong "chiem" vung nho (request_mem_region).
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return dev_err_probe(dev, -EINVAL, "missing 'reg' in DT\n");
	t->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!t->base)
		return -ENOMEM;

	/* 2. IRQ va LED tu Device Tree */
	t->irq = platform_get_irq(pdev, 0);
	if (t->irq < 0)
		return t->irq;

	t->led = devm_gpiod_get_optional(dev, "led", GPIOD_OUT_LOW);
	if (IS_ERR(t->led))
		return dev_err_probe(dev, PTR_ERR(t->led), "cannot get led-gpios\n");
	if (t->led && gpiod_cansleep(t->led))
		return dev_err_probe(dev, -EINVAL,
				     "LED GPIO can sleep, not usable from hard IRQ\n");

	if (of_property_read_u32(dev->of_node, "edu,period-us", &period))
		period = PERIOD_DEFAULT_US;
	t->period_us = clamp_t(u32, period, PERIOD_MIN_US, PERIOD_MAX_US);

	/* Xoa co M1 cu (neu co) truoc khi dang ky ngat */
	st_write(t, ST_CS, CS_MATCH);

	ret = devm_request_irq(dev, t->irq, bcm_timer_isr, 0, dev_name(dev), t);
	if (ret)
		return dev_err_probe(dev, ret, "cannot request IRQ %d\n", t->irq);

	/* 3. Bat dau chay */
	bcm_timer_set_enabled(t, true);

	dev_info(dev, "System Timer channel %d: IRQ %d, period %u us, LED %s, counter %llu us\n",
		 TIMER_CH, t->irq, t->period_us,
		 t->led ? "on GPIO" : "none", st_counter(t));
	return 0;
}

static void bcm_timer_remove(struct platform_device *pdev)
{
	struct bcm_timer *t = platform_get_drvdata(pdev);

	bcm_timer_set_enabled(t, false);
	synchronize_irq(t->irq);	/* cho ISR dang chay (neu co) ket thuc */
	dev_info(&pdev->dev, "removing timer (%llu interrupts since last enable)\n",
		 t->irq_count);
	/* IRQ, GPIO, iomap: devm giai phong sau ham nay */
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
static int bcm_timer_remove_int(struct platform_device *pdev)
{
	bcm_timer_remove(pdev);
	return 0;
}
#endif

static const struct of_device_id bcm_timer_of_match[] = {
	{ .compatible = "edu,bcm2835-timer-ch1" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, bcm_timer_of_match);

static struct platform_driver bcm_timer_driver = {
	.probe  = bcm_timer_probe,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
	.remove = bcm_timer_remove,
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
	.remove_new = bcm_timer_remove,
#else
	.remove = bcm_timer_remove_int,
#endif
	.driver = {
		.name           = DRV_NAME,
		.of_match_table = bcm_timer_of_match,
		.dev_groups     = bcm_timer_groups,
	},
};
module_platform_driver(bcm_timer_driver);

MODULE_AUTHOR("Tuan");
MODULE_DESCRIPTION("Platform driver for BCM2835 System Timer channel 1 (periodic IRQ + LED)");
MODULE_LICENSE("GPL");
