/*
 * twl4030_usb - TWL4030 USB transceiver, talking to OMAP OTG controller
 *
 * Copyright (C) 2004-2007 Texas Instruments
 * Copyright (C) 2008 Nokia Corporation
 * Contact: Felipe Balbi <felipe.balbi@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Current status:
 *	- HS USB ULPI mode works.
 *	- 3-pin mode support may be added in future.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/i2c/twl.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/wakelock.h>
#include <linux/notifier.h>
#include <linux/slab.h>
//&*&*&*Beacon_20120508,Add earlysuspend mechanism of usb
#include <linux/earlysuspend.h>

/* Register defines */

#define MCPC_CTRL			0x30
#define MCPC_CTRL_RTSOL			(1 << 7)
#define MCPC_CTRL_EXTSWR		(1 << 6)
#define MCPC_CTRL_EXTSWC		(1 << 5)
#define MCPC_CTRL_VOICESW		(1 << 4)
#define MCPC_CTRL_OUT64K		(1 << 3)
#define MCPC_CTRL_RTSCTSSW		(1 << 2)
#define MCPC_CTRL_HS_UART		(1 << 0)

#define MCPC_IO_CTRL			0x33
#define MCPC_IO_CTRL_MICBIASEN		(1 << 5)
#define MCPC_IO_CTRL_CTS_NPU		(1 << 4)
#define MCPC_IO_CTRL_RXD_PU		(1 << 3)
#define MCPC_IO_CTRL_TXDTYP		(1 << 2)
#define MCPC_IO_CTRL_CTSTYP		(1 << 1)
#define MCPC_IO_CTRL_RTSTYP		(1 << 0)

#define MCPC_CTRL2			0x36
#define MCPC_CTRL2_MCPC_CK_EN		(1 << 0)

#define OTHER_FUNC_CTRL			0x80
#define OTHER_FUNC_CTRL_BDIS_ACON_EN	(1 << 4)
#define OTHER_FUNC_CTRL_FIVEWIRE_MODE	(1 << 2)

#define OTHER_IFC_CTRL			0x83
#define OTHER_IFC_CTRL_OE_INT_EN	(1 << 6)
#define OTHER_IFC_CTRL_CEA2011_MODE	(1 << 5)
#define OTHER_IFC_CTRL_FSLSSERIALMODE_4PIN	(1 << 4)
#define OTHER_IFC_CTRL_HIZ_ULPI_60MHZ_OUT	(1 << 3)
#define OTHER_IFC_CTRL_HIZ_ULPI		(1 << 2)
#define OTHER_IFC_CTRL_ALT_INT_REROUTE	(1 << 0)

#define OTHER_INT_EN_RISE		0x86
#define OTHER_INT_EN_FALL		0x89
#define OTHER_INT_STS			0x8C
#define OTHER_INT_LATCH			0x8D
#define OTHER_INT_VB_SESS_VLD		(1 << 7)
#define OTHER_INT_DM_HI			(1 << 6) /* not valid for "latch" reg */
#define OTHER_INT_DP_HI			(1 << 5) /* not valid for "latch" reg */
#define OTHER_INT_BDIS_ACON		(1 << 3) /* not valid for "fall" regs */
#define OTHER_INT_MANU			(1 << 1)
#define OTHER_INT_ABNORMAL_STRESS	(1 << 0)

#define ID_STATUS			0x96
#define ID_RES_FLOAT			(1 << 4)
#define ID_RES_440K			(1 << 3)
#define ID_RES_200K			(1 << 2)
#define ID_RES_102K			(1 << 1)
#define ID_RES_GND			(1 << 0)

#define POWER_CTRL			0xAC
#define POWER_CTRL_OTG_ENAB		(1 << 5)

#define OTHER_IFC_CTRL2			0xAF
#define OTHER_IFC_CTRL2_ULPI_STP_LOW	(1 << 4)
#define OTHER_IFC_CTRL2_ULPI_TXEN_POL	(1 << 3)
#define OTHER_IFC_CTRL2_ULPI_4PIN_2430	(1 << 2)
#define OTHER_IFC_CTRL2_USB_INT_OUTSEL_MASK	(3 << 0) /* bits 0 and 1 */
#define OTHER_IFC_CTRL2_USB_INT_OUTSEL_INT1N	(0 << 0)
#define OTHER_IFC_CTRL2_USB_INT_OUTSEL_INT2N	(1 << 0)

