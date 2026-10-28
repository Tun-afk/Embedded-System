// SPDX-License-Identifier: GPL-2.0
/*
 * Platform driver tu viet cho khoi PWM tich hop trong SoC BCM2835
 * (Raspberry Pi Zero / Zero W). Thay the driver co san pwm-bcm2835.
 *
 * Mo hinh platform driver:
 *
 *   Node &pwm (pwm@7e20c000) da co san trong Device Tree cua Pi, mac dinh
 *   status = "disabled" va compatible = "brcm,bcm2835-pwm". Overlay
 *   bcm-pwm-plat-overlay.dts "de" len node nay:
 *       compatible = "edu,bcm2835-pwm-plat";   -> driver co san khong match nua
 *       status     = "okay";
 *       pinctrl-0  = <&pwm0_gpio18>;           -> GPIO18 = PWM0 (ALT5)
 *
 *   Kernel tao platform_device cho node, compatible khop of_match_table
 *   ben duoi -> goi probe(). Trong probe():
 *     1. anh xa thanh ghi (resource "reg" trong DT)
 *     2. lay va bat clock PWM (resource "clocks" trong DT, 50 MHz)
 *     3. dang ky pwm_chip voi PWM framework -> /sys/class/pwm/pwmchipN
 *   remove(): khi rmmod/unbind; pwm_chip go tu dong (devm), sau do tat
 *   ca 2 kenh PWM.
 *
 * Ban do thanh ghi (BCM2835 ARM Peripherals, chuong 9):
 *   0x00 CTL : moi kenh 8 bit (kenh 0: bit 0..7, kenh 1: bit 8..15)
 *              PWEN(0) MODE(1) RPTL(2) SBIT(3) POLA(4) USEF(5) CLRF(6) MSEN(7)
 *   0x04 STA
 *   0x10 RNG1, 0x14 DAT1   (kenh 0)
 *   0x20 RNG2, 0x24 DAT2   (kenh 1)
 *
 *   Che do mark-space (MSEN = 1): chu ky = RNG xung clock, muc cao = DAT xung.
 *   Voi clock 50 MHz: 1 xung = 20 ns.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/version.h>

#define DRV_NAME		"bcm-pwm-plat"
#define NUM_CHANNELS		2

#define PWM_CTL			0x00
#define PWM_STA			0x04
#define PWM_RNG(ch)		(0x10 + (ch) * 0x10)
#define PWM_DAT(ch)		(0x14 + (ch) * 0x10)

#define CTL_SHIFT(ch)		((ch) * 8)
#define CTL_PWEN		BIT(0)	/* bat kenh */
#define CTL_POLA		BIT(4)	/* dao cuc tinh */
#define CTL_MSEN		BIT(7)	/* che do mark-space */
#define CTL_CH_MASK		0xff

#define RNG_MIN			2	/* RNG toi thieu theo datasheet */

struct bcm_pwm {
	void __iomem	*base;
	struct clk	*clk;
	unsigned long	rate;		/* Hz */
	spinlock_t	lock;		/* CTL dung chung cho 2 kenh */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 9, 0)
	struct pwm_chip	chip;
#endif
};

static struct bcm_pwm *to_bcm_pwm(struct pwm_chip *chip)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
	return pwmchip_get_drvdata(chip);
#else
	return container_of(chip, struct bcm_pwm, chip);
#endif
}

/* Doi ns -> so xung clock (lam tron xuong) */
static u64 ns_to_ticks(struct bcm_pwm *pc, u64 ns)
{
	return mul_u64_u64_div_u64(ns, pc->rate, NSEC_PER_SEC);
}

static u64 ticks_to_ns(struct bcm_pwm *pc, u64 ticks)
{
	return DIV64_U64_ROUND_UP(ticks * NSEC_PER_SEC, pc->rate);
}

static void ctl_update(struct bcm_pwm *pc, unsigned int ch, u32 bits)
{
	unsigned long flags;
	u32 ctl;

	spin_lock_irqsave(&pc->lock, flags);
	ctl = readl(pc->base + PWM_CTL);
	ctl &= ~(CTL_CH_MASK << CTL_SHIFT(ch));
	ctl |= bits << CTL_SHIFT(ch);
	writel(ctl, pc->base + PWM_CTL);
	spin_unlock_irqrestore(&pc->lock, flags);
}

/*
 * Ham chinh cua PWM framework: dua phan cung ve trang thai @state
 * (period, duty_cycle tinh bang ns; polarity; enabled).
 */
static int bcm_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			 const struct pwm_state *state)
{
	struct bcm_pwm *pc = to_bcm_pwm(chip);
	unsigned int ch = pwm->hwpwm;
	u64 range, duty;
	u32 bits;

	if (!state->enabled) {
		ctl_update(pc, ch, 0);
		return 0;
	}

	range = ns_to_ticks(pc, state->period);
	if (range < RNG_MIN)
		return -EINVAL;		/* chu ky qua ngan (< 40 ns) */
	if (range > U32_MAX)
		range = U32_MAX;	/* lay chu ky dai nhat co the */

	duty = ns_to_ticks(pc, state->duty_cycle);
	if (duty > range)
		duty = range;

	writel((u32)range, pc->base + PWM_RNG(ch));
	writel((u32)duty, pc->base + PWM_DAT(ch));

	bits = CTL_MSEN | CTL_PWEN;
	if (state->polarity == PWM_POLARITY_INVERSED)
		bits |= CTL_POLA;
	ctl_update(pc, ch, bits);

	return 0;
}

