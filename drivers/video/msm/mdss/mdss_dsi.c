/* Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/leds-qpnp-wled.h>
#include <linux/clk.h>
/* optimize the screen wake up time*/
#include <linux/pm_qos.h>
#include "mdss.h"
#include "mdss_panel.h"
#include "mdss_dsi.h"
#include "mdss_debug.h"
/* a requirement about the production line test the leaky current of LCD  */
/* PT test control LDO power alone for LCD  */
#ifdef CONFIG_HUAWEI_LCD
bool enable_PT_test = 0;
module_param_named(enable_PT_test, enable_PT_test, bool, S_IRUGO | S_IWUSR);
bool enable_LDO_test = 0;
module_param_named(enable_LDO_test, enable_LDO_test, bool, S_IRUGO | S_IWUSR);
#endif
#include <linux/hw_lcd_common.h>
#define XO_CLK_RATE	19200000
#define REGULATOR_DCDC_MODE 0
#define REGULATOR_LDO_MODE  1
static int g_mipi_regulator_mode = REGULATOR_DCDC_MODE;
static int mdss_dsi_pinctrl_set_state(struct mdss_dsi_ctrl_pdata *ctrl_pdata,
					bool active);
/*delete cpuget() to avoid panic*/
/* optimize the screen wake up time*/
#define DSI_DISABLE_PC_LATENCY 100
#define DSI_ENABLE_PC_LATENCY PM_QOS_DEFAULT_VALUE

static struct pm_qos_request mdss_dsi_pm_qos_request;

static void mdss_dsi_pm_qos_add_request(void)
{
	pr_debug("%s: add request",__func__);
	pm_qos_add_request(&mdss_dsi_pm_qos_request, PM_QOS_CPU_DMA_LATENCY,
			PM_QOS_DEFAULT_VALUE);
}

static void mdss_dsi_pm_qos_remove_request(void)
{
	pr_debug("%s: remove request",__func__);
	pm_qos_remove_request(&mdss_dsi_pm_qos_request);
}