#define REG_CTRL_EN			0xB2
#define REG_CTRL_ERROR			0xB5
#define ULPI_I2C_CONFLICT_INTEN		(1 << 0)

#define OTHER_FUNC_CTRL2		0xB8
#define OTHER_FUNC_CTRL2_VBAT_TIMER_EN	(1 << 0)

/* following registers do not have separate _clr and _set registers */
#define VBUS_DEBOUNCE			0xC0
#define ID_DEBOUNCE			0xC1
#define VBAT_TIMER			0xD3
#define PHY_PWR_CTRL			0xFD
#define PHY_PWR_PHYPWD			(1 << 0)
#define PHY_CLK_CTRL			0xFE
#define PHY_CLK_CTRL_CLOCKGATING_EN	(1 << 2)
#define PHY_CLK_CTRL_CLK32K_EN		(1 << 1)
#define REQ_PHY_DPLL_CLK		(1 << 0)
#define PHY_CLK_CTRL_STS		0xFF
#define PHY_DPLL_CLK			(1 << 0)

/* In module TWL4030_MODULE_PM_MASTER */
#define STS_HW_CONDITIONS		0x0F

/* In module TWL4030_MODULE_PM_RECEIVER */
#define VUSB_DEDICATED1			0x7D
#define VUSB_DEDICATED2			0x7E
#define VUSB1V5_DEV_GRP			0x71
#define VUSB1V5_TYPE			0x72
#define VUSB1V5_REMAP			0x73
#define VUSB1V8_DEV_GRP			0x74
#define VUSB1V8_TYPE			0x75
#define VUSB1V8_REMAP			0x76
#define VUSB3V1_DEV_GRP			0x77
#define VUSB3V1_TYPE			0x78
#define VUSB3V1_REMAP			0x79

/* In module TWL4030_MODULE_INTBR */
#define PMBR1				0x0D
#define GPIO_USB_4PIN_ULPI_2430C	(3 << 0)

#define TPS65921_USB_DTCT_CTRL		0x02
#define TPS65921_USB_CHG_DET_EN_SW	(1 << 7)
#define TPS65921_USB_DET_STS_MASK	(3 << 2)
#define TPS65921_USB_DET_STS_100MA	(1 << 2)
#define TPS65921_USB_DET_STS_500MA	(2 << 2)
#define TPS65921_USB_HW_CHRG_DET_EN	(1 << 0)

#define TPS65921_USB_SW_CHRG_CTRL	0x03
#define TPS65921_CHGD_DET_EN_SW		(1 << 7)
#define TPS65921_CHGD_SERX_DM_LOWV	(1 << 5)
#define TPS65921_CHGD_SERX_DP_LOWV	(1 << 4)

#define TPS65921_ACCISR1		0x05
#define TPS65921_ACCIMR1		0x06
#define TPS65921_USB_CHRG_TYPE_ISR1	(1 << 0)

//&*&*&*AL1_20110523, USB OTG/Charger switch on EVT3 of EP10
#define OTG_EN_1V8     156
#define CHG_OFFMODE    34
//&*&*&*AL2_20110523, USB OTG/Charger switch on EVT3 of EP10
struct twl4030_usb {
	struct otg_transceiver	otg;
	struct device		*dev;

	/* TWL4030 internal USB regulator supplies */
	struct regulator	*usb1v5;
	struct regulator	*usb1v8;
	struct regulator	*usb3v1;

	/* for vbus reporting with irqs disabled */
	spinlock_t		lock;

	/* pin configuration */
	enum twl4030_usb_mode	usb_mode;

	int			irq;
	u8			linkstat;
	bool			vbus_supplied;
	u8			asleep;
	bool			irq_enabled;
	struct early_suspend		early_suspend;
	int enter_early_suspend;//&*&*&*Beacon_20120508,Add earlysuspend mechanism of usb
};

static struct wake_lock usb_lock;
/* internal define on top of container_of */
#define xceiv_to_twl(x)		container_of((x), struct twl4030_usb, otg);

static struct twl4030_usb *g_twl_usb;

extern void SendPowerbuttonEvent( void );

/*-------------------------------------------------------------------------*/