/* Doc lai trang thai thuc te tu thanh ghi (framework goi khi dang ky) */
static int bcm_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			     struct pwm_state *state)
{
	struct bcm_pwm *pc = to_bcm_pwm(chip);
	unsigned int ch = pwm->hwpwm;
	u32 ctl = readl(pc->base + PWM_CTL) >> CTL_SHIFT(ch);

	state->enabled    = !!(ctl & CTL_PWEN);
	state->polarity   = (ctl & CTL_POLA) ? PWM_POLARITY_INVERSED :
					       PWM_POLARITY_NORMAL;
	state->period     = ticks_to_ns(pc, readl(pc->base + PWM_RNG(ch)));
	state->duty_cycle = ticks_to_ns(pc, readl(pc->base + PWM_DAT(ch)));
	return 0;
}

static const struct pwm_ops bcm_pwm_ops = {
	.apply     = bcm_pwm_apply,
	.get_state = bcm_pwm_get_state,
};

/* ------------------------------------------------------------------ */
/* sysfs tren platform device: regs (dump thanh ghi), clock_hz        */
/* ------------------------------------------------------------------ */

static ssize_t regs_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct bcm_pwm *pc = dev_get_drvdata(dev);

	return sysfs_emit(buf,
			  "CTL=0x%08x STA=0x%08x RNG1=%u DAT1=%u RNG2=%u DAT2=%u\n",
			  readl(pc->base + PWM_CTL), readl(pc->base + PWM_STA),
			  readl(pc->base + PWM_RNG(0)), readl(pc->base + PWM_DAT(0)),
			  readl(pc->base + PWM_RNG(1)), readl(pc->base + PWM_DAT(1)));
}
static DEVICE_ATTR_RO(regs);

static ssize_t clock_hz_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct bcm_pwm *pc = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%lu\n", pc->rate);
}
static DEVICE_ATTR_RO(clock_hz);

static struct attribute *bcm_pwm_attrs[] = {
	&dev_attr_regs.attr,
	&dev_attr_clock_hz.attr,
	NULL,
};
ATTRIBUTE_GROUPS(bcm_pwm);

/* ------------------------------------------------------------------ */
/* probe / remove                                                     */
/* ------------------------------------------------------------------ */

/* Chay khi go driver, SAU khi pwm_chip da duoc go: tat ca 2 kenh */
static void bcm_pwm_hw_off(void *data)
{
	struct bcm_pwm *pc = data;

	writel(0, pc->base + PWM_CTL);
}

static int bcm_pwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pwm_chip *chip;
	struct bcm_pwm *pc;
	int ret;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
	chip = devm_pwmchip_alloc(dev, NUM_CHANNELS, sizeof(*pc));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	pc = to_bcm_pwm(chip);
#else
	pc = devm_kzalloc(dev, sizeof(*pc), GFP_KERNEL);
	if (!pc)
		return -ENOMEM;
	chip = &pc->chip;
	chip->dev  = dev;
	chip->npwm = NUM_CHANNELS;
#endif
	chip->ops = &bcm_pwm_ops;
	spin_lock_init(&pc->lock);
	platform_set_drvdata(pdev, pc);

	/* 1. Thanh ghi: lay tu thuoc tinh "reg" cua node DT */
	pc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pc->base))
		return dev_err_probe(dev, PTR_ERR(pc->base), "cannot map registers\n");

	/* 2. Clock: lay tu thuoc tinh "clocks", bat clock (tu tat khi go) */
	pc->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(pc->clk))
		return dev_err_probe(dev, PTR_ERR(pc->clk), "cannot get PWM clock\n");

	pc->rate = clk_get_rate(pc->clk);
	if (!pc->rate || pc->rate > NSEC_PER_SEC)
		return dev_err_probe(dev, -EINVAL, "bad PWM clock rate %lu Hz\n",
				     pc->rate);

	/* Dang ky tat phan cung TRUOC pwmchip_add -> chay SAU khi chip bi go */
	ret = devm_add_action_or_reset(dev, bcm_pwm_hw_off, pc);
	if (ret)
		return ret;

	/* 3. Dang ky voi PWM framework -> /sys/class/pwm/pwmchipN */
	ret = devm_pwmchip_add(dev, chip);
	if (ret)
		return dev_err_probe(dev, ret, "cannot register pwm chip\n");

	dev_info(dev, "PWM ready: %d channels, clock %lu Hz (%llu ns/tick), CTL=0x%08x\n",
		 NUM_CHANNELS, pc->rate, ticks_to_ns(pc, 1),
		 readl(pc->base + PWM_CTL));
	return 0;
}

static void bcm_pwm_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "removing PWM (outputs will be switched off)\n");
	/* pwm_chip, ham tat phan cung, clock, iomap: devm giai phong theo thu tu nguoc */
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
static int bcm_pwm_remove_int(struct platform_device *pdev)
{
	bcm_pwm_remove(pdev);
	return 0;
}
#endif

static const struct of_device_id bcm_pwm_of_match[] = {
	{ .compatible = "edu,bcm2835-pwm-plat" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, bcm_pwm_of_match);

static struct platform_driver bcm_pwm_driver = {
	.probe  = bcm_pwm_probe,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
	.remove = bcm_pwm_remove,
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
	.remove_new = bcm_pwm_remove,
#else
	.remove = bcm_pwm_remove_int,
#endif
	.driver = {
		.name           = DRV_NAME,
		.of_match_table = bcm_pwm_of_match,
		.dev_groups     = bcm_pwm_groups,
	},
};
module_platform_driver(bcm_pwm_driver);

MODULE_AUTHOR("Tuan");
MODULE_DESCRIPTION("Platform driver for the BCM2835 SoC PWM block (replaces pwm-bcm2835)");
MODULE_LICENSE("GPL");