static void mdss_dsi_pm_qos_update_request(int val)
{
	pr_debug("%s: update request %d",__func__,val);
	pm_qos_update_request(&mdss_dsi_pm_qos_request, val);
}
static int mdss_dsi_regulator_init(struct platform_device *pdev)
{
	int rc = 0;

	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	int i = 0;
	int j = 0;

	if (!pdev) {
		pr_err("%s: invalid input\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = platform_get_drvdata(pdev);
	if (!ctrl_pdata) {
		pr_err("%s: invalid driver data\n", __func__);
		return -EINVAL;
	}

	for (i = 0; !rc && (i < DSI_MAX_PM); i++) {
		rc = msm_dss_config_vreg(&pdev->dev,
			ctrl_pdata->power_data[i].vreg_config,
			ctrl_pdata->power_data[i].num_vreg, 1);
		if (rc) {
			pr_err("%s: failed to init vregs for %s\n",
				__func__, __mdss_dsi_pm_name(i));
			for (j = i-1; j >= 0; j--) {
				msm_dss_config_vreg(&pdev->dev,
				ctrl_pdata->power_data[j].vreg_config,
				ctrl_pdata->power_data[j].num_vreg, 0);
			}
		}
	}
	return rc;
}

/*T2 10 LCD on*/
/*Delete useless GPIO,change power on/off sequence*/
static int hw_request_gpios(struct mdss_dsi_ctrl_pdata* ctrl_pdata)
{
	int ret = 0;

	if (1 == ctrl_pdata->which_product_pad){
		if (gpio_is_valid(ctrl_pdata->disp_vsn_gpio)){
			ret = gpio_request(ctrl_pdata->disp_vsn_gpio, "disp_vsn_gpio");
			if (ret){
				pr_err("request disp_vsn_gpio gpio failed,ret=%d\n", ret);
				goto disp_vsn_gpio_request_err;
			}
		}

		if (gpio_is_valid(ctrl_pdata->disp_vsp_gpio)){
			ret = gpio_request(ctrl_pdata->disp_vsp_gpio, "disp_vsp_gpio");
			if (ret){
				pr_err("request disp_vsp_gpio gpio failed,ret=%d\n", ret);
				goto disp_vsp_gpio_request_err;
			}
		}

		if (gpio_is_valid(ctrl_pdata->disp_vled_gpio)){
			ret = gpio_request(ctrl_pdata->disp_vled_gpio, "disp_vled_gpio");
			if (ret){
				pr_err("request disp_vled_gpio gpio failed,ret=%d\n", ret);
				goto disp_vled_gpio_request_err;
			}
		}

		if (gpio_is_valid(ctrl_pdata->disp_bl_gpio)){
			ret = gpio_request(ctrl_pdata->disp_bl_gpio, "disp_bl_gpio");
			if (ret){
				pr_err("request disp_bl_gpio gpio failed,ret=%d\n", ret);
				goto disp_bl_gpio_request_err;
			}
		}

		if (gpio_is_valid(ctrl_pdata->disp_vcc_gpio)){
			ret = gpio_request(ctrl_pdata->disp_vcc_gpio, "disp_vcc_gpio");
			if (ret){
				pr_err("request disp_vcc_gpio gpio failed,ret=%d\n", ret);
				goto disp_vcc_gpio_request_err;
			}
		}
	} else if (2 == ctrl_pdata->which_product_pad) {
		if (gpio_is_valid(ctrl_pdata->disp_vled_gpio)){
			ret = gpio_request(ctrl_pdata->disp_vled_gpio, "disp_vled_gpio");
			if (ret){
				pr_err("request disp_vled_gpio gpio failed,ret=%d\n", ret);
				goto disp_vled_gpio_request_err;
			}
		}

		if (gpio_is_valid(ctrl_pdata->disp_bl_gpio)){
			ret = gpio_request(ctrl_pdata->disp_bl_gpio, "disp_bl_gpio");
			if (ret){
				pr_err("request disp_bl_gpio gpio failed,ret=%d\n", ret);
				goto disp_bl_gpio_request_err;
			}
		}

		if (gpio_is_valid(ctrl_pdata->disp_vcc_gpio)){
			ret = gpio_request(ctrl_pdata->disp_vcc_gpio, "disp_vcc_gpio");
			if (ret){
				pr_err("request disp_vcc_gpio gpio failed,ret=%d\n", ret);
				goto disp_vcc_gpio_request_err;
			}
		}
	} else{
		if (gpio_is_valid(ctrl_pdata->disp_bl_gpio)){
			ret = gpio_request(ctrl_pdata->disp_bl_gpio, "disp_power_backlight");
			if (ret){
				pr_err("request disp_power_backlight gpio failed,ret=%d\n", ret);
				goto disp_power_backlight_gpio_err;
			}
		}

		if (gpio_is_valid(ctrl_pdata->disp_vcc_gpio)){
			ret = gpio_request(ctrl_pdata->disp_vcc_gpio, "disp_power_panel");
			if (ret){
				pr_err("request disp_power_panel gpio failed,ret=%d\n", ret);
				goto disp_power_panel_gpio_err;
			}
		}

		if (gpio_is_valid(ctrl_pdata->disp_vled_gpio)){
			ret = gpio_request(ctrl_pdata->disp_vled_gpio, "disp_en_gpio_vled");
			if (ret){
				pr_err("request disp_en_gpio_vled gpio failed,ret=%d\n", ret);
				goto disp_en_gpio_vled_gpio_request_err;
			}
		}
	}

	return ret;
disp_vcc_gpio_request_err:
	if (gpio_is_valid(ctrl_pdata->disp_vcc_gpio)){
		gpio_free(ctrl_pdata->disp_vcc_gpio);
	}
disp_bl_gpio_request_err:
	if (gpio_is_valid(ctrl_pdata->disp_bl_gpio)){
		gpio_free(ctrl_pdata->disp_bl_gpio);
	}

disp_vled_gpio_request_err:
	if (gpio_is_valid(ctrl_pdata->disp_vled_gpio)){
		gpio_free(ctrl_pdata->disp_vled_gpio);
	}


disp_vsp_gpio_request_err:
	if (gpio_is_valid(ctrl_pdata->disp_vsp_gpio)){
		gpio_free(ctrl_pdata->disp_vsp_gpio);
	}

disp_vsn_gpio_request_err:
	if (gpio_is_valid(ctrl_pdata->disp_vsn_gpio)){
		gpio_free(ctrl_pdata->disp_vsn_gpio);
	}

	return ret;

disp_en_gpio_vled_gpio_request_err:
	if (gpio_is_valid(ctrl_pdata->disp_vcc_gpio)){
		gpio_free(ctrl_pdata->disp_vcc_gpio);
	}
disp_power_panel_gpio_err:
	if (gpio_is_valid(ctrl_pdata->disp_bl_gpio)){
		gpio_free(ctrl_pdata->disp_bl_gpio);
	}
disp_power_backlight_gpio_err:
	return ret;
}

static void hw_panel_power_en(struct mdss_panel_data* pdata, int enable)
{
	struct mdss_dsi_ctrl_pdata* ctrl_pdata = NULL;
	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata, panel_data);

	if (enable){
		if (1 == ctrl_pdata->which_product_pad){
			if (gpio_is_valid(ctrl_pdata->disp_vcc_gpio)){
				gpio_set_value((ctrl_pdata->disp_vcc_gpio), enable);
				pr_info("%s,%d 111set disp_vcc_gpio = %d \n", __func__, __LINE__, enable);
			}
			mdelay(1);

			if (gpio_is_valid(ctrl_pdata->disp_bl_gpio)){
				gpio_set_value((ctrl_pdata->disp_bl_gpio), enable);
				pr_info("%s,%d 111set disp_bl_gpio = %d \n", __func__, __LINE__, enable);
		       }
		    mdelay(5);

			if (gpio_is_valid(ctrl_pdata->disp_vsp_gpio)){
				gpio_set_value((ctrl_pdata->disp_vsp_gpio), enable);
				pr_info("%s,%d set disp_vsp_gpio = %d \n", __func__, __LINE__, enable);
			}

			mdelay(5);
			if (gpio_is_valid(ctrl_pdata->disp_vsn_gpio)){
				gpio_set_value((ctrl_pdata->disp_vsn_gpio), enable);
				pr_info("%s,%d set disp_vsn_gpio = %d \n", __func__, __LINE__, enable);
			}

			mdelay(5);

	        if (gpio_is_valid(ctrl_pdata->disp_vled_gpio)){
				gpio_set_value((ctrl_pdata->disp_vled_gpio), enable);
				pr_info("%s,%d set disp_vled_gpio = %d \n", __func__, __LINE__, enable);
	        }
	        mdelay(5);
		} else if (2 == ctrl_pdata->which_product_pad) {
			/*delete*/
			if (gpio_is_valid(ctrl_pdata->disp_vcc_gpio)){
				gpio_set_value((ctrl_pdata->disp_vcc_gpio), enable);
				pr_info("%s,%d set disp_vcc_gpio = %d \n", __func__, __LINE__, enable);
			}
			mdelay(1);
			if (gpio_is_valid(ctrl_pdata->disp_bl_gpio)){
				gpio_set_value((ctrl_pdata->disp_bl_gpio), enable);
				pr_info("%s,%d set disp_bl_gpio = %d \n", __func__, __LINE__, enable);
		       }
			mdelay(30);
		} else {
			if (!strcmp(ctrl_pdata->panel_data.panel_info.panel_name,"CMI_NT51021_10_1200P_VIDEO"))
			{
				if (gpio_is_valid(ctrl_pdata->disp_vcc_gpio)){
					gpio_set_value((ctrl_pdata->disp_vcc_gpio), enable);
					pr_info("%s,%d set disp_power_panel = %d \n", __func__, __LINE__, enable);
				}
				if (gpio_is_valid(ctrl_pdata->disp_bl_gpio)){
					gpio_set_value((ctrl_pdata->disp_bl_gpio), enable);
					pr_info("%s,%d set disp_power_backlight = %d \n", __func__, __LINE__, enable);
				}
				mdelay(50);
				}
			}
	} else {
		if (1 == ctrl_pdata->which_product_pad){
			if (gpio_is_valid(ctrl_pdata->disp_vled_gpio)){
				gpio_set_value((ctrl_pdata->disp_vled_gpio), enable);
				pr_info("%s,%d 100set disp_vled_gpio = %d \n", __func__, __LINE__, enable);
			}
			mdelay(5);

			if (gpio_is_valid(ctrl_pdata->disp_vsn_gpio)){
				gpio_set_value((ctrl_pdata->disp_vsn_gpio), enable);
				pr_info("%s,%d set disp_vsn_gpio = %d \n", __func__, __LINE__, enable);
			}
			mdelay(5);

			if (gpio_is_valid(ctrl_pdata->disp_vsp_gpio)){
				gpio_set_value((ctrl_pdata->disp_vsp_gpio), enable);
				pr_info("%s,%d set disp_vsp_gpio = %d \n", __func__, __LINE__, enable);
			}
			mdelay(5);

			if (gpio_is_valid(ctrl_pdata->disp_bl_gpio)){
				gpio_set_value((ctrl_pdata->disp_bl_gpio), enable);
				pr_info("%s,%d set disp_bl_gpio = %d \n", __func__, __LINE__, enable);
			}
			mdelay(5);

			if (gpio_is_valid(ctrl_pdata->disp_vcc_gpio)){
				gpio_set_value((ctrl_pdata->disp_vcc_gpio), enable);
				pr_info("%s,%d set disp_vcc_gpio = %d \n", __func__, __LINE__, enable);
			}
			mdelay(1);

		} else if (2 == ctrl_pdata->which_product_pad) {
			/*delete*/
			if (gpio_is_valid(ctrl_pdata->disp_bl_gpio)){
				gpio_set_value((ctrl_pdata->disp_bl_gpio), enable);
				pr_info("%s,%d set disp_bl_gpio = %d \n", __func__, __LINE__, enable);
			}
			if (gpio_is_valid(ctrl_pdata->disp_vcc_gpio)){
				gpio_set_value((ctrl_pdata->disp_vcc_gpio), enable);
				pr_info("%s,%d set disp_vcc_gpio = %d \n", __func__, __LINE__, enable);
			}
			mdelay(500);
		} else {
			if (gpio_is_valid(ctrl_pdata->disp_bl_gpio)){
				gpio_set_value((ctrl_pdata->disp_bl_gpio), enable);
				pr_info("%s,%d set disp_power_backlight = %d \n", __func__, __LINE__, enable);
			}

			if (gpio_is_valid(ctrl_pdata->disp_vcc_gpio)){
				gpio_set_value((ctrl_pdata->disp_vcc_gpio), enable);
				pr_info("%s,%d set disp_power_panel = %d \n", __func__, __LINE__, enable);
			}
			mdelay(500);
		}
	}
}

static int mdss_dsi_panel_power_off(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	int i = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		ret = -EINVAL;
		goto end;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	ret = mdss_dsi_panel_reset(pdata, 0);
	if (ret) {
		pr_warn("%s: Panel reset failed. rc=%d\n", __func__, ret);
		ret = 0;
	}

	if (mdss_dsi_pinctrl_set_state(ctrl_pdata, false))
		pr_debug("reset disable: pinctrl not enabled\n");

	if (ctrl_pdata->panel_bias_vreg) {
		pr_debug("%s: Disabling panel bias vreg. ndx = %d\n",
		       __func__, ctrl_pdata->ndx);
		if (qpnp_ibb_enable(false))
			pr_err("Unable to disable bias vreg\n");
		/* Add delay recommended by panel specs */
		udelay(2000);
	}

	for (i = DSI_MAX_PM - 1; i >= 0; i--) {
		/*
		 * Core power module will be disabled when the
		 * clocks are disabled
		 */
		if (DSI_CORE_PM == i)
			continue;
		ret = msm_dss_enable_vreg(
			ctrl_pdata->power_data[i].vreg_config,
			ctrl_pdata->power_data[i].num_vreg, 0);
		if (ret)
			pr_err("%s: failed to disable vregs for %s\n",
				__func__, __mdss_dsi_pm_name(i));
	}

end:
	return ret;
}

/*T2 10 LCD on*/
static int mdss_dsi_panel_power_off_pad(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		ret = -EINVAL;
		goto end;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	ret = mdss_dsi_panel_reset(pdata, 0);
	if (ret) {
		pr_warn("%s: Panel reset failed. rc=%d\n", __func__, ret);
		ret = 0;
	}
#ifndef CONFIG_HUAWEI_LCD
	if (mdss_dsi_pinctrl_set_state(ctrl_pdata, false))
		pr_debug("reset disable: pinctrl not enabled\n");
#endif

	{
		hw_panel_power_en(pdata, 0);
		ret = msm_dss_enable_vreg(
			ctrl_pdata->power_data[DSI_PANEL_PM].vreg_config,
			ctrl_pdata->power_data[DSI_PANEL_PM].num_vreg, 0);
		if (ret)
			pr_err("%s: failed to disable vregs for %s\n",
				__func__, __mdss_dsi_pm_name(DSI_PANEL_PM));
		mdelay(2);
	}
end:
	return ret;
}

static int mdss_dsi_panel_power_on(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	int i = 0;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	for (i = 0; i < DSI_MAX_PM; i++) {
		/*
		 * Core power module will be enabled when the
		 * clocks are enabled
		 */
		if (DSI_CORE_PM == i)
			continue;
		ret = msm_dss_enable_vreg(
			ctrl_pdata->power_data[i].vreg_config,
			ctrl_pdata->power_data[i].num_vreg, 1);
		if (ret) {
			pr_err("%s: failed to enable vregs for %s\n",
				__func__, __mdss_dsi_pm_name(i));
			goto error;
		}
	}
	if (ctrl_pdata->panel_bias_vreg) {
		pr_debug("%s: Enable panel bias vreg. ndx = %d\n",
		       __func__, ctrl_pdata->ndx);
		if (qpnp_ibb_enable(true))
			pr_err("Unable to configure bias vreg\n");
		/* Add delay recommended by panel specs */
		udelay(2000);
	}

	i--;

	/*
	 * If continuous splash screen feature is enabled, then we need to
	 * request all the GPIOs that have already been configured in the
	 * bootloader. This needs to be done irresepective of whether
	 * the lp11_init flag is set or not.
	 */
	if (pdata->panel_info.cont_splash_enabled ||
		!pdata->panel_info.mipi.lp11_init) {
		if (mdss_dsi_pinctrl_set_state(ctrl_pdata, true))
			pr_debug("reset enable: pinctrl not enabled\n");

		ret = mdss_dsi_panel_reset(pdata, 1);
		if (ret)
			pr_err("%s: Panel reset failed. rc=%d\n",
					__func__, ret);
	}

error:
	if (ret) {
		for (; i >= 0; i--)
			msm_dss_enable_vreg(
				ctrl_pdata->power_data[i].vreg_config,
				ctrl_pdata->power_data[i].num_vreg, 0);
	}
	return ret;
}


/*T2 10 LCD on*/
static int mdss_dsi_panel_power_on_pad(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	/*delete*/
	hw_panel_power_en(pdata, 1);

	if (pdata->panel_info.cont_splash_enabled ||
		!pdata->panel_info.mipi.lp11_init) {
		if (mdss_dsi_pinctrl_set_state(ctrl_pdata, true))
			pr_debug("reset enable: pinctrl not enabled\n");

		ret = mdss_dsi_panel_reset(pdata, 1);
		if (ret)
			pr_err("%s: Panel reset failed. rc=%d\n",
					__func__, ret);
	}
	return ret;
	/*delete*/
}

static int mdss_dsi_panel_power_lp(struct mdss_panel_data *pdata, int enable)
{
	/* Panel power control when entering/exiting lp mode */
	return 0;
}

static int mdss_dsi_panel_power_ctrl(struct mdss_panel_data *pdata,
	int power_state)
{
/*open black screen gesture function,can't wake up screen*/
	int ret;
	struct mdss_panel_info *pinfo;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	/* Modify JDI tp/lcd power on/off to reduce power consumption */
	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pinfo = &pdata->panel_info;
	pr_debug("%s: cur_power_state=%d req_power_state=%d\n", __func__,
		pinfo->panel_power_state, power_state);

	if (pinfo->panel_power_state == power_state) {
		pr_debug("%s: no change needed\n", __func__);
		return 0;
	}

	/*
	 * If a dynamic mode switch is pending, the regulators should not
	 * be turned off or on.
	 */
	if (pdata->panel_info.dynamic_switch_pending)
		return 0;

	switch (power_state) {
	case MDSS_PANEL_POWER_OFF:
/*open black screen gesture function,can't wake up screen*/
/*T2 10 LCD on*/
			if (ctrl_pdata->hw_product_pad) {
				ret = mdss_dsi_panel_power_off_pad(pdata);
			} else {
				ret = mdss_dsi_panel_power_off(pdata);
			}
		break;
	case MDSS_PANEL_POWER_ON:
			/* Modify JDI tp/lcd power on/off to reduce power consumption */
			if(pinfo->lens_type == LENS_INCELL)
			{
				gpio_set_value(ctrl_pdata->tp_vci_gpio,1);
				udelay(10);
			}
			if (ctrl_pdata->hw_product_pad) {
				ret = mdss_dsi_panel_power_on_pad(pdata);
			} else {
				if (mdss_dsi_is_panel_on_lp(pdata))
					ret = mdss_dsi_panel_power_lp(pdata, false);
				else
					ret = mdss_dsi_panel_power_on(pdata);
			}
		break;
	case MDSS_PANEL_POWER_LP1:
	case MDSS_PANEL_POWER_LP2:
		ret = mdss_dsi_panel_power_lp(pdata, true);
		break;
	default:
		pr_err("%s: unknown panel power state requested (%d)\n",
			__func__, power_state);
		ret = -EINVAL;
	}

	if (!ret)
		pinfo->panel_power_state = power_state;

	return ret;
}

static void mdss_dsi_put_dt_vreg_data(struct device *dev,
	struct dss_module_power *module_power)
{
	if (!module_power) {
		pr_err("%s: invalid input\n", __func__);
		return;
	}

	if (module_power->vreg_config) {
		devm_kfree(dev, module_power->vreg_config);
		module_power->vreg_config = NULL;
	}
	module_power->num_vreg = 0;
}

static int mdss_dsi_get_dt_vreg_data(struct device *dev,
	struct dss_module_power *mp, enum dsi_pm_type module)
{
	int i = 0, rc = 0, ret = 0;
	u32 tmp = 0;
	struct device_node *of_node = NULL, *supply_node = NULL;
	const char *pm_supply_name = NULL;
	struct device_node *supply_root_node = NULL;

	if (!dev || !mp) {
		pr_err("%s: invalid input\n", __func__);
		rc = -EINVAL;
		return rc;
	}

	of_node = dev->of_node;

	mp->num_vreg = 0;
	pm_supply_name = __mdss_dsi_pm_supply_node_name(module);
	supply_root_node = of_get_child_by_name(of_node, pm_supply_name);
	if (!supply_root_node) {
		pr_err("no supply entry present\n");
		goto novreg;
	}

/*T2 10 LCD on*/
	for_each_child_of_node(supply_root_node, supply_node) {
		const char *supply_name = NULL;
		ret = of_property_read_string(supply_node,
			"qcom,supply-name", &supply_name);
		if (!ret) {/*vreg supply-name not null*/
			mp->num_vreg++;
		}
	}

	if (mp->num_vreg == 0) {
		pr_debug("%s: no vreg\n", __func__);
		goto novreg;
	} else {
		pr_debug("%s: vreg found. count=%d\n", __func__, mp->num_vreg);
	}

	mp->vreg_config = devm_kzalloc(dev, sizeof(struct dss_vreg) *
		mp->num_vreg, GFP_KERNEL);
	if (!mp->vreg_config) {
		pr_err("%s: can't alloc vreg mem\n", __func__);
		rc = -ENOMEM;
		goto error;
	}

	for_each_child_of_node(supply_root_node, supply_node) {
		const char *st = NULL;
		/* vreg-name */
		rc = of_property_read_string(supply_node,
			"qcom,supply-name", &st);
		if (rc) {
			pr_err("%s: error reading name. rc=%d\n",
				__func__, rc);
			//goto error;
			rc = 0;/*if name is null we not return and find next one*/
			break;
		}
		snprintf(mp->vreg_config[i].vreg_name,
			ARRAY_SIZE((mp->vreg_config[i].vreg_name)), "%s", st);
		/* vreg-min-voltage */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-min-voltage", &tmp);
		if (rc) {
			pr_err("%s: error reading min volt. rc=%d\n",
				__func__, rc);
			goto error;
		}
		mp->vreg_config[i].min_voltage = tmp;

		/* vreg-max-voltage */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-max-voltage", &tmp);
		if (rc) {
			pr_err("%s: error reading max volt. rc=%d\n",
				__func__, rc);
			goto error;
		}
		mp->vreg_config[i].max_voltage = tmp;

		/* enable-load */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-enable-load", &tmp);
		if (rc) {
			pr_err("%s: error reading enable load. rc=%d\n",
				__func__, rc);
			goto error;
		}
		mp->vreg_config[i].enable_load = tmp;

		/* disable-load */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-disable-load", &tmp);
		if (rc) {
			pr_err("%s: error reading disable load. rc=%d\n",
				__func__, rc);
			goto error;
		}
		mp->vreg_config[i].disable_load = tmp;

		/* pre-sleep */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-pre-on-sleep", &tmp);
		if (rc) {
			pr_debug("%s: error reading supply pre sleep value. rc=%d\n",
				__func__, rc);
			rc = 0;
		} else {
			mp->vreg_config[i].pre_on_sleep = tmp;
		}

		rc = of_property_read_u32(supply_node,
			"qcom,supply-pre-off-sleep", &tmp);
		if (rc) {
			pr_debug("%s: error reading supply pre sleep value. rc=%d\n",
				__func__, rc);
			rc = 0;
		} else {
			mp->vreg_config[i].pre_off_sleep = tmp;
		}

		/* post-sleep */
		rc = of_property_read_u32(supply_node,
			"qcom,supply-post-on-sleep", &tmp);
		if (rc) {
			pr_debug("%s: error reading supply post sleep value. rc=%d\n",
				__func__, rc);
			rc = 0;
		} else {
			mp->vreg_config[i].post_on_sleep = tmp;
		}

		rc = of_property_read_u32(supply_node,
			"qcom,supply-post-off-sleep", &tmp);
		if (rc) {
			pr_debug("%s: error reading supply post sleep value. rc=%d\n",
				__func__, rc);
			rc = 0;
		} else {
			mp->vreg_config[i].post_off_sleep = tmp;
		}

		pr_debug("%s: %s min=%d, max=%d, enable=%d, disable=%d, preonsleep=%d, postonsleep=%d, preoffsleep=%d, postoffsleep=%d\n",
			__func__,
			mp->vreg_config[i].vreg_name,
			mp->vreg_config[i].min_voltage,
			mp->vreg_config[i].max_voltage,
			mp->vreg_config[i].enable_load,
			mp->vreg_config[i].disable_load,
			mp->vreg_config[i].pre_on_sleep,
			mp->vreg_config[i].post_on_sleep,
			mp->vreg_config[i].pre_off_sleep,
			mp->vreg_config[i].post_off_sleep
			);
		++i;
	}

	return rc;

error:
	if (mp->vreg_config) {
		devm_kfree(dev, mp->vreg_config);
		mp->vreg_config = NULL;
	}
novreg:
	mp->num_vreg = 0;

	return rc;
}

static int mdss_dsi_get_panel_cfg(char *panel_cfg,
				struct mdss_dsi_ctrl_pdata *ctrl)
{
	int rc;
	struct mdss_panel_cfg *pan_cfg = NULL;

	if (!panel_cfg)
		return MDSS_PANEL_INTF_INVALID;

	pan_cfg = ctrl->mdss_util->panel_intf_type(MDSS_PANEL_INTF_DSI);
	if (IS_ERR(pan_cfg)) {
		return PTR_ERR(pan_cfg);
	} else if (!pan_cfg) {
		panel_cfg[0] = 0;
		return 0;
	}

	pr_debug("%s:%d: cfg:[%s]\n", __func__, __LINE__,
		 pan_cfg->arg_cfg);
	ctrl->panel_data.panel_info.is_prim_panel = true;
	rc = strlcpy(panel_cfg, pan_cfg->arg_cfg,
		     sizeof(pan_cfg->arg_cfg));
	return rc;
}

static int mdss_dsi_off(struct mdss_panel_data *pdata, int power_state)
{
	int ret = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_panel_info *panel_info = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	panel_info = &ctrl_pdata->panel_data.panel_info;

	pr_debug("%s+: ctrl=%pK ndx=%d power_state=%d\n",
		__func__, ctrl_pdata, ctrl_pdata->ndx, power_state);

	if (power_state == panel_info->panel_power_state) {
		pr_debug("%s: No change in power state %d -> %d\n", __func__,
			panel_info->panel_power_state, power_state);
		goto end;
	}

	if (mdss_panel_is_power_on(power_state)) {
		pr_debug("%s: dsi_off with panel always on\n", __func__);
		goto panel_power_ctrl;
	}

	if (pdata->panel_info.type == MIPI_CMD_PANEL)
		mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 1);

	if (!pdata->panel_info.ulps_suspend_enabled) {
		/* disable DSI controller */
		mdss_dsi_controller_cfg(0, pdata);

		/* disable DSI phy */
		mdss_dsi_phy_disable(ctrl_pdata);
	}

	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 0);