static int twl4030_i2c_write_u8_verify(struct twl4030_usb *twl,
		u8 module, u8 data, u8 address)
{
	u8 check;

	if ((twl_i2c_write_u8(module, data, address) >= 0) &&
	    (twl_i2c_read_u8(module, &check, address) >= 0) &&
						(check == data))
		return 0;
	dev_dbg(twl->dev, "Write%d[%d,0x%x] wrote %02x but read %02x\n",
			1, module, address, check, data);

	/* Failed once: Try again */
	if ((twl_i2c_write_u8(module, data, address) >= 0) &&
	    (twl_i2c_read_u8(module, &check, address) >= 0) &&
						(check == data))
		return 0;
	dev_dbg(twl->dev, "Write%d[%d,0x%x] wrote %02x but read %02x\n",
			2, module, address, check, data);

	/* Failed again: Return error */
	return -EBUSY;
}

#define twl4030_usb_write_verify(twl, address, data)	\
	twl4030_i2c_write_u8_verify(twl, TWL4030_MODULE_USB, (data), (address))

static inline int twl4030_usb_write(struct twl4030_usb *twl,
		u8 address, u8 data)
{
	int ret = 0;

	ret = twl_i2c_write_u8(TWL4030_MODULE_USB, data, address);
	if (ret < 0)
		dev_dbg(twl->dev,
			"TWL4030:USB:Write[0x%x] Error %d\n", address, ret);
	return ret;
}

static inline int twl4030_readb(struct twl4030_usb *twl, u8 module, u8 address)
{
	u8 data;
	int ret = 0;

	ret = twl_i2c_read_u8(module, &data, address);
	if (ret >= 0)
		ret = data;
	else
		dev_dbg(twl->dev,
			"TWL4030:readb[0x%x,0x%x] Error %d\n",
					module, address, ret);

	return ret;
}

static inline int twl4030_usb_read(struct twl4030_usb *twl, u8 address)
{
	return twl4030_readb(twl, TWL4030_MODULE_USB, address);
}

/*-------------------------------------------------------------------------*/

static inline int
twl4030_usb_set_bits(struct twl4030_usb *twl, u8 reg, u8 bits)
{
	return twl4030_usb_write(twl, ULPI_SET(reg), bits);
}

static inline int
twl4030_usb_clear_bits(struct twl4030_usb *twl, u8 reg, u8 bits)
{
	return twl4030_usb_write(twl, ULPI_CLR(reg), bits);
}

/*-------------------------------------------------------------------------*/
/* <-- LH_SWRD_CL1_Henry@2011.8.6 delete Vibrator_EN and CCM_PWDN2 control from twl4030_otg_enable  */	
#if 0
int twl4030_otg_enable(int enable)
{
	printk("[twl4030-usb]Enter %s, enable:%d.\n", __FUNCTION__, enable);
	if(enable)
	{
		gpio_set_value(OTG_EN_1V8, 1);
		gpio_set_value(CHG_OFFMODE, 1);
	}
	else
	{
		gpio_set_value(OTG_EN_1V8, 0);
		gpio_set_value(CHG_OFFMODE, 0);
	}
	
	return 0;
}
#else
int twl4030_otg_enable(int enable)
{
	return 0;
}
#endif
/*  LH_SWRD_CL1_Henry@2011.8.6 delete Vibrator_EN and CCM_PWDN2 control from twl4030_otg_enable  -->*/	

EXPORT_SYMBOL(twl4030_otg_enable);

