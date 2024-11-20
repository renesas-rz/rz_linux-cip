// SPDX-License-Identifier: GPL-2.0
/*
 * Long Luu <long.luu.ur@renesas.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/string.h>
#include "led_dipsw.h"
#include "utypes.h"
#include "../../rzt2h_esc.h"
#include "../../soes/ecat_slv.h"
#include "../../soes/options.h"

/* param    u8 LED output value. The value one means ON.
 *
 * SET LED
 */
void application_setled(u8 value)
{
	if (value & 1)
		gpiod_set_value(led_dipsw.led0, 1);
	else
		gpiod_set_value(led_dipsw.led0, 0);

	if (value & 2)
		gpiod_set_value(led_dipsw.led1, 1);
	else
		gpiod_set_value(led_dipsw.led1, 0);

	if (value & 4)
		gpiod_set_value(led_dipsw.led2, 1);
	else
		gpiod_set_value(led_dipsw.led2, 0);

	if (value & 8)
		gpiod_set_value(led_dipsw.led3, 1);
	else
		gpiod_set_value(led_dipsw.led3, 0);
}

/* return   u8 DIP SW value. Low input level means ON.
 *
 * Get DIP SW
 */
u8 application_getdipsw(void)
{
	u8 u8DipSw;

	u8DipSw = 0;

	if (gpiod_get_value(led_dipsw.dipsw0))
		u8DipSw |= 0x01;

	if (gpiod_get_value(led_dipsw.dipsw1))
		u8DipSw |= 0x02;

	if (gpiod_get_value(led_dipsw.dipsw2))
		u8DipSw |= 0x04;

	if (gpiod_get_value(led_dipsw.dipsw3))
		u8DipSw |= 0x08;

	return u8DipSw;
}

/**
 * This function reads physical input values and assigns the corresponding members
 * of Obj.Buttons
 */
void cb_get_inputs(void)
{
	volatile u8 io_input;

	io_input = application_getdipsw();
	Obj.BUTTON = io_input;
}

/**
 * This function writes physical output values from the corresponding members of
 * Obj.LEDs
 */
void cb_set_outputs(void)
{
	volatile u8 io_output;

	io_output = Obj.LED;
	application_setled(io_output);
}

void cb_state_change(u8 *as, u8 *an)
{
	if (*as == SAFEOP_TO_OP) {
		/* Enable watchdog interrupt */
		ESC_ALeventmaskwrite(ESC_ALeventmaskread() | ESCREG_ALEVENT_WD);
	}
}

/* Called from stack when stopping outputs */
void user_safeoutput(void)
{
	memset(&Obj.LED, 0, (sizeof(Obj.LED)));
}

/* SYNC0 ISR handler */
void Sync0_Isr(void)
{
}

/* SYNC1 ISR handler */
void Sync1_Isr(void)
{
}

/* PDI ISR handler */
void PDI_Isr(void)
{
	u32 alevent;
	u16 wd;

	ESC_read(ESCREG_ALEVENT, &alevent, sizeof(alevent));
	CC_ATOMIC_SET(ESCvar.ALevent, etohs(alevent));

	if (ESCvar.ALevent & (ESCREG_ALEVENT_SM2 | ESCREG_ALEVENT_SM3)) {
		DIG_process(DIG_PROCESS_OUTPUTS_FLAG |
			    DIG_PROCESS_APP_HOOK_FLAG |
			    DIG_PROCESS_INPUTS_FLAG);
	}

	/* SM watchdog */
	if (ESCvar.ALevent & ESCREG_ALEVENT_WD) {
		/* Ack the WD IRQ */
		ESC_read(ESCREG_WDSTATUS, &wd, sizeof(wd));
		/* Check if the WD have expired and if we're in OP */
		if (((wd & 0x1) == 0) && ((CC_ATOMIC_GET(ESCvar.App.state) & APPSTATE_OUTPUT) > 0)) {
			ESC_ALstatusgotoerror((ESCsafeop | ESCerror), ALERR_WATCHDOG);
			ESC_ALeventmaskwrite(ESC_ALeventmaskread() & ~ESCREG_ALEVENT_WD);
		}
	}
}

/* Configuration parameters for SOES
 * SM and Mailbox parameters comes from the
 * generated config.h
 */