panel_power_ctrl:
	ret = mdss_dsi_panel_power_ctrl(pdata, power_state);
	if (ret) {
		pr_err("%s: Panel power off failed\n", __func__);
		goto end;
	}

	if (panel_info->dynamic_fps
	    && (panel_info->dfps_update == DFPS_SUSPEND_RESUME_MODE)
	    && (panel_info->new_fps != panel_info->mipi.frame_rate))
		panel_info->mipi.frame_rate = panel_info->new_fps;

end:
	pr_debug("%s-:\n", __func__);

	return ret;
}

static int mdss_dsi_update_panel_config(struct mdss_dsi_ctrl_pdata *ctrl_pdata,
				int mode)
{
	int ret = 0;
	struct mdss_panel_info *pinfo = &(ctrl_pdata->panel_data.panel_info);

	if (mode == DSI_CMD_MODE) {
		pinfo->mipi.mode = DSI_CMD_MODE;
		pinfo->type = MIPI_CMD_PANEL;
		pinfo->mipi.vsync_enable = 1;
		pinfo->mipi.hw_vsync_mode = 1;
	} else {	/*video mode*/
		pinfo->mipi.mode = DSI_VIDEO_MODE;
		pinfo->type = MIPI_VIDEO_PANEL;
		pinfo->mipi.vsync_enable = 0;
		pinfo->mipi.hw_vsync_mode = 0;
	}

	ctrl_pdata->panel_mode = pinfo->mipi.mode;
	mdss_panel_get_dst_fmt(pinfo->bpp, pinfo->mipi.mode,
			pinfo->mipi.pixel_packing, &(pinfo->mipi.dst_format));
	pinfo->cont_splash_enabled = 0;

	return ret;
}

int mdss_dsi_on(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mdss_panel_info *pinfo;
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	int cur_power_state;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	cur_power_state = pdata->panel_info.panel_power_state;
	pr_debug("%s+: ctrl=%pK ndx=%d cur_power_state=%d\n", __func__,
		ctrl_pdata, ctrl_pdata->ndx, cur_power_state);

	pinfo = &pdata->panel_info;
	mipi = &pdata->panel_info.mipi;

	if (mdss_dsi_is_panel_on_interactive(pdata)) {
		pr_debug("%s: panel already on\n", __func__);
		goto end;
	}

	ret = mdss_dsi_panel_power_ctrl(pdata, MDSS_PANEL_POWER_ON);
	if (ret) {
		pr_err("%s:Panel power on failed. rc=%d\n", __func__, ret);
		return ret;
	}
	if (!strcmp(ctrl_pdata->panel_data.panel_info.panel_name,"BOE_NT51021_10_1200P_VIDEO"))
	{
		if (gpio_is_valid(ctrl_pdata->disp_vcc_gpio)){
				gpio_set_value((ctrl_pdata->disp_vcc_gpio), 1);
				pr_info("%s,%d set disp_power_panel = 1 \n", __func__, __LINE__);
			}
		if (gpio_is_valid(ctrl_pdata->disp_bl_gpio)){
			gpio_set_value((ctrl_pdata->disp_bl_gpio), 1);
			pr_info("%s,%d set disp_power_backlight = 1 \n", __func__, __LINE__);
		}
		 mdelay(30);
	 }
	if (cur_power_state != MDSS_PANEL_POWER_OFF) {
		pr_debug("%s: dsi_on from panel low power state\n", __func__);
		goto end;
	}

	/*
	 * Enable DSI bus clocks prior to resetting and initializing DSI
	 * Phy. Phy and ctrl setup need to be done before enabling the link
	 * clocks.
	 */
	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_BUS_CLKS, 1);

	/*
	 * If ULPS during suspend feature is enabled, then DSI PHY was
	 * left on during suspend. In this case, we do not need to reset/init
	 * PHY. This would have already been done when the BUS clocks are
	 * turned on. However, if cont splash is disabled, the first time DSI
	 * is powered on, phy init needs to be done unconditionally.
	 */
	if (!pdata->panel_info.ulps_suspend_enabled || !ctrl_pdata->ulps) {
		mdss_dsi_phy_sw_reset(ctrl_pdata);
		mdss_dsi_phy_init(ctrl_pdata);
		mdss_dsi_ctrl_setup(ctrl_pdata);
	}

	/* DSI link clocks need to be on prior to ctrl sw reset */
	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_LINK_CLKS, 1);
	mdss_dsi_sw_reset(ctrl_pdata, true);

	/*
	 * Issue hardware reset line after enabling the DSI clocks and data
	 * data lanes for LP11 init
	 */
	if (mipi->lp11_init) {
		if (mdss_dsi_pinctrl_set_state(ctrl_pdata, true))
			pr_debug("reset enable: pinctrl not enabled\n");
		mdss_dsi_panel_reset(pdata, 1);
	}

	if (mipi->init_delay)
		usleep(mipi->init_delay);

	if (mipi->force_clk_lane_hs) {
		u32 tmp;

		tmp = MIPI_INP((ctrl_pdata->ctrl_base) + 0xac);
		tmp |= (1<<28);
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0xac, tmp);
		wmb();
	}

	if (pdata->panel_info.type == MIPI_CMD_PANEL)
		mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 0);

end:
	pr_debug("%s-:\n", __func__);
	return 0;
}

static int mdss_dsi_pinctrl_set_state(
	struct mdss_dsi_ctrl_pdata *ctrl_pdata,
	bool active)
{
	struct pinctrl_state *pin_state;
	int rc = -EFAULT;

	if (IS_ERR_OR_NULL(ctrl_pdata->pin_res.pinctrl))
		return PTR_ERR(ctrl_pdata->pin_res.pinctrl);

	pin_state = active ? ctrl_pdata->pin_res.gpio_state_active
				: ctrl_pdata->pin_res.gpio_state_suspend;
	if (!IS_ERR_OR_NULL(pin_state)) {
		rc = pinctrl_select_state(ctrl_pdata->pin_res.pinctrl,
				pin_state);
		if (rc)
			pr_err("%s: can not set %s pins\n", __func__,
			       active ? MDSS_PINCTRL_STATE_DEFAULT
			       : MDSS_PINCTRL_STATE_SLEEP);
	} else {
		pr_err("%s: invalid '%s' pinstate\n", __func__,
		       active ? MDSS_PINCTRL_STATE_DEFAULT
		       : MDSS_PINCTRL_STATE_SLEEP);
	}
	return rc;
}