int twl4030_cable_type_detection(void)
{
	int dtct = 0;
	enum usb_xceiv_events stat = USB_EVENT_NONE;
	int status;
	//printk("[kernel3.0]Entry-----------------%s\n",__func__);
	if(g_twl_usb == NULL)
		return stat;
	/*
	 * Why sleep here instead of waiting for ACCISR1 interrupt?
	 *
	 * Upon cable plugging the VBUS and GND signals are the first to
	 * contact the connector pins, leaving D+/D- floating for a while so
	 * that the charger D+/D- short cannot be detected. This leaves us
	 * with the only option to wait a while after VBUS RISE IRQ, praying
	 * that cable will be fully inserted by then.
	 */

	status = twl4030_readb(g_twl_usb, TWL4030_MODULE_PM_MASTER,
			STS_HW_CONDITIONS);
	if (status < 0)
		dev_err(g_twl_usb->dev, "USB link status err %d\n", status);
	else if (status & (BIT(7) | BIT(2))) {
		if (status & BIT(2))
			stat = USB_EVENT_ID;
		else {
			/* Read charger detection status */
			dtct = twl4030_readb(g_twl_usb, TWL4030_MODULE_MAIN_CHARGE,
							TPS65921_USB_DTCT_CTRL);
			if ((dtct & TPS65921_USB_DET_STS_MASK) != TPS65921_USB_DET_STS_500MA)
				stat = USB_EVENT_VBUS;
			else
				stat = USB_EVENT_CHARGER;
			printk("[twl4030-usb]USB_DTCT_CTRL=0x%02x, status=0x%02x\n", dtct, status);
		}
	} else
		stat = USB_EVENT_NONE;

	if(!wake_lock_active(&usb_lock))
		wake_lock(&usb_lock);
	return stat;
}
EXPORT_SYMBOL(twl4030_cable_type_detection);

static enum usb_xceiv_events twl4030_usb_linkstat(struct twl4030_usb *twl)
{
	int	status;
	int	linkstat = USB_EVENT_NONE;

	twl->vbus_supplied = false;

	/*
	 * For ID/VBUS sensing, see manual section 15.4.8 ...
	 * except when using only battery backup power, two
	 * comparators produce VBUS_PRES and ID_PRES signals,
	 * which don't match docs elsewhere.  But ... BIT(7)
	 * and BIT(2) of STS_HW_CONDITIONS, respectively, do
	 * seem to match up.  If either is true the USB_PRES
	 * signal is active, the OTG module is activated, and
	 * its interrupt may be raised (may wake the system).
	 */
	status = twl4030_readb(twl, TWL4030_MODULE_PM_MASTER,
			STS_HW_CONDITIONS);
	if (status < 0)
		dev_err(twl->dev, "USB link status err %d\n", status);
	else if (status & (BIT(7) | BIT(2))) {
		if (status & (BIT(7)))
                        twl->vbus_supplied = true;

		if (status & BIT(2))
			linkstat = USB_EVENT_ID;
		else
			linkstat = USB_EVENT_VBUS;
	} else
		linkstat = USB_EVENT_NONE;

	dev_dbg(twl->dev, "HW_CONDITIONS 0x%02x/%d; link %d\n",
			status, status, linkstat);

	twl->otg.last_event = linkstat;

	/* REVISIT this assumes host and peripheral controllers
	 * are registered, and that both are active...
	 */

	spin_lock_irq(&twl->lock);
	twl->linkstat = linkstat;
	if (linkstat == USB_EVENT_ID) {
		twl->otg.default_a = true;
		twl->otg.state = OTG_STATE_A_IDLE;
	} else {
		twl->otg.default_a = false;
		twl->otg.state = OTG_STATE_B_IDLE;
	}
	spin_unlock_irq(&twl->lock);

	return linkstat;
}

static void twl4030_usb_set_mode(struct twl4030_usb *twl, int mode)
{
	twl->usb_mode = mode;

	switch (mode) {
	case T2_USB_MODE_ULPI:
		twl4030_usb_clear_bits(twl, ULPI_IFC_CTRL,
					ULPI_IFC_CTRL_CARKITMODE);
		twl4030_usb_set_bits(twl, POWER_CTRL, POWER_CTRL_OTG_ENAB);
		twl4030_usb_clear_bits(twl, ULPI_FUNC_CTRL,
					ULPI_FUNC_CTRL_XCVRSEL_MASK |
					ULPI_FUNC_CTRL_OPMODE_MASK);
		break;
	case -1:
		/* FIXME: power on defaults */
		break;
	default:
		dev_err(twl->dev, "unsupported T2 transceiver mode %d\n",
				mode);
		break;
	};
}

