/*
 * Copyright (C) 2009 Texas Instruments Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/mtd/nand.h>
#include <linux/memblock.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <plat/common.h>
#include <plat/board.h>
#include <plat/gpmc-smc91x.h>
#include <plat/usb.h>

#include <mach/board-zoom.h>

#include "board-flash.h"
#include "mux.h"
//#include "sdram-hynix-h8mbx00u0mer-0em.h"
#if defined(CONFIG_MACH_SDRAM_HYNIX_H8MBX00U0MER0EM_OR_SAMSUNG_K4X4G303PB)
#include "sdram-samsung-k4x4g303pb-or-hynix-h8mbx00u0mer-0em.h"
#elif defined (CONFIG_MACH_SDRAM_SAMSUNG_K4X4G303PB)
#include "sdram-samsung-k4x4g303pb.h"
#elif defined(CONFIG_MACH_SDRAM_HYNIX_H8MBX00U0MER0EM)
#include "sdram-hynix-h8mbx00u0mer-0em.h"
#elif defined(CONFIG_MACH_SDRAM_ELPIDA_EDK4332C2PB)
#include "sdram-elpida-pop-edk4332c2pb.h"
#endif
#include "omap_ion.h"

///////add mike.ma for wifi module
#define CONFIG_WIRELESS_BCM4329_RFKILL  1

#ifdef CONFIG_WIRELESS_BCM4329
#include <linux/fxn-rfkill.h>
#endif

#ifdef CONFIG_WIRELESS_BCM4329_RFKILL
static void config_wlan_mux(void)
{

	/* MMC3 */
	omap_mux_init_signal("etk_clk.sdmmc3_clk", OMAP_PIN_INPUT_PULLUP);
	omap_mux_init_signal("mcspi1_cs1.sdmmc3_cmd", OMAP_PIN_INPUT_PULLUP);
	omap_mux_init_signal("etk_d4.sdmmc3_dat0", OMAP_PIN_INPUT_PULLUP);
	omap_mux_init_signal("etk_d5.sdmmc3_dat1", OMAP_PIN_INPUT_PULLUP);
	omap_mux_init_signal("etk_d6.sdmmc3_dat2", OMAP_PIN_INPUT_PULLUP);
	omap_mux_init_signal("etk_d3.sdmmc3_dat3", OMAP_PIN_INPUT_PULLUP);
}

static struct fxn_rfkill_platform_data fxn_plat_data = {
      .wifi_bt_pwr_en_gpio = FXN_GPIO_WL_BT_POW_EN,      /* WiFi/BT power enable GPIO */
	.wifi_reset_gpio = FXN_GPIO_WL_RST_N,   		/* WiFi reset GPIO */
	.bt_reset_gpio = FXN_GPIO_BT_RST_N,          	/* Bluetooth reset GPIO */
#ifdef CONFIG_RFKILL_GPS
	#ifdef  FXN_GPIO_GPS_PWR_EN
	.gps_pwr_en_gpio = FXN_GPIO_GPS_PWR_EN, 	/* GPS power enable GPIO */
	#endif
	.gps_on_off_gpio = FXN_GPIO_GPS_ON_OFF,		/* GPS on/off GPIO */
	.gps_reset_gpio = FXN_GPIO_GPS_RST,    		/* GPS reset GPIO */	
#endif
};


static struct platform_device conn_device = {
	.name           = "rfkill-fxn",
	.id             = -1,
	.dev.platform_data = &fxn_plat_data,
//	.dev.platform_data = NULL,
};

static struct platform_device *conn_plat_devices[] __initdata = {
    &conn_device,
//    &evt_wifi_device,
};

void __init conn_add_plat_device(void)
{
    printk(">>> conn_add_plat_device()\n");
    platform_add_devices(conn_plat_devices, ARRAY_SIZE(conn_plat_devices));
    // bluetooth_init();
    printk("<<< conn_add_plat_device()\n");
}
#endif

extern void __init evt_peripherals_init(void);
extern void __init evt_lcd_panel_init(void);

#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)

static struct omap_smc91x_platform_data board_smc91x_data = {
	.cs             = 3,
	.flags          = GPMC_MUX_ADD_DATA | IORESOURCE_IRQ_LOWLEVEL,
};

static void __init board_smc91x_init(void)
{
	board_smc91x_data.gpio_irq = 158;
	gpmc_smc91x_init(&board_smc91x_data);
}

#else

static inline void board_smc91x_init(void)
{
}

#endif /* defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE) */

static void enable_board_wakeup_source(void)
{
	/* T2 interrupt line (keypad) */
	omap_mux_init_signal("sys_nirq",
		OMAP_WAKEUP_EN | OMAP_PIN_INPUT_PULLUP);
}

static const struct usbhs_omap_board_data usbhs_bdata __initconst = {

	.port_mode[0] = OMAP_EHCI_PORT_MODE_PHY,
	.port_mode[1] = OMAP_EHCI_PORT_MODE_PHY,
	.port_mode[2] = OMAP_USBHS_PORT_MODE_UNUSED,

	.phy_reset  = true,
	.reset_gpio_port[0]  = 126,
	.reset_gpio_port[1]  = 61,
	.reset_gpio_port[2]  = -EINVAL
};

static struct omap_board_config_kernel sdp_config[] __initdata = {
};

static void __init omap_sdp_init_early(void)
{
	omap2_init_common_infrastructure();
	//omap2_init_common_devices(h8mbx00u0mer0em_sdrc_params,
				 // h8mbx00u0mer0em_sdrc_params);
				  omap2_init_common_devices(edk4332C2pb_sdrc_params,
				  edk4332C2pb_sdrc_params);
}

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#endif




static void __init omap_sdp_init(void)
{
	omap3_mux_init(board_mux, OMAP_PACKAGE_CBP);
	//omap_board_config = sdp_config;
	//omap_board_config_size = ARRAY_SIZE(sdp_config);
	evt_peripherals_init();
      evt_lcd_panel_init();
	config_wlan_mux();

	conn_add_plat_device();

	//zoom_peripherals_init();
	//zoom_display_init();
	//board_smc91x_init();
	//board_flash_init(sdp_flash_partitions, chip_sel_sdp, NAND_BUSWIDTH_16);
	enable_board_wakeup_source();
	//usbhs_init(&usbhs_bdata);
#if CONFIG_ION_OMAP			//Henry Li
	omap_register_ion();	
#endif
}


//OMAP3621_EDP1
	//OMAP_3630SDP
/*Henry Li@20120212 add omap_ion_init */	
static void __init zoom_reserve(void)
{
	/* do the static reservations first */
		memblock_remove(OMAP3_PHYS_ADDR_SMC_MEM, PHYS_ADDR_SMC_SIZE);


#ifdef CONFIG_ION_OMAP
	omap_ion_init();
#endif

	omap_reserve();
}


MACHINE_START(OMAP3621_EDP1, "OMAP 3630SDP board")
	.boot_params	= 0x80000100,
//	.reserve	= omap_reserve,
	.reserve	= zoom_reserve,		//Henry Li
	.map_io		= omap3_map_io,
	.init_early	= omap_sdp_init_early,
	.init_irq	= omap_init_irq,
	.init_machine	= omap_sdp_init,
	.timer		= &omap_timer,
MACHINE_END