static int mdss_dsi_pinctrl_init(struct platform_device *pdev)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata;

	ctrl_pdata = platform_get_drvdata(pdev);
	ctrl_pdata->pin_res.pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR_OR_NULL(ctrl_pdata->pin_res.pinctrl)) {
		pr_err("%s: failed to get pinctrl\n", __func__);
		return PTR_ERR(ctrl_pdata->pin_res.pinctrl);
	}

	ctrl_pdata->pin_res.gpio_state_active
		= pinctrl_lookup_state(ctrl_pdata->pin_res.pinctrl,
				MDSS_PINCTRL_STATE_DEFAULT);
	if (IS_ERR_OR_NULL(ctrl_pdata->pin_res.gpio_state_active))
		pr_warn("%s: can not get default pinstate\n", __func__);

	ctrl_pdata->pin_res.gpio_state_suspend
		= pinctrl_lookup_state(ctrl_pdata->pin_res.pinctrl,
				MDSS_PINCTRL_STATE_SLEEP);
	if (IS_ERR_OR_NULL(ctrl_pdata->pin_res.gpio_state_suspend))
		pr_warn("%s: can not get sleep pinstate\n", __func__);

	return 0;
}

static int mdss_dsi_unblank(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	mipi  = &pdata->panel_info.mipi;
	/* optimize the screen wake up time*/
	mdss_dsi_pm_qos_update_request(DSI_DISABLE_PC_LATENCY);
	pr_debug("%s+: ctrl=%pK ndx=%d cur_blank_state=%d\n", __func__,
		ctrl_pdata, ctrl_pdata->ndx, pdata->panel_info.blank_state);

	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 1);

	if (pdata->panel_info.blank_state == MDSS_PANEL_BLANK_LOW_POWER) {
		pr_debug("%s: dsi_unblank with panel always on\n", __func__);
		if (ctrl_pdata->low_power_config)
			ret = ctrl_pdata->low_power_config(pdata, false);
		goto error;
	}

	if (!(ctrl_pdata->ctrl_state & CTRL_STATE_PANEL_INIT)) {
		if (!pdata->panel_info.dynamic_switch_pending) {
			ret = ctrl_pdata->on(pdata);
			if (ret) {
				pr_err("%s: unable to initialize the panel\n",
							__func__);
				goto error;
			}
		}
		ctrl_pdata->ctrl_state |= CTRL_STATE_PANEL_INIT;
	}

	if ((pdata->panel_info.type == MIPI_CMD_PANEL) &&
		mipi->vsync_enable && mipi->hw_vsync_mode) {
		mdss_dsi_set_tear_on(ctrl_pdata);
		if (mdss_dsi_is_te_based_esd(ctrl_pdata))
			enable_irq(gpio_to_irq(ctrl_pdata->disp_te_gpio));
	}

error:
	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 0);
	/* optimize the screen wake up time*/
	mdss_dsi_pm_qos_update_request(DSI_ENABLE_PC_LATENCY);
	pr_debug("%s-:\n", __func__);

	return ret;
}

static int mdss_dsi_blank(struct mdss_panel_data *pdata, int power_state)
{
	int ret = 0;
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	mipi = &pdata->panel_info.mipi;

	pr_debug("%s+: ctrl=%pK ndx=%d power_state=%d\n",
		__func__, ctrl_pdata, ctrl_pdata->ndx, power_state);

	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 1);

	if (mdss_panel_is_power_on_lp(power_state)) {
		pr_debug("%s: low power state requested\n", __func__);
		if (ctrl_pdata->low_power_config)
			ret = ctrl_pdata->low_power_config(pdata, true);
		goto error;
	}

	if (pdata->panel_info.type == MIPI_VIDEO_PANEL &&
			ctrl_pdata->off_cmds.link_state == DSI_LP_MODE) {
		mdss_dsi_sw_reset(ctrl_pdata, false);
		mdss_dsi_host_init(pdata);
	}

	mdss_dsi_op_mode_config(DSI_CMD_MODE, pdata);

	if (pdata->panel_info.dynamic_switch_pending) {
		pr_info("%s: switching to %s mode\n", __func__,
			(pdata->panel_info.mipi.mode ? "video" : "command"));
		if (pdata->panel_info.type == MIPI_CMD_PANEL) {
			ctrl_pdata->switch_mode(pdata, DSI_VIDEO_MODE);
		} else if (pdata->panel_info.type == MIPI_VIDEO_PANEL) {
			ctrl_pdata->switch_mode(pdata, DSI_CMD_MODE);
			mdss_dsi_set_tear_off(ctrl_pdata);
		}
	}

	if ((pdata->panel_info.type == MIPI_CMD_PANEL) &&
		mipi->vsync_enable && mipi->hw_vsync_mode) {
		if (mdss_dsi_is_te_based_esd(ctrl_pdata)) {
				disable_irq(gpio_to_irq(
					ctrl_pdata->disp_te_gpio));
				atomic_dec(&ctrl_pdata->te_irq_ready);
		}
		mdss_dsi_set_tear_off(ctrl_pdata);
	}

	if (ctrl_pdata->ctrl_state & CTRL_STATE_PANEL_INIT) {
		if (!pdata->panel_info.dynamic_switch_pending) {
			ret = ctrl_pdata->off(pdata);
			if (ret) {
				pr_err("%s: Panel OFF failed\n", __func__);
				goto error;
			}
		}
		ctrl_pdata->ctrl_state &= ~CTRL_STATE_PANEL_INIT;
	}

error:
	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 0);
	pr_debug("%s-:End\n", __func__);
	return ret;
}

static int mdss_dsi_post_panel_on(struct mdss_panel_data *pdata)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pr_debug("%s+: ctrl=%pK ndx=%d\n", __func__,
				ctrl_pdata, ctrl_pdata->ndx);

	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 1);

	if (ctrl_pdata->post_panel_on)
		ctrl_pdata->post_panel_on(pdata);

	mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 0);
	pr_debug("%s-:\n", __func__);

	return 0;
}

int mdss_dsi_cont_splash_on(struct mdss_panel_data *pdata)
{
	int ret = 0;
	struct mipi_panel_info *mipi;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	pr_info("%s:%d DSI on for continuous splash.\n", __func__, __LINE__);

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	mipi = &pdata->panel_info.mipi;

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pr_debug("%s+: ctrl=%pK ndx=%d\n", __func__,
				ctrl_pdata, ctrl_pdata->ndx);

	WARN((ctrl_pdata->ctrl_state & CTRL_STATE_PANEL_INIT),
		"Incorrect Ctrl state=0x%x\n", ctrl_pdata->ctrl_state);

	mdss_dsi_ctrl_setup(ctrl_pdata);
	mdss_dsi_sw_reset(ctrl_pdata, true);
	pr_debug("%s-:End\n", __func__);
	return ret;
}

static void __mdss_dsi_update_video_mode_total(struct mdss_panel_data *pdata,
		int new_fps)
{
	u32 hsync_period, vsync_period, ctrl_rev;
	u32 new_dsi_v_total, current_dsi_v_total;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s Invalid pdata\n", __func__);
		return;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
			panel_data);
	if (ctrl_pdata == NULL) {
		pr_err("%s Invalid ctrl_pdata\n", __func__);
		return;
	}

	vsync_period =
		mdss_panel_get_vtotal(&pdata->panel_info);
	hsync_period =
		mdss_panel_get_htotal(&pdata->panel_info, true);
	current_dsi_v_total =
		MIPI_INP((ctrl_pdata->ctrl_base) + 0x2C);
	new_dsi_v_total =
		((vsync_period - 1) << 16) | (hsync_period - 1);

	MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x2C,
			(current_dsi_v_total | 0x8000000));
	if (new_dsi_v_total & 0x8000000) {
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x2C,
				new_dsi_v_total);
	} else {
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x2C,
				(new_dsi_v_total | 0x8000000));
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x2C,
				(new_dsi_v_total & 0x7ffffff));
	}
	ctrl_rev = MIPI_INP(ctrl_pdata->ctrl_base);
	/* Flush DSI TIMING registers for 8916/8939 */
	if (ctrl_rev == MDSS_DSI_HW_REV_103_1)
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x1e4, 0x1);
	ctrl_pdata->panel_data.panel_info.mipi.frame_rate = new_fps;

}

static void __mdss_dsi_dyn_refresh_config(
		struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	int reg_data;

	reg_data = MIPI_INP((ctrl_pdata->ctrl_base) + DSI_DYNAMIC_REFRESH_CTRL);
	reg_data &= ~BIT(12);

	pr_debug("Dynamic fps ctrl = 0x%x\n", reg_data);
	MIPI_OUTP((ctrl_pdata->ctrl_base) + DSI_DYNAMIC_REFRESH_CTRL, reg_data);
}

static void __mdss_dsi_calc_dfps_delay(struct mdss_panel_data *pdata)
{
	u32 esc_clk_rate = XO_CLK_RATE;
	u32 pipe_delay, pipe_delay2 = 0, pll_delay;
	u32 hsync_period = 0;
	u32 pclk_to_esc_ratio, byte_to_esc_ratio, hr_bit_to_esc_ratio;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_panel_info *pinfo = NULL;
	struct mdss_dsi_phy_ctrl *pd = NULL;

	if (pdata == NULL) {
		pr_err("%s Invalid pdata\n", __func__);
		return;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
			panel_data);

	pinfo = &pdata->panel_info;
	pd = &(pinfo->mipi.dsi_phy_db);

	pclk_to_esc_ratio = (ctrl_pdata->pclk_rate / esc_clk_rate);
	byte_to_esc_ratio = (ctrl_pdata->byte_clk_rate / esc_clk_rate);
	hr_bit_to_esc_ratio = ((ctrl_pdata->byte_clk_rate * 4) / esc_clk_rate);

	hsync_period = mdss_panel_get_htotal(pinfo, true);
	pipe_delay = (hsync_period + 1) / pclk_to_esc_ratio;
	if (pinfo->mipi.eof_bllp_power_stop == 0)
		pipe_delay += (17 / pclk_to_esc_ratio) +
			((21 + pinfo->mipi.t_clk_pre +
			pinfo->mipi.t_clk_post) / byte_to_esc_ratio) +
			((((pd->timing[8] >> 1) + 1) +
			((pd->timing[6] >> 1) + 1) +
			((pd->timing[3] * 4) + (pd->timing[5] >> 1) + 1) +
			((pd->timing[7] >> 1) + 1) +
			((pd->timing[1] >> 1) + 1) +
			((pd->timing[4] >> 1) + 1)) / hr_bit_to_esc_ratio);

	if (pinfo->mipi.force_clk_lane_hs)
		pipe_delay2 = (6 / byte_to_esc_ratio) +
			((((pd->timing[1] >> 1) + 1) +
			((pd->timing[4] >> 1) + 1)) / hr_bit_to_esc_ratio);

	pll_delay = ((1000 * esc_clk_rate) / 1000000) * 2;

	MIPI_OUTP((ctrl_pdata->ctrl_base) + DSI_DYNAMIC_REFRESH_PIPE_DELAY,
						pipe_delay);
	MIPI_OUTP((ctrl_pdata->ctrl_base) + DSI_DYNAMIC_REFRESH_PIPE_DELAY2,
						pipe_delay2);
	MIPI_OUTP((ctrl_pdata->ctrl_base) + DSI_DYNAMIC_REFRESH_PLL_DELAY,
						pll_delay);
}