static void twl4030_i2c_access(struct twl4030_usb *twl, int on)
{
	unsigned long timeout;
	int val = twl4030_usb_read(twl, PHY_CLK_CTRL);

	if (val >= 0) {
		if (on) {
			/* enable DPLL to access PHY registers over I2C */
			val |= REQ_PHY_DPLL_CLK;
			WARN_ON(twl4030_usb_write_verify(twl, PHY_CLK_CTRL,
						(u8)val) < 0);

			timeout = jiffies + HZ;
			while (!(twl4030_usb_read(twl, PHY_CLK_CTRL_STS) &
							PHY_DPLL_CLK)
				&& time_before(jiffies, timeout))
					udelay(10);
			if (!(twl4030_usb_read(twl, PHY_CLK_CTRL_STS) &
							PHY_DPLL_CLK))
				dev_err(twl->dev, "Timeout setting T2 HSUSB "
						"PHY DPLL clock\n");
		} else {
			/* let ULPI control the DPLL clock */
			val &= ~REQ_PHY_DPLL_CLK;
			WARN_ON(twl4030_usb_write_verify(twl, PHY_CLK_CTRL,
						(u8)val) < 0);
		}
	}
}

static void __twl4030_phy_power(struct twl4030_usb *twl, int on)
{
	u8 pwr = twl4030_usb_read(twl, PHY_PWR_CTRL);

	if (on)
		pwr &= ~PHY_PWR_PHYPWD;
	else
		pwr |= PHY_PWR_PHYPWD;

	WARN_ON(twl4030_usb_write_verify(twl, PHY_PWR_CTRL, pwr) < 0);
}

static void twl4030_phy_power(struct twl4030_usb *twl, int on)
{
	if (on) {
		regulator_enable(twl->usb3v1);
		regulator_enable(twl->usb1v8);
		/*
		 * Disabling usb3v1 regulator (= writing 0 to VUSB3V1_DEV_GRP
		 * in twl4030) resets the VUSB_DEDICATED2 register. This reset
		 * enables VUSB3V1_SLEEP bit that remaps usb3v1 ACTIVE state to
		 * SLEEP. We work around this by clearing the bit after usv3v1
		 * is re-activated. This ensures that VUSB3V1 is really active.
		 */
		twl_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0,
							VUSB_DEDICATED2);
		regulator_enable(twl->usb1v5);
		__twl4030_phy_power(twl, 1);
		twl4030_usb_write(twl, PHY_CLK_CTRL,
				  twl4030_usb_read(twl, PHY_CLK_CTRL) |
					(PHY_CLK_CTRL_CLOCKGATING_EN |
						PHY_CLK_CTRL_CLK32K_EN));
	} else {
		__twl4030_phy_power(twl, 0);
		regulator_disable(twl->usb1v5);
		regulator_disable(twl->usb1v8);
		regulator_disable(twl->usb3v1);
	}
}

static void twl4030_phy_suspend(struct twl4030_usb *twl, int controller_off)
{
	if (twl->asleep)
		return;

	twl4030_phy_power(twl, 0);
	twl->asleep = 1;
	dev_dbg(twl->dev, "%s\n", __func__);
}

static void __twl4030_phy_resume(struct twl4030_usb *twl)
{
	twl4030_phy_power(twl, 1);
	twl4030_i2c_access(twl, 1);
	twl4030_usb_set_mode(twl, twl->usb_mode);
	if (twl->usb_mode == T2_USB_MODE_ULPI)
		twl4030_i2c_access(twl, 0);
}

static void twl4030_phy_resume(struct twl4030_usb *twl)
{
	if (!twl->asleep)
		return;
	/* Check the link status before enabling */
	if (twl4030_usb_linkstat(twl) == USB_EVENT_NONE)
		return;
	__twl4030_phy_resume(twl);
	twl->asleep = 0;
	dev_dbg(twl->dev, "%s\n", __func__);
}

