
/* Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/printk.h>
#include <linux/list.h>
#include <linux/pinctrl/consumer.h>

/* #define CONFIG_PMIC_FLASH_DEBUG */
#undef CDBG
#define CONFIG_PMIC_FLASH_DEBUG
#ifdef CONFIG_PMIC_FLASH_DEBUG
#define CDBG(fmt, args...) pr_err(fmt, ##args)
#else
#define CDBG(fmt, args...) do { } while (0)
#endif

#define LED_PMIC_FLASH_DRIVER_NAME	"qcom,leds-pmic-flash"

#define LED_PMIC_CDEV_NUM 2
#define PMIC_LEDS  2

enum flash_led_type {
	FLASH = 0,
	TORCH,
	NONE,
};

struct pmic_flash_led_cdev {
	struct led_classdev cdev;
	u16	duration;
	u16	max_current;
	u16 prgm_current;
	struct pmic_flash_ctrl * parent;
	u8 type;
};

struct pmic_flash_ctrl {
	struct platform_device *pdev;
	struct pmic_flash_led_cdev flash_cdev;
	struct pmic_flash_led_cdev torch_cdev;

	/* Flash */
	uint32_t flash_num_sources;
	const char *flash_trigger_name[PMIC_LEDS];
	struct led_trigger *flash_trigger[PMIC_LEDS];
	uint32_t flash_op_current[PMIC_LEDS];
	uint32_t flash_max_current[PMIC_LEDS];
	uint32_t flash_max_duration[PMIC_LEDS];

	/* Torch */
	uint32_t torch_num_sources;
	const char *torch_trigger_name[PMIC_LEDS];
	struct led_trigger *torch_trigger[PMIC_LEDS];
	uint32_t torch_op_current[PMIC_LEDS];
	uint32_t torch_max_current[PMIC_LEDS];
};

static struct of_device_id led_pmic_flash_of_match[] = {
	{.compatible = LED_PMIC_FLASH_DRIVER_NAME,},
	{},
};

static enum led_brightness led_pmic_brightness_get(struct led_classdev *led_cdev)
{
	pr_err("led_pmic_brightness_get: brightness:%d\n", led_cdev->brightness);
	return led_cdev->brightness;
}

static void led_pmic_brightness_set(struct led_classdev *led_cdev,
						enum led_brightness value)
{
	int curr, i = 0;
	struct pmic_flash_led_cdev *flash_led_cdev;
	struct pmic_flash_ctrl * flash_ctrl;

	pr_err("led_pmic_brightness_set: brightness value:%d\n", value);
	flash_led_cdev = container_of(led_cdev, struct pmic_flash_led_cdev, cdev);
	if (value < LED_OFF) {
		pr_err("Invalid brightness value\n");
		return;
	}

	flash_ctrl = flash_led_cdev->parent;

	if (value > flash_led_cdev->cdev.max_brightness)
		value = flash_led_cdev->cdev.max_brightness;

	flash_led_cdev->cdev.brightness = value;

	pr_err("led_pmic_brightness_set: brightness set:%d\n", led_cdev->brightness);

	
	if (flash_led_cdev->type == FLASH)
	{
		//set flash current, turn on flash triggers
		curr = value / flash_ctrl->flash_num_sources;
		for (i = 0; i < flash_ctrl->flash_num_sources; i++)
			if (flash_ctrl->flash_trigger[i])
				led_trigger_event(flash_ctrl->flash_trigger[i], curr);
	}
	else
	{
		//set torch current, turn on torch triggers
		curr = value / flash_ctrl->torch_num_sources;
		for (i = 0; i < flash_ctrl->torch_num_sources; i++)
			if (flash_ctrl->torch_trigger[i])
				led_trigger_event(flash_ctrl->torch_trigger[i], curr);
	}
	
	return;
}

static int led_pmic_flash_parse_each_cdev_dt(struct device_node *of_node,
					struct pmic_flash_ctrl *flash_ctrl)
{
	struct pmic_flash_led_cdev * flash_led_cdev = NULL;
	const char *temp_string;
	int rc = 0, i = 0;
	u32 max_curr = 0;
	u32 val;