static int __mdss_dsi_dfps_update_clks(struct mdss_panel_data *pdata,
		int new_fps)
{
	int rc = 0;
	u32 data;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s Invalid pdata\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
			panel_data);
	if (ctrl_pdata == NULL) {
		pr_err("%s Invalid ctrl_pdata\n", __func__);
		return -EINVAL;
	}

	rc = mdss_dsi_clk_div_config
		(&ctrl_pdata->panel_data.panel_info, new_fps);
	if (rc) {
		pr_err("%s: unable to initialize the clk dividers\n",
				__func__);
		return rc;
	}

	if (pdata->panel_info.dfps_update
			== DFPS_IMMEDIATE_CLK_UPDATE_MODE) {
		__mdss_dsi_dyn_refresh_config(ctrl_pdata);
		__mdss_dsi_calc_dfps_delay(pdata);
		ctrl_pdata->pclk_rate =
			pdata->panel_info.mipi.dsi_pclk_rate;
		ctrl_pdata->byte_clk_rate =
			pdata->panel_info.clk_rate / 8;

		pr_debug("byte_rate=%i\n", ctrl_pdata->byte_clk_rate);
		pr_debug("pclk_rate=%i\n", ctrl_pdata->pclk_rate);

		if (mdss_dsi_is_ctrl_clk_slave(ctrl_pdata)) {
			pr_debug("%s DFPS already updated.\n", __func__);
			return rc;
		}

		/* add an extra reference to main clks */
		clk_prepare_enable(ctrl_pdata->pll_byte_clk);
		clk_prepare_enable(ctrl_pdata->pll_pixel_clk);

		/* change the parent to shadow clocks*/
		clk_set_parent(ctrl_pdata->mux_byte_clk,
				ctrl_pdata->shadow_byte_clk);
		clk_set_parent(ctrl_pdata->mux_pixel_clk,
				ctrl_pdata->shadow_pixel_clk);

		rc =  clk_set_rate(ctrl_pdata->byte_clk,
					ctrl_pdata->byte_clk_rate);
		if (rc) {
			pr_err("%s: dsi_byte_clk - clk_set_rate failed\n",
					__func__);
			return rc;
		}

		rc = clk_set_rate(ctrl_pdata->pixel_clk, ctrl_pdata->pclk_rate);
		if (rc) {
			pr_err("%s: dsi_pixel_clk - clk_set_rate failed\n",
				__func__);
			return rc;
		}

		mdss_dsi_en_wait4dynamic_done(ctrl_pdata);
		MIPI_OUTP((ctrl_pdata->ctrl_base) + DSI_DYNAMIC_REFRESH_CTRL,
							0x00);

		data = MIPI_INP((ctrl_pdata->ctrl_base) + 0x0120);
		MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x120, data);
		pr_debug("pll unlock: 0x%x\n", data);
		clk_set_parent(ctrl_pdata->mux_byte_clk,
				ctrl_pdata->pll_byte_clk);
		clk_set_parent(ctrl_pdata->mux_pixel_clk,
				ctrl_pdata->pll_pixel_clk);
		clk_disable_unprepare(ctrl_pdata->pll_byte_clk);
		clk_disable_unprepare(ctrl_pdata->pll_pixel_clk);
		ctrl_pdata->panel_data.panel_info.mipi.frame_rate = new_fps;
	} else {
		ctrl_pdata->pclk_rate =
			pdata->panel_info.mipi.dsi_pclk_rate;
		ctrl_pdata->byte_clk_rate =
			pdata->panel_info.clk_rate / 8;
	}

	return rc;
}

static int mdss_dsi_dfps_config(struct mdss_panel_data *pdata, int new_fps)
{
	int rc = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_dsi_ctrl_pdata *sctrl_pdata = NULL;
	struct mdss_panel_info *pinfo;

	pr_debug("%s+:\n", __func__);

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
			panel_data);

	if (!ctrl_pdata->panel_data.panel_info.dynamic_fps) {
		pr_err("%s: Dynamic fps not enabled for this panel\n",
					__func__);
		return -EINVAL;
	}

	/*
	 * at split display case, DFPS registers were already programmed
	 * while programming the left ctrl(DSI0). Ignore right ctrl (DSI1)
	 * reguest.
	 */
	pinfo = &pdata->panel_info;
	if (pinfo->is_split_display) {
		if (mdss_dsi_is_right_ctrl(ctrl_pdata)) {
			pr_debug("%s DFPS already updated.\n", __func__);
			return rc;
		}
		/* left ctrl to get right ctrl */
		sctrl_pdata = mdss_dsi_get_other_ctrl(ctrl_pdata);
	}

	ctrl_pdata->dfps_status = true;
	if (sctrl_pdata)
		sctrl_pdata->dfps_status = true;

	if (new_fps !=
		ctrl_pdata->panel_data.panel_info.mipi.frame_rate) {
		if (pdata->panel_info.dfps_update
			== DFPS_IMMEDIATE_PORCH_UPDATE_MODE) {

			__mdss_dsi_update_video_mode_total(pdata, new_fps);
			if (sctrl_pdata) {
				pr_debug("%s Updating slave ctrl DFPS\n",
						__func__);
				__mdss_dsi_update_video_mode_total(
						&sctrl_pdata->panel_data,
						new_fps);
			}

		} else {
			rc = __mdss_dsi_dfps_update_clks(pdata, new_fps);
			if (!rc && sctrl_pdata) {
				pr_debug("%s Updating slave ctrl DFPS\n",
						__func__);
				rc = __mdss_dsi_dfps_update_clks(
						&sctrl_pdata->panel_data,
						new_fps);
			}
		}
	} else {
		pr_debug("%s: Panel is already at this FPS\n", __func__);
	}

	return rc;
}

static int mdss_dsi_ctl_partial_roi(struct mdss_panel_data *pdata)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	int rc = -EINVAL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	if (ctrl_pdata->set_col_page_addr)
		rc = ctrl_pdata->set_col_page_addr(pdata);

	return rc;
}

static int mdss_dsi_set_stream_size(struct mdss_panel_data *pdata)
{
	u32 data, idle;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct mdss_panel_info *pinfo;
	struct mdss_rect *roi;
	struct panel_horizontal_idle *pidle;
	int i;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);

	pinfo = &pdata->panel_info;

	if (!pinfo->partial_update_enabled)
		return -EINVAL;

	roi = &pinfo->roi;

	/* DSI_COMMAND_MODE_MDP_STREAM_CTRL */
	data = (((roi->w * 3) + 1) << 16) |
			(pdata->panel_info.mipi.vc << 8) | DTYPE_DCS_LWRITE;
	MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x60, data);
	MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x58, data);

	/* DSI_COMMAND_MODE_MDP_STREAM_TOTAL */
	data = roi->h << 16 | roi->w;
	MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x64, data);
	MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x5C, data);

	/* set idle control -- dsi clk cycle */
	idle = 0;
	pidle = ctrl_pdata->line_idle;
	for (i = 0; i < ctrl_pdata->horizontal_idle_cnt; i++) {
		if (roi->w > pidle->min && roi->w <= pidle->max) {
			idle = pidle->idle;
			pr_debug("%s: ndx=%d w=%d range=%d-%d idle=%d\n",
				__func__, ctrl_pdata->ndx, roi->w,
				pidle->min, pidle->max, pidle->idle);
			break;
		}
		pidle++;
	}

	if (idle)
		idle |= BIT(12);	/* enable */

	MIPI_OUTP((ctrl_pdata->ctrl_base) + 0x194, idle);

	return 0;
}

int mdss_dsi_register_recovery_handler(struct mdss_dsi_ctrl_pdata *ctrl,
	struct mdss_intf_recovery *recovery)
{
	mutex_lock(&ctrl->mutex);
	ctrl->recovery = recovery;
	mutex_unlock(&ctrl->mutex);
	return 0;
}
/*rename gpio*/
#ifdef CONFIG_HUAWEI_LCD
static void hw_panel_bias_en(struct mdss_panel_data *pdata, int enable)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return;
	}
	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	if (gpio_is_valid(ctrl_pdata->disp_vled_gpio))
	{
		gpio_set_value((ctrl_pdata->disp_vled_gpio), enable);
		pr_debug("%s,%d set en disp_vled_gpio = %d \n",__func__,__LINE__,enable);
	}

	if(enable){
		ctrl_pdata->hw_led_en_flag = 1;
	}else {
		ctrl_pdata->hw_led_en_flag = 0;
	}

}
#endif

static int mdss_dsi_clk_refresh(struct mdss_panel_data *pdata)
{
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	int rc = 0;

	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
							panel_data);
	rc = mdss_dsi_clk_div_config(&pdata->panel_info,
			pdata->panel_info.mipi.frame_rate);
	if (rc) {
		pr_err("%s: unable to initialize the clk dividers\n",
								__func__);
		return rc;
	}
	ctrl_pdata->refresh_clk_rate = false;
	ctrl_pdata->pclk_rate = pdata->panel_info.mipi.dsi_pclk_rate;
	ctrl_pdata->byte_clk_rate = pdata->panel_info.clk_rate / 8;
	pr_debug("%s ctrl_pdata->byte_clk_rate=%d ctrl_pdata->pclk_rate=%d\n",
		__func__, ctrl_pdata->byte_clk_rate, ctrl_pdata->pclk_rate);
	return rc;
}

static int mdss_dsi_event_handler(struct mdss_panel_data *pdata,
				  int event, void *arg)
{
	int rc = 0;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	int power_state;

	if (pdata == NULL) {
		pr_err("%s: Invalid input data\n", __func__);
		return -EINVAL;
	}
	ctrl_pdata = container_of(pdata, struct mdss_dsi_ctrl_pdata,
				panel_data);
	pr_debug("%s+: ctrl=%d event=%d\n", __func__, ctrl_pdata->ndx, event);

	MDSS_XLOG(event, arg, ctrl_pdata->ndx, 0x3333);

	switch (event) {
	case MDSS_EVENT_CHECK_PARAMS:
		pr_debug("%s:Entered Case MDSS_EVENT_CHECK_PARAMS\n", __func__);
		ctrl_pdata->refresh_clk_rate = true;
		break;
	case MDSS_EVENT_LINK_READY:
		rc = mdss_dsi_on(pdata);
		mdss_dsi_op_mode_config(pdata->panel_info.mipi.mode,
							pdata);
		break;
	case MDSS_EVENT_UNBLANK:
		if (ctrl_pdata->refresh_clk_rate)
			rc = mdss_dsi_clk_refresh(pdata);
		mdss_dsi_get_hw_revision(ctrl_pdata);
		if (ctrl_pdata->on_cmds.link_state == DSI_LP_MODE)
			rc = mdss_dsi_unblank(pdata);
		break;
	case MDSS_EVENT_POST_PANEL_ON:
		rc = mdss_dsi_post_panel_on(pdata);
		break;
	case MDSS_EVENT_PANEL_ON:
		ctrl_pdata->ctrl_state |= CTRL_STATE_MDP_ACTIVE;
		if (ctrl_pdata->on_cmds.link_state == DSI_HS_MODE)
			rc = mdss_dsi_unblank(pdata);
		pdata->panel_info.esd_rdy = true;
		break;
	case MDSS_EVENT_BLANK:
		if (ctrl_pdata->hw_product_pad)
		{
			if (1 != ctrl_pdata->which_product_pad){
				hw_panel_bias_en(pdata,0);
				mdelay(100);
			}
		}
		power_state = (int) (unsigned long) arg;
		if (ctrl_pdata->off_cmds.link_state == DSI_HS_MODE)
			rc = mdss_dsi_blank(pdata, power_state);
		break;
	case MDSS_EVENT_PANEL_OFF:
		power_state = (int) (unsigned long) arg;
		ctrl_pdata->ctrl_state &= ~CTRL_STATE_MDP_ACTIVE;
		if (ctrl_pdata->off_cmds.link_state == DSI_LP_MODE)
			rc = mdss_dsi_blank(pdata, power_state);
		if (!(pdata->panel_info.mipi.always_on))
			rc = mdss_dsi_off(pdata, power_state);
		break;
	case MDSS_EVENT_CONT_SPLASH_FINISH:
		if (ctrl_pdata->off_cmds.link_state == DSI_LP_MODE)
			rc = mdss_dsi_blank(pdata, MDSS_PANEL_POWER_OFF);
		ctrl_pdata->ctrl_state &= ~CTRL_STATE_MDP_ACTIVE;
		rc = mdss_dsi_cont_splash_on(pdata);
		break;
	case MDSS_EVENT_PANEL_CLK_CTRL:
		mdss_dsi_clk_req(ctrl_pdata, (int) (unsigned long) arg);
		break;
	case MDSS_EVENT_DSI_CMDLIST_KOFF:
		mdss_dsi_cmdlist_commit(ctrl_pdata, 1);
		break;
	case MDSS_EVENT_PANEL_UPDATE_FPS:
		if (arg != NULL) {
			rc = mdss_dsi_dfps_config(pdata,
					 (int) (unsigned long) arg);
			pr_debug("%s:update fps to = %d\n",
				 __func__, (int) (unsigned long) arg);
		}
		break;
	case MDSS_EVENT_CONT_SPLASH_BEGIN:
		if (ctrl_pdata->off_cmds.link_state == DSI_HS_MODE) {
			/* Panel is Enabled in Bootloader */
			rc = mdss_dsi_blank(pdata, MDSS_PANEL_POWER_OFF);
		}
		break;
	case MDSS_EVENT_ENABLE_PARTIAL_ROI:
		rc = mdss_dsi_ctl_partial_roi(pdata);
		break;
	case MDSS_EVENT_DSI_STREAM_SIZE:
		rc = mdss_dsi_set_stream_size(pdata);
		break;
	case MDSS_EVENT_DSI_DYNAMIC_SWITCH:
		rc = mdss_dsi_update_panel_config(ctrl_pdata,
					(int)(unsigned long) arg);
		break;
	case MDSS_EVENT_REGISTER_RECOVERY_HANDLER:
		rc = mdss_dsi_register_recovery_handler(ctrl_pdata,
			(struct mdss_intf_recovery *)arg);
		break;
	case MDSS_EVENT_DSI_PANEL_STATUS:
		if (ctrl_pdata->check_status)
			rc = ctrl_pdata->check_status(ctrl_pdata);
		break;
	default:
		pr_debug("%s: unhandled event=%d\n", __func__, event);
		break;
	}
	pr_debug("%s-:event=%d, rc=%d\n", __func__, event, rc);
	return rc;
}