static int twl4030_usb_ldo_init(struct twl4030_usb *twl)
{
	/* Enable writing to power configuration registers */
	twl_i2c_write_u8(TWL4030_MODULE_PM_MASTER,
			TWL4030_PM_MASTER_KEY_CFG1,
			TWL4030_PM_MASTER_PROTECT_KEY);

	twl_i2c_write_u8(TWL4030_MODULE_PM_MASTER,
			TWL4030_PM_MASTER_KEY_CFG2,
			TWL4030_PM_MASTER_PROTECT_KEY);

	/* Keep VUSB3V1 LDO in sleep state until VBUS/ID change detected*/
	/*twl_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0, VUSB_DEDICATED2);*/

	/* input to VUSB3V1 LDO is from VBAT, not VBUS */
	twl_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0x14, VUSB_DEDICATED1);

	/* Initialize 3.1V regulator */
	twl_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0, VUSB3V1_DEV_GRP);

	twl->usb3v1 = regulator_get(twl->dev, "usb3v1");
	if (IS_ERR(twl->usb3v1))
		return -ENODEV;

	twl_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0, VUSB3V1_TYPE);

	/* Initialize 1.5V regulator */
	twl_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0, VUSB1V5_DEV_GRP);

	twl->usb1v5 = regulator_get(twl->dev, "usb1v5");
	if (IS_ERR(twl->usb1v5))
		goto fail1;

	twl_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0, VUSB1V5_TYPE);

	/* Initialize 1.8V regulator */
	twl_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0, VUSB1V8_DEV_GRP);

	twl->usb1v8 = regulator_get(twl->dev, "usb1v8");
	if (IS_ERR(twl->usb1v8))
		goto fail2;

	twl_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0, VUSB1V8_TYPE);

	/* disable access to power configuration registers */
	twl_i2c_write_u8(TWL4030_MODULE_PM_MASTER, 0,
			TWL4030_PM_MASTER_PROTECT_KEY);

	return 0;

fail2:
	regulator_put(twl->usb1v5);
	twl->usb1v5 = NULL;
fail1:
	regulator_put(twl->usb3v1);
	twl->usb3v1 = NULL;
	return -ENODEV;
}
/* <-- LH_SWRD_CL1_Henry@2011.7.25 work around way to make adb connect properly */
void read_vbus_power(u8 *data3v1,  u8* data1v8,  u8* data1v5)
{
twl_i2c_read_u8(TWL4030_MODULE_PM_RECEIVER, data3v1, VUSB3V1_DEV_GRP);
twl_i2c_read_u8(TWL4030_MODULE_PM_RECEIVER, data1v8, VUSB1V8_DEV_GRP);
twl_i2c_read_u8(TWL4030_MODULE_PM_RECEIVER, data1v5, VUSB1V5_DEV_GRP);
}
void write_vbus_power(u8 data3v1, u8 data1v8, u8 data1v5)
{
twl_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, data3v1, VUSB3V1_DEV_GRP);
twl_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, data1v8, VUSB1V8_DEV_GRP);
twl_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, data1v5, VUSB1V5_DEV_GRP);
}
/* LH_SWRD_CL1_Henry@2011.7.25 work around way to make adb connect properly --> */

static ssize_t twl4030_usb_vbus_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct twl4030_usb *twl = dev_get_drvdata(dev);
	unsigned long flags;
	int ret = -EINVAL;

	spin_lock_irqsave(&twl->lock, flags);
	ret = sprintf(buf, "%s\n",
			twl->vbus_supplied ? "on" : "off");
	spin_unlock_irqrestore(&twl->lock, flags);

	return ret;
}
static DEVICE_ATTR(vbus, 0444, twl4030_usb_vbus_show, NULL);

unsigned int previous_status=0;
unsigned long previous_timeout=0;
static irqreturn_t twl4030_usb_irq(int irq, void *_twl)
{
	struct twl4030_usb *twl = _twl;
	int status;

	status = twl4030_usb_linkstat(twl);
/* <-- LH_SWRD_CL1_Henry@2011.9.30 improve USB connection stability (not break down frequently) */
/*
	timeout = previous_timeout + msecs_to_jiffies(400);
	if ((previous_status != status) && (time_before(jiffies, timeout)))
		{
		previous_status = status;
		previous_timeout = jiffies;
		return;		
		}
	else
*/	
		{
		previous_status = status;
		previous_timeout = jiffies;
		}		
	msleep(100);
	status = twl4030_usb_linkstat(twl);
/* LH_SWRD_CL1_Henry@2011.9.30 improve USB storage connection stability (not break down frequently) -->*/
	if (status >= 0) {
		/* FIXME add a set_power() method so that B-devices can
		 * configure the charger appropriately.  It's not always
		 * correct to consume VBUS power, and how much current to
		 * consume is a function of the USB configuration chosen
		 * by the host.
		 *
		 * REVISIT usb_gadget_vbus_connect(...) as needed, ditto
		 * its disconnect() sibling, when changing to/from the
		 * USB_LINK_VBUS state.  musb_hdrc won't care until it
		 * starts to handle softconnect right.
		 */

		if (status == USB_EVENT_NONE) {
			//&*&*&*Beacon_20120508,Add earlysuspend mechanism of usb
			twl4030_phy_suspend(twl, 0);
			if(twl->enter_early_suspend == 1)
			{
				SendPowerbuttonEvent();
				printk("[twl4030-usb]Enter %s, send power suspend event.\n", __FUNCTION__);
			}
			if(wake_lock_active(&usb_lock))
				wake_unlock(&usb_lock);
			
			

			}else{
			
			if(!wake_lock_active(&usb_lock))
				wake_lock(&usb_lock);
			
			twl4030_phy_resume(twl);
			}


		atomic_notifier_call_chain(&twl->otg.notifier, status,
				twl->otg.gadget);
	}
	sysfs_notify(&twl->dev->kobj, NULL, "vbus");

	return IRQ_HANDLED;
}