	rc = of_property_read_string(of_node, "label", &temp_string);
	if (!rc) {
		if (strcmp(temp_string, "flash") == 0)
		{
			flash_led_cdev = &flash_ctrl->flash_cdev;
			flash_led_cdev->type = FLASH;
			for (i = 0; i < flash_ctrl->flash_num_sources; i++)
			{
				if (flash_ctrl->flash_trigger[i])
					max_curr += flash_ctrl->flash_max_current[i];
			}
		}
		else if (strcmp(temp_string, "torch") == 0)
		{
			flash_led_cdev = &flash_ctrl->torch_cdev;
			flash_led_cdev->type = TORCH;
			for (i = 0; i < flash_ctrl->torch_num_sources; i++)
			{
				if (flash_ctrl->torch_trigger[i])
					max_curr += flash_ctrl->torch_max_current[i];
			}
		}
		else {
			pr_err("Wrong flash LED type\n");
			return -EINVAL;
		}
	} else if (rc < 0) {
		pr_err("Unable to read flash type\n");
		return rc;
	}

	rc = of_property_read_string(of_node, "qcom,led-name",
					&flash_led_cdev->cdev.name);
	if (rc < 0) {
		rc = of_property_read_string(of_node, "linux,name",
						&flash_led_cdev->cdev.name);	
		if (rc < 0) {
			pr_err("Unable to read flash name\n");
			return rc;
		}
	}

	rc = of_property_read_string(of_node, "qcom,default-led-trigger",
			&flash_led_cdev->cdev.default_trigger);
	if (rc < 0) {
		rc = of_property_read_string(of_node, "linux,default-trigger",
						&flash_led_cdev->cdev.default_trigger);	
		if (rc < 0) {
			pr_err("Unable to read trigger name\n");
			return rc;
		}
	}
	
	rc = of_property_read_u32(of_node, "qcom,max-current", &val);
	if (!rc) {
		if (val > max_curr)
			val = max_curr;
		flash_led_cdev->max_current = (u16)val;
		flash_led_cdev->cdev.max_brightness = val;
	} else if (rc < 0) {
		pr_err("Unable to read max current\n");
		return rc;
	}

	rc = of_property_read_u32(of_node, "qcom,current", &val);
	if (!rc) {
		if (val > max_curr)
			val = max_curr;
		flash_led_cdev->prgm_current = (u16)val;
	} else if (rc != -EINVAL) {
		pr_err("Unable to read current settings\n");
		return rc;
	}
	else
	{
		pr_err("Set default current\n");
		flash_led_cdev->prgm_current = (flash_led_cdev->max_current / 2);
	}

	rc = of_property_read_u32(of_node, "qcom,duration", &val);
	if (!rc)
		flash_led_cdev->duration = (u16)val;
	else if (rc != -EINVAL) {
		pr_err("Unable to read clamp current\n");
		return rc;
	}
	else
	{
		if (flash_led_cdev->type == FLASH)
		{
			pr_err("Set default flash duratioin\n");
			flash_led_cdev->duration = 1050;
		}
		else
		{
			pr_err("No duration needed for torch type\n");
			flash_led_cdev->duration = 0;
		}
	}

	flash_led_cdev->cdev.brightness_set = led_pmic_brightness_set;
	flash_led_cdev->cdev.brightness_get = led_pmic_brightness_get;
	flash_led_cdev->parent = flash_ctrl;
	return 0;
}

static int led_pmic_flash_get_pmic_source_info(struct device_node *of_node,
					struct pmic_flash_ctrl *flash_ctrl)
{
	int rc = 0, count = 0, i = 0;
	struct device_node *flash_src_node = NULL;
	struct device_node *torch_src_node = NULL;