static struct device_node *mdss_dsi_pref_prim_panel(
		struct platform_device *pdev)
{
	struct device_node *dsi_pan_node = NULL;

	pr_debug("%s:%d: Select primary panel from dt\n",
					__func__, __LINE__);
	dsi_pan_node = of_parse_phandle(pdev->dev.of_node,
					"qcom,dsi-pref-prim-pan", 0);
	if (!dsi_pan_node)
		pr_err("%s:can't find panel phandle\n", __func__);

	return dsi_pan_node;
}
/* This function be used to change the dsi regulator mode.*/
void set_mipi_regulator_mode(int mode)
{
	g_mipi_regulator_mode = mode;
}
/* Calling this function be get the dsi regulator mode.*/
int get_mipi_regulator_mode(void)
{
	return g_mipi_regulator_mode;
}
/**
 * mdss_dsi_find_panel_of_node(): find device node of dsi panel
 * @pdev: platform_device of the dsi ctrl node
 * @panel_cfg: string containing intf specific config data
 *
 * Function finds the panel device node using the interface
 * specific configuration data. This configuration data is
 * could be derived from the result of bootloader's GCDB
 * panel detection mechanism. If such config data doesn't
 * exist then this panel returns the default panel configured
 * in the device tree.
 *
 * returns pointer to panel node on success, NULL on error.
 */
static struct device_node *mdss_dsi_find_panel_of_node(
		struct platform_device *pdev, char *panel_cfg)
{
	int len, i;
	int ctrl_id = pdev->id - 1;
	char panel_name[MDSS_MAX_PANEL_LEN];
	char ctrl_id_stream[3] =  "0:";
	char mipi_mode_stream[] =  ":lcd_mipi_regulator_mode:";
	char *stream = NULL, *pan = NULL;
	struct device_node *dsi_pan_node = NULL, *mdss_node = NULL;

	len = strlen(panel_cfg);
	if (!len) {
		/* no panel cfg chg, parse dt */
		pr_debug("%s:%d: no cmd line cfg present\n",
			 __func__, __LINE__);
		goto end;
	} else {
		if (ctrl_id == 1)
			strlcpy(ctrl_id_stream, "1:", 3);

		stream = strnstr(panel_cfg, ctrl_id_stream, len);
		if (!stream) {
			pr_err("controller config is not present\n");
			goto end;
		}
		stream += 2;

		pan = strnchr(stream, strlen(stream), ':');
		if (!pan) {
			strlcpy(panel_name, stream, MDSS_MAX_PANEL_LEN);
		} else {
			for (i = 0; (stream + i) < pan; i++)
				panel_name[i] = *(stream + i);
			panel_name[i] = 0;
		}
		/* Here to parse the cmdline of the dsi regulator mode.*/
		stream = strnstr(panel_cfg, mipi_mode_stream, len);
		if (!stream) {
				pr_err("controller config is not present\n");
				goto end;
		}
		stream += strlen(mipi_mode_stream);
		if (*stream == '0')
		{
			set_mipi_regulator_mode(REGULATOR_DCDC_MODE);
		}
		else
		{
			set_mipi_regulator_mode(REGULATOR_LDO_MODE);
		}
		LCD_LOG_INFO("%s:dsi_regulator_mode = %c \n",__func__,*stream);
		pr_debug("%s:%d:%s:%s\n", __func__, __LINE__,
			 panel_cfg, panel_name);

		mdss_node = of_parse_phandle(pdev->dev.of_node,
					     "qcom,mdss-mdp", 0);

		if (!mdss_node) {
			pr_err("%s: %d: mdss_node null\n",
			       __func__, __LINE__);
			return NULL;
		}
		dsi_pan_node = of_find_node_by_name(mdss_node,
						    panel_name);
		if (!dsi_pan_node) {
			pr_err("%s: invalid pan node, selecting prim panel\n",
			       __func__);
			goto end;
		}
		return dsi_pan_node;
	}
end:
	if (strcmp(panel_name, NONE_PANEL))
		dsi_pan_node = mdss_dsi_pref_prim_panel(pdev);

	return dsi_pan_node;
}

static int mdss_dsi_ctrl_probe(struct platform_device *pdev)
{
	int rc = 0, i = 0;
	u32 index;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct device_node *dsi_pan_node = NULL;
	char panel_cfg[MDSS_MAX_PANEL_LEN];
	const char *ctrl_name;
	bool cmd_cfg_cont_splash = true;
	struct mdss_panel_cfg *pan_cfg = NULL;
	struct mdss_util_intf *util;

	util = mdss_get_util_intf();
	if (util == NULL) {
		pr_err("Failed to get mdss utility functions\n");
		return -ENODEV;
	}

	if (!util->mdp_probe_done) {
		pr_err("%s: MDP not probed yet!\n", __func__);
		return -EPROBE_DEFER;
	}

	if (!pdev->dev.of_node) {
		pr_err("DSI driver only supports device tree probe\n");
		return -ENOTSUPP;
	}

	pan_cfg = util->panel_intf_type(MDSS_PANEL_INTF_HDMI);
	if (IS_ERR(pan_cfg)) {
		return PTR_ERR(pan_cfg);
	} else if (pan_cfg) {
		pr_debug("%s: HDMI is primary\n", __func__);
		return -ENODEV;
	}

	ctrl_pdata = platform_get_drvdata(pdev);
	if (!ctrl_pdata) {
		ctrl_pdata = devm_kzalloc(&pdev->dev,
					  sizeof(struct mdss_dsi_ctrl_pdata),
					  GFP_KERNEL);
		if (!ctrl_pdata) {
			pr_err("%s: FAILED: cannot alloc dsi ctrl\n",
			       __func__);
			rc = -ENOMEM;
			goto error_no_mem;
		}
		platform_set_drvdata(pdev, ctrl_pdata);
	}
	ctrl_pdata->mdss_util = util;
	atomic_set(&ctrl_pdata->te_irq_ready, 0);

	ctrl_name = of_get_property(pdev->dev.of_node, "label", NULL);
	if (!ctrl_name)
		pr_info("%s:%d, DSI Ctrl name not specified\n",
			__func__, __LINE__);
	else
		pr_info("%s: DSI Ctrl name = %s\n",
			__func__, ctrl_name);

	rc = of_property_read_u32(pdev->dev.of_node,
				  "cell-index", &index);
	if (rc) {
		dev_err(&pdev->dev,
			"%s: Cell-index not specified, rc=%d\n",
			__func__, rc);
		goto error_no_mem;
	}

	if (index == 0)
		pdev->id = 1;
	else
		pdev->id = 2;

	rc = of_platform_populate(pdev->dev.of_node,
				  NULL, NULL, &pdev->dev);
	if (rc) {
		dev_err(&pdev->dev,
			"%s: failed to add child nodes, rc=%d\n",
			__func__, rc);
		goto error_no_mem;
	}

	rc = mdss_dsi_pinctrl_init(pdev);
	if (rc)
		pr_warn("%s: failed to get pin resources\n", __func__);

	/* Parse the regulator information */
	for (i = 0; i < DSI_MAX_PM; i++) {
		rc = mdss_dsi_get_dt_vreg_data(&pdev->dev,
			&ctrl_pdata->power_data[i], i);
		if (rc) {
			DEV_ERR("%s: '%s' get_dt_vreg_data failed.rc=%d\n",
				__func__, __mdss_dsi_pm_name(i), rc);
			goto error_vreg;
		}
	}

	/*
	 * Currently, the Bias vreg is controlled by wled driver.
	 * Once we have support from pmic driver, implement the
	 * bias vreg control using the existing vreg apis.
	 */
	ctrl_pdata->panel_bias_vreg = of_property_read_bool(
			pdev->dev.of_node, "qcom,dsi-panel-bias-vreg");

	/* DSI panels can be different between controllers */
	rc = mdss_dsi_get_panel_cfg(panel_cfg, ctrl_pdata);
	if (!rc)
		/* dsi panel cfg not present */
		pr_warn("%s:%d:dsi specific cfg not present\n",
			__func__, __LINE__);

	/* find panel device node */
	dsi_pan_node = mdss_dsi_find_panel_of_node(pdev, panel_cfg);
	if (!dsi_pan_node) {
		pr_err("%s: can't find panel node %s\n", __func__, panel_cfg);
		goto error_pan_node;
	}

	cmd_cfg_cont_splash = mdss_panel_get_boot_cfg() ? true : false;

	rc = mdss_dsi_panel_init(dsi_pan_node, ctrl_pdata, cmd_cfg_cont_splash);
	if (rc) {
		pr_err("%s: dsi panel init failed\n", __func__);
		goto error_pan_node;
	}

	rc = dsi_panel_device_register(dsi_pan_node, ctrl_pdata);
	if (rc) {
		pr_err("%s: dsi panel dev reg failed\n", __func__);
		goto error_pan_node;
	}

/* TE Signal instable lead to mdp-fence timeout or blank screen and can't wake up*/
	ctrl_pdata->cmd_clk_ln_recovery_en =
		of_property_read_bool(pdev->dev.of_node,
			"qcom,dsi-clk-ln-recovery");

	if (mdss_dsi_is_te_based_esd(ctrl_pdata)) {
		rc = devm_request_irq(&pdev->dev,
			gpio_to_irq(ctrl_pdata->disp_te_gpio),
			hw_vsync_handler, IRQF_TRIGGER_FALLING,
			"VSYNC_GPIO", ctrl_pdata);
		if (rc) {
			pr_err("TE request_irq failed.\n");
			goto error_pan_node;
		}
		disable_irq(gpio_to_irq(ctrl_pdata->disp_te_gpio));
	}
	/* optimize the screen wake up time*/
	mdss_dsi_pm_qos_add_request();
	pr_debug("%s: Dsi Ctrl->%d initialized\n", __func__, index);
	return 0;

error_pan_node:
	mdss_dsi_unregister_bl_settings(ctrl_pdata);
	of_node_put(dsi_pan_node);
	i--;
error_vreg:
	for (; i >= 0; i--)
		mdss_dsi_put_dt_vreg_data(&pdev->dev,
			&ctrl_pdata->power_data[i]);
error_no_mem:
	devm_kfree(&pdev->dev, ctrl_pdata);

	return rc;
}