static void twl4030_usb_phy_init(struct twl4030_usb *twl)
{
	int status;

	status = twl4030_usb_linkstat(twl);
	if (status >= 0) {
		if (status == USB_EVENT_NONE) {
			__twl4030_phy_power(twl, 0);
			twl->asleep = 1;
		} else {
			__twl4030_phy_resume(twl);
			twl->asleep = 0;
		}

		atomic_notifier_call_chain(&twl->otg.notifier, status,
				twl->otg.gadget);
	}
	sysfs_notify(&twl->dev->kobj, NULL, "vbus");
}

static int twl4030_set_suspend(struct otg_transceiver *x, int suspend)
{
	struct twl4030_usb *twl = xceiv_to_twl(x);

	if (suspend)
		twl4030_phy_suspend(twl, 1);
	else
		twl4030_phy_resume(twl);

	return 0;
}

static int twl4030_set_peripheral(struct otg_transceiver *x,
		struct usb_gadget *gadget)
{
	struct twl4030_usb *twl;

	if (!x)
		return -ENODEV;

	twl = xceiv_to_twl(x);
	twl->otg.gadget = gadget;
	if (!gadget)
		twl->otg.state = OTG_STATE_UNDEFINED;

	return 0;
}

static int twl4030_set_host(struct otg_transceiver *x, struct usb_bus *host)
{
	struct twl4030_usb *twl;

	if (!x)
		return -ENODEV;

	twl = xceiv_to_twl(x);
	twl->otg.host = host;
	if (!host)
		twl->otg.state = OTG_STATE_UNDEFINED;

	return 0;
}

//&*&*&*Beacon_20120508,Add earlysuspend mechanism of usb
#ifdef CONFIG_HAS_EARLYSUSPEND
static void twl4030_usb_early_suspend(struct early_suspend *handler)
{
	struct twl4030_usb	*twl;

	twl = container_of(handler, struct twl4030_usb, early_suspend);
	twl->enter_early_suspend = 1;
	printk("[twl4030-usb]Enter %s.\n", __FUNCTION__);
}

static void twl4030_usb_late_resume(struct early_suspend *handler)
{
	struct twl4030_usb	*twl;

	twl = container_of(handler, struct twl4030_usb, early_suspend);
	twl->enter_early_suspend = 0;
	printk("[twl4030-usb]Enter %s.\n", __FUNCTION__);
}
#endif
//&*&*&*Beacon_20120508,Add earlysuspend mechanism of usb