static struct esc_cfg_t config = {
	.user_arg = NULL,
	.use_interrupt = 1,
	.watchdog_cnt = S32_MAX, /* Use HW SM watchdog instead */
	.set_defaults_hook = NULL,
	.pre_state_change_hook = NULL,
	.post_state_change_hook = cb_state_change,
	.application_hook = NULL,
	.safeoutput_override = user_safeoutput,
	.pre_object_download_hook = NULL,
	.post_object_download_hook = NULL,
	.rxpdo_override = NULL,
	.txpdo_override = NULL,
	.esc_hw_interrupt_enable = ESC_interrupt_enable,
	.esc_hw_interrupt_disable = ESC_interrupt_disable,
	.esc_hw_eep_handler = NULL,
	.esc_check_dc_handler = NULL
};

int application_init(struct esc *esc)
{
	struct device_node *led_dipsw_node;

	ecat_slv_init(&config);

	led_dipsw_node = of_get_child_by_name(dev_of_node(esc->dev),
					      "led_dipsw_device");
	if (!led_dipsw_node) {
		dev_err(esc->dev, "Can not find led_dipsw_device node\n");
		return -ENODEV;
	}

	led_dipsw.led0 = devm_gpiod_get_from_of_node(esc->dev, led_dipsw_node,
						     "led0-gpios", 0,
						     GPIOD_OUT_HIGH, "led0");
	if (IS_ERR(led_dipsw.led0)) {
		dev_err(esc->dev, "Failed to get led_dipsw_device led0\n");
		return PTR_ERR(led_dipsw.led0);
	}

	led_dipsw.led1 = devm_gpiod_get_from_of_node(esc->dev, led_dipsw_node,
						     "led1-gpios", 0,
						     GPIOD_OUT_HIGH, "led1");
	if (IS_ERR(led_dipsw.led1)) {
		dev_err(esc->dev, "Failed to get led_dipsw_device led1\n");
		return PTR_ERR(led_dipsw.led1);
	}

	led_dipsw.led2 = devm_gpiod_get_from_of_node(esc->dev, led_dipsw_node,
						     "led2-gpios", 0,
						     GPIOD_OUT_HIGH, "led2");
	if (IS_ERR(led_dipsw.led2)) {
		dev_err(esc->dev, "Failed to get led_dipsw_device led2\n");
		return PTR_ERR(led_dipsw.led2);
	}

	led_dipsw.led3 = devm_gpiod_get_from_of_node(esc->dev, led_dipsw_node,
						     "led3-gpios", 0,
						     GPIOD_OUT_HIGH, "led3");
	if (IS_ERR(led_dipsw.led3)) {
		dev_err(esc->dev, "Failed to get led_dipsw_device led3\n");
		return PTR_ERR(led_dipsw.led3);
	}

	led_dipsw.dipsw0 = devm_gpiod_get_from_of_node(esc->dev, led_dipsw_node,
						       "dipsw0-gpios", 0,
						       GPIOD_IN, "dipsw0");
	if (IS_ERR(led_dipsw.dipsw0)) {
		dev_err(esc->dev, "Failed to get led_dipsw_device dipsw0\n");
		return PTR_ERR(led_dipsw.dipsw0);
	}

	led_dipsw.dipsw1 = devm_gpiod_get_from_of_node(esc->dev, led_dipsw_node,
						       "dipsw1-gpios", 0,
						       GPIOD_IN, "dipsw1");
	if (IS_ERR(led_dipsw.dipsw1)) {
		dev_err(esc->dev, "Failed to get led_dipsw_device dipsw1\n");
		return PTR_ERR(led_dipsw.dipsw1);
	}

	led_dipsw.dipsw2 = devm_gpiod_get_from_of_node(esc->dev, led_dipsw_node,
						       "dipsw2-gpios", 0,
						       GPIOD_IN, "dipsw2");
	if (IS_ERR(led_dipsw.dipsw2)) {
		dev_err(esc->dev, "Failed to get led_dipsw_device dipsw2\n");
		return PTR_ERR(led_dipsw.dipsw2);
	}

	led_dipsw.dipsw3 = devm_gpiod_get_from_of_node(esc->dev, led_dipsw_node,
						       "dipsw3-gpios", 0,
						       GPIOD_IN, "dipsw3");
	if (IS_ERR(led_dipsw.dipsw3)) {
		dev_err(esc->dev, "Failed to get led_dipsw_device dipsw3\n");
		return PTR_ERR(led_dipsw.dipsw3);
	}

	return 0;
}

void application_loop(void)
{
	ecat_slv_poll();
}