static int mdss_dsi_ctrl_remove(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = platform_get_drvdata(pdev);
	int i = 0;

	if (!ctrl_pdata) {
		pr_err("%s: no driver data\n", __func__);
		return -ENODEV;
	}
	/* optimize the screen wake up time*/
	mdss_dsi_pm_qos_remove_request();
	for (i = DSI_MAX_PM - 1; i >= 0; i--) {
		if (msm_dss_config_vreg(&pdev->dev,
				ctrl_pdata->power_data[i].vreg_config,
				ctrl_pdata->power_data[i].num_vreg, 1) < 0)
			pr_err("%s: failed to de-init vregs for %s\n",
				__func__, __mdss_dsi_pm_name(i));
		mdss_dsi_put_dt_vreg_data(&pdev->dev,
			&ctrl_pdata->power_data[i]);
	}

	mfd = platform_get_drvdata(pdev);
	msm_dss_iounmap(&ctrl_pdata->mmss_misc_io);
	msm_dss_iounmap(&ctrl_pdata->phy_io);
	msm_dss_iounmap(&ctrl_pdata->ctrl_io);
	return 0;
}

struct device dsi_dev;

int mdss_dsi_retrieve_ctrl_resources(struct platform_device *pdev, int mode,
			struct mdss_dsi_ctrl_pdata *ctrl)
{
	int rc = 0;
	u32 index;

	rc = of_property_read_u32(pdev->dev.of_node, "cell-index", &index);
	if (rc) {
		dev_err(&pdev->dev,
			"%s: Cell-index not specified, rc=%d\n",
						__func__, rc);
		return rc;
	}

	if (index == 0) {
		if (mode != DISPLAY_1) {
			pr_err("%s:%d Panel->Ctrl mapping is wrong\n",
				       __func__, __LINE__);
			return -EPERM;
		}
	} else if (index == 1) {
		if (mode != DISPLAY_2) {
			pr_err("%s:%d Panel->Ctrl mapping is wrong\n",
				       __func__, __LINE__);
			return -EPERM;
		}
	} else {
		pr_err("%s:%d Unknown Ctrl mapped to panel\n",
			       __func__, __LINE__);
		return -EPERM;
	}

	rc = msm_dss_ioremap_byname(pdev, &ctrl->ctrl_io, "dsi_ctrl");
	if (rc) {
		pr_err("%s:%d unable to remap dsi ctrl resources\n",
			       __func__, __LINE__);
		return rc;
	}

	ctrl->ctrl_base = ctrl->ctrl_io.base;
	ctrl->reg_size = ctrl->ctrl_io.len;

	rc = msm_dss_ioremap_byname(pdev, &ctrl->phy_io, "dsi_phy");
	if (rc) {
		pr_err("%s:%d unable to remap dsi phy resources\n",
			       __func__, __LINE__);
		return rc;
	}

	pr_info("%s: ctrl_base=%pK ctrl_size=%x phy_base=%pK phy_size=%x\n",
		__func__, ctrl->ctrl_base, ctrl->reg_size, ctrl->phy_io.base,
		ctrl->phy_io.len);

	rc = msm_dss_ioremap_byname(pdev, &ctrl->mmss_misc_io,
		"mmss_misc_phys");
	if (rc) {
		pr_debug("%s:%d mmss_misc IO remap failed\n",
			__func__, __LINE__);
	}

	return 0;
}

static int mdss_dsi_irq_init(struct device *dev, int irq_no,
			struct mdss_dsi_ctrl_pdata *ctrl)
{
	int ret;

	ret = devm_request_irq(dev, irq_no, mdss_dsi_isr,
				IRQF_DISABLED, "DSI", ctrl);
	if (ret) {
		pr_err("msm_dsi_irq_init request_irq() failed!\n");
		return ret;
	}

	disable_irq(irq_no);
	ctrl->dsi_hw->irq_info = kzalloc(sizeof(struct irq_info), GFP_KERNEL);
	if (!ctrl->dsi_hw->irq_info) {
		pr_err("no mem to save irq info: kzalloc fail\n");
		return -ENOMEM;
	}
	ctrl->dsi_hw->irq_info->irq = irq_no;
	ctrl->dsi_hw->irq_info->irq_ena = false;

	return ret;
}

int dsi_panel_device_register(struct device_node *pan_node,
				struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	struct mipi_panel_info *mipi;
	int rc, i, len;
	struct mdss_panel_info *pinfo = &(ctrl_pdata->panel_data.panel_info);
	struct device_node *dsi_ctrl_np = NULL;
	struct platform_device *ctrl_pdev = NULL;
	const char *data;
	struct resource *res;

	mipi  = &(pinfo->mipi);

	pinfo->type =
		((mipi->mode == DSI_VIDEO_MODE)
			? MIPI_VIDEO_PANEL : MIPI_CMD_PANEL);

	rc = mdss_dsi_clk_div_config(pinfo, mipi->frame_rate);
	if (rc) {
		pr_err("%s: unable to initialize the clk dividers\n", __func__);
		return rc;
	}

	dsi_ctrl_np = of_parse_phandle(pan_node,
				"qcom,mdss-dsi-panel-controller", 0);
	if (!dsi_ctrl_np) {
		pr_err("%s: Dsi controller node not initialized\n", __func__);
		return -EPROBE_DEFER;
	}

	ctrl_pdev = of_find_device_by_node(dsi_ctrl_np);

	rc = mdss_dsi_regulator_init(ctrl_pdev);
	if (rc) {
		pr_err("%s: failed to init regulator, rc=%d\n",
						__func__, rc);
		return rc;
	}

	data = of_get_property(ctrl_pdev->dev.of_node,
		"qcom,platform-strength-ctrl", &len);
	if ((!data) || (len != 2)) {
		pr_err("%s:%d, Unable to read Phy Strength ctrl settings\n",
			__func__, __LINE__);
		return -EINVAL;
	}
	pinfo->mipi.dsi_phy_db.strength[0] = data[0];
	pinfo->mipi.dsi_phy_db.strength[1] = data[1];

	pinfo->mipi.dsi_phy_db.reg_ldo_mode = of_property_read_bool(
		ctrl_pdev->dev.of_node, "qcom,regulator-ldo-mode");
	/*Here to prase the dtsi and get the parameter setting.*/
	if(get_mipi_regulator_mode() == REGULATOR_DCDC_MODE)
	{
        pinfo->mipi.dsi_phy_db.reg_ldo_mode = 0;
		data = of_get_property(ctrl_pdev->dev.of_node,
			"qcom,platform-regulator-settings-dcdc", &len);
	}
	else
	{
		data = of_get_property(ctrl_pdev->dev.of_node,
			"qcom,platform-regulator-settings", &len);
	}
	LCD_LOG_INFO("%s:pinfo->mipi.dsi_phy_db.reg_ldo_mode=%d \n",__func__,pinfo->mipi.dsi_phy_db.reg_ldo_mode);
	if ((!data) || (len != 7)) {
		pr_err("%s:%d, Unable to read Phy regulator settings\n",
			__func__, __LINE__);
		return -EINVAL;
	}
	for (i = 0; i < len; i++) {
		pinfo->mipi.dsi_phy_db.regulator[i]
			= data[i];
	}

	data = of_get_property(ctrl_pdev->dev.of_node,
		"qcom,platform-bist-ctrl", &len);
	if ((!data) || (len != 6)) {
		pr_err("%s:%d, Unable to read Phy Bist Ctrl settings\n",
			__func__, __LINE__);
		return -EINVAL;
	}
	for (i = 0; i < len; i++) {
		pinfo->mipi.dsi_phy_db.bistctrl[i]
			= data[i];
	}

	data = of_get_property(ctrl_pdev->dev.of_node,
		"qcom,platform-lane-config", &len);
	if ((!data) || (len != 45)) {
		pr_err("%s:%d, Unable to read Phy lane configure settings\n",
			__func__, __LINE__);
		return -EINVAL;
	}
	for (i = 0; i < len; i++) {
		pinfo->mipi.dsi_phy_db.lanecfg[i] =
			data[i];
	}

	rc = of_property_read_u32(ctrl_pdev->dev.of_node,
			"qcom,mmss-ulp-clamp-ctrl-offset",
			&ctrl_pdata->ulps_clamp_ctrl_off);
	if (!rc) {
		rc = of_property_read_u32(ctrl_pdev->dev.of_node,
				"qcom,mmss-phyreset-ctrl-offset",
				&ctrl_pdata->ulps_phyrst_ctrl_off);
	}
	if (rc && pinfo->ulps_feature_enabled) {
		pr_err("%s: dsi ulps clamp register settings missing\n",
				__func__);
		return -EINVAL;
	}

	ctrl_pdata->cmd_sync_wait_broadcast = of_property_read_bool(
		pan_node, "qcom,cmd-sync-wait-broadcast");

	ctrl_pdata->cmd_sync_wait_trigger = of_property_read_bool(
		pan_node, "qcom,cmd-sync-wait-trigger");

	pr_debug("%s: cmd_sync_wait_enable=%d trigger=%d\n", __func__,
				ctrl_pdata->cmd_sync_wait_broadcast,
				ctrl_pdata->cmd_sync_wait_trigger);

	pinfo->panel_max_fps = mdss_panel_get_framerate(pinfo);
	pinfo->panel_max_vtotal = mdss_panel_get_vtotal(pinfo);

	/*
	 * If disp_en_gpio has been set previously (disp_en_gpio > 0)
	 *  while parsing the panel node, then do not override it
	 */
	if (ctrl_pdata->disp_en_gpio <= 0) {
		ctrl_pdata->disp_en_gpio = of_get_named_gpio(
			ctrl_pdev->dev.of_node,
			"qcom,platform-enable-gpio", 0);

		if (!gpio_is_valid(ctrl_pdata->disp_en_gpio))
			pr_err("%s:%d, Disp_en gpio not specified\n",
					__func__, __LINE__);
	}

	ctrl_pdata->disp_te_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
		"qcom,platform-te-gpio", 0);

	if (!gpio_is_valid(ctrl_pdata->disp_te_gpio))
		pr_err("%s:%d, TE gpio not specified\n",
						__func__, __LINE__);

	ctrl_pdata->bklt_en_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
		"qcom,platform-bklight-en-gpio", 0);
	if (!gpio_is_valid(ctrl_pdata->bklt_en_gpio))
		pr_info("%s: bklt_en gpio not specified\n", __func__);

	ctrl_pdata->rst_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
			 "qcom,platform-reset-gpio", 0);
	if (!gpio_is_valid(ctrl_pdata->rst_gpio))
		pr_err("%s:%d, reset gpio not specified\n",
						__func__, __LINE__);
	/* Modify JDI tp/lcd power on/off to reduce power consumption */