	if (of_get_property(of_node, "qcom,pmic-flash-source", &count)) {
		count /= sizeof(uint32_t);
		CDBG("count %d\n", count);
		if (count > PMIC_LEDS) {
			pr_err("invalid count\n");
			return -EINVAL;
		}
		flash_ctrl->flash_num_sources = count;
		CDBG("%s:%d flash_num_sources = %d",
			__func__, __LINE__, flash_ctrl->flash_num_sources);
		for (i = 0; i < count; i++) {
			flash_src_node = of_parse_phandle(of_node,
				"qcom,pmic-flash-source", i);
			if (!flash_src_node) {
				pr_err("flash_src_node NULL\n");
				return -EINVAL;
			}

			rc = of_property_read_string(flash_src_node,
				"qcom,default-led-trigger",
				&flash_ctrl->flash_trigger_name[i]);
			if (rc < 0) {
				rc = of_property_read_string(flash_src_node,
					"linux,default-trigger",
					&flash_ctrl->flash_trigger_name[i]);
				if (rc < 0) {
					pr_err("default-trigger read failed\n");
					of_node_put(flash_src_node);
					return rc;
				}
			}

			CDBG("default trigger %s\n",
				flash_ctrl->flash_trigger_name[i]);

			/* Read duration */
			rc = of_property_read_u32(flash_src_node,
				"qcom,duration",
				&flash_ctrl->flash_max_duration[i]);
			if (rc < 0) {
				pr_err("duration: read failed\n");
				of_node_put(flash_src_node);
				return rc;
			}

			/* Read operational-current */
			rc = of_property_read_u32(flash_src_node,
				"qcom,current",
				&flash_ctrl->flash_op_current[i]);
			if (rc < 0) {
				pr_err("current: read failed\n");
				of_node_put(flash_src_node);
				return rc;
			}

			/* Read max-current */
			rc = of_property_read_u32(flash_src_node,
				"qcom,max-current",
				&flash_ctrl->flash_max_current[i]);
			if (rc < 0) {
				pr_err("current: read failed\n");
				of_node_put(flash_src_node);
				return rc;
			}

			of_node_put(flash_src_node);

			CDBG("max_current[%d] %d\n",
				i, flash_ctrl->flash_op_current[i]);

			led_trigger_register_simple(
				flash_ctrl->flash_trigger_name[i],
				&flash_ctrl->flash_trigger[i]);
		}
	}

	if (of_get_property(of_node, "qcom,pmic-torch-source", &count)) {
		count /= sizeof(uint32_t);
		CDBG("count %d\n", count);
		if (count > PMIC_LEDS) {
			pr_err("invalid count\n");
			return -EINVAL;
		}
		flash_ctrl->torch_num_sources = count;
		CDBG("%s:%d torch_num_sources = %d",
			__func__, __LINE__, flash_ctrl->torch_num_sources);
		for (i = 0; i < count; i++) {
			torch_src_node = of_parse_phandle(of_node,
				"qcom,pmic-torch-source", i);
			if (!torch_src_node) {
				pr_err("torch_src_node NULL\n");
				return -EINVAL;
			}

			rc = of_property_read_string(torch_src_node,
				"qcom,default-led-trigger",
				&flash_ctrl->torch_trigger_name[i]);
			if (rc < 0) {
				rc = of_property_read_string(torch_src_node,
					"linux,default-trigger",
					&flash_ctrl->torch_trigger_name[i]);
				if (rc < 0) {
					pr_err("default-trigger read failed\n");
					of_node_put(torch_src_node);
					return rc;
				}
			}

			CDBG("default trigger %s\n",
				flash_ctrl->torch_trigger_name[i]);

			/* Read operational-current */
			rc = of_property_read_u32(torch_src_node,
				"qcom,current",
				&flash_ctrl->torch_op_current[i]);
			if (rc < 0) {
				pr_err("current: read failed\n");
				of_node_put(torch_src_node);
				return rc;
			}

			/* Read max-current */
			rc = of_property_read_u32(torch_src_node,
				"qcom,max-current",
				&flash_ctrl->torch_max_current[i]);
			if (rc < 0) {
				pr_err("current: read failed\n");
				of_node_put(torch_src_node);
				return rc;
			}

			of_node_put(torch_src_node);

			CDBG("max_current[%d] %d\n",
				i, flash_ctrl->torch_op_current[i]);

			led_trigger_register_simple(
				flash_ctrl->torch_trigger_name[i],
				&flash_ctrl->torch_trigger[i]);
		}
	}