static int __devinit twl4030_usb_probe(struct platform_device *pdev)
{
	struct twl4030_usb_data *pdata = pdev->dev.platform_data;
	struct twl4030_usb	*twl;
	int			status, err;

	if (!pdata) {
		dev_dbg(&pdev->dev, "platform_data not available\n");
		return -EINVAL;
	}

	twl = kzalloc(sizeof *twl, GFP_KERNEL);
	if (!twl)
		return -ENOMEM;

	twl->dev		= &pdev->dev;
	twl->irq		= platform_get_irq(pdev, 0);
	twl->otg.dev		= twl->dev;
	twl->otg.label		= "twl4030";
	twl->otg.set_host	= twl4030_set_host;
	twl->otg.set_peripheral	= twl4030_set_peripheral;
	twl->otg.set_suspend	= twl4030_set_suspend;
	twl->usb_mode		= pdata->usb_mode;
	twl->vbus_supplied	= false;
	twl->asleep = 1;

//&*&*&*Beacon_20120508,Add earlysuspend mechanism of usb
#ifdef CONFIG_HAS_EARLYSUSPEND
	twl->early_suspend.suspend = twl4030_usb_early_suspend;
	twl->early_suspend.resume = twl4030_usb_late_resume;
	register_early_suspend(&twl->early_suspend);
#endif
//&*&*&*Beacon_20120508,Add earlysuspend mechanism of usb

	/* init spinlock for workqueue */
	spin_lock_init(&twl->lock);

	err = twl4030_usb_ldo_init(twl);
	if (err) {
		dev_err(&pdev->dev, "ldo init failed\n");
		kfree(twl);
		return err;
	}
	otg_set_transceiver(&twl->otg);

	platform_set_drvdata(pdev, twl);
	if (device_create_file(&pdev->dev, &dev_attr_vbus))
		dev_warn(&pdev->dev, "could not create sysfs file\n");

	ATOMIC_INIT_NOTIFIER_HEAD(&twl->otg.notifier);

	/* Our job is to use irqs and status from the power module
	 * to keep the transceiver disabled when nothing's connected.
	 *
	 * FIXME we actually shouldn't start enabling it until the
	 * USB controller drivers have said they're ready, by calling
	 * set_host() and/or set_peripheral() ... OTG_capable boards
	 * need both handles, otherwise just one suffices.
	 */
	twl->irq_enabled = true;
	g_twl_usb = twl;//&*&*&*Beacon_20120320
	status = request_threaded_irq(twl->irq, NULL, twl4030_usb_irq,
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
			"twl4030_usb", twl);
	if (status < 0) {
		dev_dbg(&pdev->dev, "can't get IRQ %d, err %d\n",
			twl->irq, status);
		kfree(twl);
		return status;
	}
	//&*&*&*Beacon_20120326
	wake_lock_init(&usb_lock, WAKE_LOCK_SUSPEND, "twl4030_musb_wake_lock");
	/* Power down phy or make it work according to
	 * current link state.
	 */
	twl4030_usb_phy_init(twl);

	dev_info(&pdev->dev, "Initialized TWL4030 USB module\n");
	return 0;
}

static int __exit twl4030_usb_remove(struct platform_device *pdev)
{
	struct twl4030_usb *twl = platform_get_drvdata(pdev);
	int val;

	free_irq(twl->irq, twl);
	device_remove_file(twl->dev, &dev_attr_vbus);

	/* set transceiver mode to power on defaults */
	twl4030_usb_set_mode(twl, -1);

	/* autogate 60MHz ULPI clock,
	 * clear dpll clock request for i2c access,
	 * disable 32KHz
	 */
	val = twl4030_usb_read(twl, PHY_CLK_CTRL);
	if (val >= 0) {
		val |= PHY_CLK_CTRL_CLOCKGATING_EN;
		val &= ~(PHY_CLK_CTRL_CLK32K_EN | REQ_PHY_DPLL_CLK);
		twl4030_usb_write(twl, PHY_CLK_CTRL, (u8)val);
	}

	/* disable complete OTG block */
	twl4030_usb_clear_bits(twl, POWER_CTRL, POWER_CTRL_OTG_ENAB);

	if (!twl->asleep)
		twl4030_phy_power(twl, 0);
	regulator_put(twl->usb1v5);
	regulator_put(twl->usb1v8);
	regulator_put(twl->usb3v1);

	wake_lock_destroy(&usb_lock);

	kfree(twl);

	return 0;
}

static struct platform_driver twl4030_usb_driver = {
	.probe		= twl4030_usb_probe,
	.remove		= __exit_p(twl4030_usb_remove),
	.driver		= {
		.name	= "twl4030_usb",
		.owner	= THIS_MODULE,
	},
};

static int __init twl4030_usb_init(void)
{
	return platform_driver_register(&twl4030_usb_driver);
}
subsys_initcall(twl4030_usb_init);

static void __exit twl4030_usb_exit(void)
{
	platform_driver_unregister(&twl4030_usb_driver);
}
module_exit(twl4030_usb_exit);

MODULE_ALIAS("platform:twl4030_usb");
MODULE_AUTHOR("Texas Instruments, Inc, Nokia Corporation");
MODULE_DESCRIPTION("TWL4030 USB transceiver driver");
MODULE_LICENSE("GPL");