/*T2 10 LCD on*/
#ifdef CONFIG_HUAWEI_LCD
	/*huawei PDU-DRV add begin*/
	pr_info("%s: which_product_pad = %d  \n", __func__, ctrl_pdata->which_product_pad);
	pr_info("%s: hw_product_pad = %d  \n", __func__, ctrl_pdata->hw_product_pad);
	if (1 == ctrl_pdata->hw_product_pad) {
		if (1 == ctrl_pdata->which_product_pad){
			ctrl_pdata->disp_vsn_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
				"qcom,platform-vsn-gpio102", 0);
			if (!gpio_is_valid(ctrl_pdata->disp_vsn_gpio))
				pr_err("%s: qcom,platform-vsn-gpio102 not specified\n", __func__);

			ctrl_pdata->disp_vsp_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
				"qcom,platform-vsp-gpio100", 0);
			if (!gpio_is_valid(ctrl_pdata->disp_vsp_gpio))
				pr_err("%s: qcom,platform-vsp-gpio100 not specified\n", __func__);

			/*
			   reset gpio use ctrl_pdata->rst_gpio, rst_gpio
			*/

			ctrl_pdata->disp_vled_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
				"qcom,platform-vled-gpio97", 0);
			if (!gpio_is_valid(ctrl_pdata->disp_vled_gpio))
				pr_err("%s: qcom,platform-vled-gpio97 not specified\n", __func__);

			ctrl_pdata->disp_bl_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
				"qcom,platform-bl-gpio109", 0);
			if (!gpio_is_valid(ctrl_pdata->disp_bl_gpio))
				pr_err("%s: qcom,platform-bl-gpio109 not specified\n", __func__);

			ctrl_pdata->disp_vcc_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
				"qcom,platform-vcc-gpio2", 0);
			if (!gpio_is_valid(ctrl_pdata->disp_vcc_gpio))
				pr_err("%s: qcom,platform-vcc-gpio2 not specified\n", __func__);
		} else if (2 == ctrl_pdata->which_product_pad) {
			ctrl_pdata->disp_vled_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
				"qcom,platform-vled-gpio97", 0);
			if (!gpio_is_valid(ctrl_pdata->disp_vled_gpio))
				pr_err("%s: qcom,platform-vled-gpio 109 not specified\n", __func__);

			ctrl_pdata->disp_bl_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
				"qcom,platform-bl-gpio109", 0);
			if (!gpio_is_valid(ctrl_pdata->disp_bl_gpio))
				pr_err("%s: qcom,platform-bl-gpio 3 not specified\n", __func__);

			ctrl_pdata->disp_vcc_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
				"qcom,platform-vcc-gpio2", 0);
			if (!gpio_is_valid(ctrl_pdata->disp_vcc_gpio))
				pr_err("%s: qcom,platform-vcc-gpio2 not specified\n", __func__);
			LCD_LOG_INFO("%s: gpio_vled = %d, gpio_bl = %d, gpio_vcc = %d!\n", __func__,\
				ctrl_pdata->disp_vled_gpio, ctrl_pdata->disp_bl_gpio, ctrl_pdata->disp_vcc_gpio);
		} else {
			ctrl_pdata->disp_bl_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
				"huawei,platform-power-blk", 0);
			if (!gpio_is_valid(ctrl_pdata->disp_bl_gpio)) {
				pr_err("%s:%d, disp_power_backlight  not specified\n",
					__func__, __LINE__);
			}
			ctrl_pdata->disp_vcc_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
					"huawei,platform-power-panel", 0);
			if (!gpio_is_valid(ctrl_pdata->disp_vcc_gpio)) {
				pr_err("%s:%d, disp_power_panel  not specified\n",
					__func__, __LINE__);
			}
			ctrl_pdata->disp_vled_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
					 "huawei,platform-enable-vled", 0);
			if (!gpio_is_valid(ctrl_pdata->disp_vled_gpio)) {
				pr_err("%s:%d, vled enable gpio not specified\n",
					__func__, __LINE__);
			}

			pr_info("%s:rst_gpio:%d, disp_en_gpio_vled:%d, disp_power_panel:%d, disp_power_backlight:%d\n",
						__func__, ctrl_pdata->rst_gpio,
						ctrl_pdata->disp_vled_gpio,ctrl_pdata->disp_vcc_gpio,
						ctrl_pdata->disp_bl_gpio);
		}
		rc =  hw_request_gpios(ctrl_pdata);
		if (rc){
			pr_err("gpio request failed\n");
		}
	}else{

		ctrl_pdata->disp_vsn_gpio = -1;
		ctrl_pdata->disp_vsp_gpio = -1;
		ctrl_pdata->rst_gpio = -1;
		ctrl_pdata->disp_vled_gpio = -1;
		ctrl_pdata->disp_bl_gpio = -1;
		ctrl_pdata->disp_vcc_gpio = -1;
	}
	/*huawei PDU-DRV add end*/
#endif
	ctrl_pdata->tp_vci_gpio = of_get_named_gpio(ctrl_pdev->dev.of_node,
			 "qcom,platform-tp-vci-gpio", 0);
	if (!gpio_is_valid(ctrl_pdata->tp_vci_gpio))
		pr_err("%s:%d, tp vci gpio not specified\n",
						__func__, __LINE__);

	if (pinfo->mode_gpio_state != MODE_GPIO_NOT_VALID) {

		ctrl_pdata->mode_gpio = of_get_named_gpio(
					ctrl_pdev->dev.of_node,
					"qcom,platform-mode-gpio", 0);
		if (!gpio_is_valid(ctrl_pdata->mode_gpio))
			pr_info("%s:%d, mode gpio not specified\n",
							__func__, __LINE__);
	} else {
		ctrl_pdata->mode_gpio = -EINVAL;
	}

	ctrl_pdata->timing_db_mode = of_property_read_bool(
		ctrl_pdev->dev.of_node, "qcom,timing-db-mode");

	if (mdss_dsi_clk_init(ctrl_pdev, ctrl_pdata)) {
		pr_err("%s: unable to initialize Dsi ctrl clks\n", __func__);
		return -EPERM;
	}

	rc = mdss_dsi_pll_1_clk_init(ctrl_pdev, ctrl_pdata);
	if (rc)
		pr_err("PLL 1 Clock's did not register\n");

	if (pinfo->dynamic_fps &&
			pinfo->dfps_update == DFPS_IMMEDIATE_CLK_UPDATE_MODE) {
		if (mdss_dsi_shadow_clk_init(ctrl_pdev, ctrl_pdata)) {
			pr_err("unable to initialize shadow ctrl clks\n");
			return -EPERM;
		}
	}

	if (mdss_dsi_retrieve_ctrl_resources(ctrl_pdev,
					     pinfo->pdest,
					     ctrl_pdata)) {
		pr_err("%s: unable to get Dsi controller res\n", __func__);
		return -EPERM;
	}

	ctrl_pdata->panel_data.event_handler = mdss_dsi_event_handler;

	if (ctrl_pdata->status_mode == ESD_REG ||
			ctrl_pdata->status_mode == ESD_REG_NT35596)
		ctrl_pdata->check_status = mdss_dsi_reg_status_check;
	else if (ctrl_pdata->status_mode == ESD_BTA)
		ctrl_pdata->check_status = mdss_dsi_bta_status_check;

	if (ctrl_pdata->status_mode == ESD_MAX) {
		pr_err("%s: Using default BTA for ESD check\n", __func__);
		ctrl_pdata->check_status = mdss_dsi_bta_status_check;
	}
	if (ctrl_pdata->bklt_ctrl == BL_PWM)
		mdss_dsi_panel_pwm_cfg(ctrl_pdata);

	mdss_dsi_ctrl_init(&ctrl_pdev->dev, ctrl_pdata);

	ctrl_pdata->dsi_irq_line = of_property_read_bool(
				ctrl_pdev->dev.of_node, "qcom,dsi-irq-line");

	if (ctrl_pdata->dsi_irq_line) {
		/* DSI has it's own irq line */
		res = platform_get_resource(ctrl_pdev, IORESOURCE_IRQ, 0);
		if (!res || res->start == 0) {
			pr_err("%s:%d unable to get the MDSS irq resources\n",
							__func__, __LINE__);
			return -ENODEV;
		}
		rc = mdss_dsi_irq_init(&ctrl_pdev->dev, res->start, ctrl_pdata);
		if (rc) {
			dev_err(&ctrl_pdev->dev, "%s: failed to init irq\n",
							__func__);
			return rc;
		}
	}

	ctrl_pdata->pclk_rate = mipi->dsi_pclk_rate;
	ctrl_pdata->byte_clk_rate = pinfo->clk_rate / 8;
	pr_debug("%s: pclk=%d, bclk=%d\n", __func__,
			ctrl_pdata->pclk_rate, ctrl_pdata->byte_clk_rate);

	ctrl_pdata->ctrl_state = CTRL_STATE_UNKNOWN;

	/*
	 * If ULPS during suspend is enabled, add an extra vote for the
	 * DSI CTRL power module. This keeps the regulator always enabled.
	 * This is needed for the DSI PHY to maintain ULPS state during
	 * suspend also.
	 */
	if (pinfo->ulps_suspend_enabled) {
		rc = msm_dss_enable_vreg(
			ctrl_pdata->power_data[DSI_CTRL_PM].vreg_config,
			ctrl_pdata->power_data[DSI_CTRL_PM].num_vreg, 1);
		if (rc) {
			pr_err("%s: failed to enable vregs for DSI_CTRL_PM\n",
				__func__);
			return rc;
		}
	}

	if (pinfo->cont_splash_enabled) {
		rc = mdss_dsi_panel_power_ctrl(&(ctrl_pdata->panel_data),
			MDSS_PANEL_POWER_ON);
		if (rc) {
			pr_err("%s: Panel power on failed\n", __func__);
			return rc;
		}

		pinfo->blank_state = MDSS_PANEL_BLANK_UNBLANK;
		mdss_dsi_clk_ctrl(ctrl_pdata, DSI_ALL_CLKS, 1);
		ctrl_pdata->ctrl_state |=
			(CTRL_STATE_PANEL_INIT | CTRL_STATE_MDP_ACTIVE);
	} else {
		pinfo->panel_power_state = MDSS_PANEL_POWER_OFF;
	}

	rc = mdss_register_panel(ctrl_pdev, &(ctrl_pdata->panel_data));
	if (rc) {
		pr_err("%s: unable to register MIPI DSI panel\n", __func__);
		return rc;
	}

	if (pinfo->pdest == DISPLAY_1) {
		mdss_debug_register_io("dsi0_ctrl", &ctrl_pdata->ctrl_io);
		mdss_debug_register_io("dsi0_phy", &ctrl_pdata->phy_io);
		ctrl_pdata->ndx = 0;
	} else {
		mdss_debug_register_io("dsi1_ctrl", &ctrl_pdata->ctrl_io);
		mdss_debug_register_io("dsi1_phy", &ctrl_pdata->phy_io);
		ctrl_pdata->ndx = 1;
	}

	panel_debug_register_base("panel",
		ctrl_pdata->ctrl_base, ctrl_pdata->reg_size);

	pr_debug("%s: Panel data initialized\n", __func__);
	return 0;
}

static const struct of_device_id mdss_dsi_ctrl_dt_match[] = {
	{.compatible = "qcom,mdss-dsi-ctrl"},
	{}
};
MODULE_DEVICE_TABLE(of, mdss_dsi_ctrl_dt_match);

static struct platform_driver mdss_dsi_ctrl_driver = {
	.probe = mdss_dsi_ctrl_probe,
	.remove = mdss_dsi_ctrl_remove,
	.shutdown = NULL,
	.driver = {
		.name = "mdss_dsi_ctrl",
		.of_match_table = mdss_dsi_ctrl_dt_match,
	},
};

static int mdss_dsi_register_driver(void)
{
	return platform_driver_register(&mdss_dsi_ctrl_driver);
}

static int __init mdss_dsi_driver_init(void)
{
	int ret;

	ret = mdss_dsi_register_driver();
	if (ret) {
		pr_err("mdss_dsi_register_driver() failed!\n");
		return ret;
	}

	return ret;
}
module_init(mdss_dsi_driver_init);

static void __exit mdss_dsi_driver_cleanup(void)
{
	platform_driver_unregister(&mdss_dsi_ctrl_driver);
}
module_exit(mdss_dsi_driver_cleanup);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DSI controller driver");
MODULE_AUTHOR("Chandan Uddaraju <chandanu@codeaurora.org>");