	return 0;
}

static int led_pmic_flash_get_dt_data(struct device_node *of_node,
					struct pmic_flash_ctrl *flash_ctrl)
{
	struct device_node *temp;
	int rc = 0, num_cdev = 0;
	rc = led_pmic_flash_get_pmic_source_info(of_node, flash_ctrl);
	if (rc < 0)
		return rc;

	temp = NULL;
	while ((temp = of_get_next_child(of_node, temp)))
		num_cdev++;

	if (num_cdev != LED_PMIC_CDEV_NUM)
	{
		pr_err("Wrong led cdev number: %d except: %d\n", num_cdev, LED_PMIC_CDEV_NUM);
		return -EINVAL;
	}

	for_each_child_of_node(of_node, temp) {
		rc = led_pmic_flash_parse_each_cdev_dt(temp, flash_ctrl);
		if (rc)
			return rc;
	}
	return 0;
}

int led_pmic_flash_probe(struct platform_device *pdev)
{
	struct pmic_flash_ctrl * flash_ctrl = NULL;
	struct device_node *node = pdev->dev.of_node;
	int rc;

	if (!node) {
		pr_err("of_node NULL\n");
		return -EINVAL;
	}

	flash_ctrl = devm_kzalloc(&pdev->dev, sizeof(struct pmic_flash_ctrl),
				 GFP_KERNEL);
	if (flash_ctrl == NULL) {
		pr_err("%s:%d Unable to allocate memory\n",
			__func__, __LINE__);
		return -ENOMEM;
	}

	flash_ctrl->pdev = pdev;

	rc = led_pmic_flash_get_dt_data(node, flash_ctrl);
	if (rc) {
		pr_err("%s: Failed to get dt data. rc = %d\n",
			__func__, rc);
		goto error;
	}

	rc = led_classdev_register(&pdev->dev, &flash_ctrl->flash_cdev.cdev);
	if (rc) {
		pr_err("%s: Failed to register led dev. rc = %d\n",
			__func__, rc);
		goto error;
	}
	rc = led_classdev_register(&pdev->dev, &flash_ctrl->torch_cdev.cdev);
	if (rc) {
		pr_err("%s: Failed to register led dev. rc = %d\n",
			__func__, rc);
		led_classdev_unregister(&flash_ctrl->flash_cdev.cdev);
		goto error;
	}

	platform_set_drvdata(pdev, flash_ctrl);

	pr_err("%s:probe successfully!\n", __func__);
	return 0;

error:
	devm_kfree(&pdev->dev, flash_ctrl);
	return rc;
}

int led_pmic_flash_remove(struct platform_device *pdev)
{
	struct pmic_flash_ctrl * flash_ctrl =
		(struct pmic_flash_ctrl *)platform_get_drvdata(pdev);

	led_classdev_unregister(&flash_ctrl->flash_cdev.cdev);
	led_classdev_unregister(&flash_ctrl->torch_cdev.cdev);
		
	devm_kfree(&pdev->dev, flash_ctrl);
	return 0;
}

static struct platform_driver led_pmic_flash_driver = {
	.probe = led_pmic_flash_probe,
	.remove = led_pmic_flash_remove,
	.driver = {
		   .name = LED_PMIC_FLASH_DRIVER_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = led_pmic_flash_of_match,
		   }
};

static int __init led_pmic_flash_init(void)
{
	return platform_driver_register(&led_pmic_flash_driver);
}

static void __exit led_pmic_flash_exit(void)
{
	return platform_driver_unregister(&led_pmic_flash_driver);
}

late_initcall(led_pmic_flash_init);
module_exit(led_pmic_flash_exit);

MODULE_DESCRIPTION("PMIC FLASH LEDs combine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("leds:leds-pmic-flash");
