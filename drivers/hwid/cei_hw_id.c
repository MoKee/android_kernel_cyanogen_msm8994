#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/seq_file.h>

#include <hwid/cei_hw_id.h>

#define HDMI_GPIO_DEF_REMOVE_DONE
#define TERT_I2S_GPIO_DEF_REMOVE_DONE
// gpio 75 & 77 conflict

struct hwid_gpio_data {
    #ifdef HDMI_GPIO_DEF_REMOVE_DONE
	int proj_id1_gpio;
    #endif
	int proj_id2_gpio;
	int hw_id1_gpio;
    #ifdef TERT_I2S_GPIO_DEF_REMOVE_DONE
	int hw_id2_gpio;
	int hw_id3_gpio;
    #endif	
	int ddr_id_gpio;
	int touch_id_gpio;
	struct pinctrl *hwid_pinctrl;
	struct pinctrl_state *hwid_pinctrl_act;
	struct pinctrl_state	*hwid_pinctrl_sus;
};
static struct hwid_gpio_data cei_hwid_gpio_data;

struct hwid_info {
	enum cei_hw_type hw_id;
	enum cei_project_type project_id;
	enum cei_ddr_type ddr_id;
	enum cei_touch_type touch_id;
};
static struct hwid_info cei_hwid_info;

static const char cei_hw_type_str[][CEI_HWID_STRING_LEN] = 
	{"EVT1", "EVT2", "DVT1", "DVT2", "PVT", "INVALID"};

static const char cei_ddr_type_str[][CEI_HWID_STRING_LEN] = 
	{"MAIN_SRC", "2ND_SRC", "INVALID"};

static const char cei_project_type_str[][CEI_HWID_STRING_LEN] = 
	{"PM97", "PM98", "PM99", "INVALID"};

static const char cei_touch_type_str[][CEI_HWID_STRING_LEN] = 
	{"MAIN_SRC", "2ND_SRC", "INVALID"};

/*
 *	HWID device tree node is defined in msm8994-mtp.dtsi and gpio defined in msm8994-pinctrl.dtsi
 */

static int cei_hwid_parse_dt_gpio(struct device *dev)
{
	struct device_node *node = dev->of_node;
	struct hwid_gpio_data *gdata = &cei_hwid_gpio_data; 
	int projid=0, hwid=0, ddrid=0, touchid=0, gpio_is_invalid=0;
	
	/* Get pinctrl & gpio from DT, initial hwid gpio data*/
	gdata->hwid_pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR_OR_NULL(gdata->hwid_pinctrl)) {
		printk(KERN_ERR "%s: Failed to get pinctrl\n", __func__);
		goto gpio_pinctrl_err;
	}
	
	gdata->hwid_pinctrl_act =
			pinctrl_lookup_state(gdata->hwid_pinctrl, "cei_hwid_pinctrl_active");
	if (IS_ERR_OR_NULL(gdata->hwid_pinctrl_act)) {
		printk(KERN_ERR "%s: Unable to get hwid_gpio_act state handle\n", __func__);
		goto gpio_pinctrl_err;
	}

	gdata->hwid_pinctrl_sus = 
			pinctrl_lookup_state(gdata->hwid_pinctrl, "cei_hwid_pinctrl_suspend");
	if (IS_ERR_OR_NULL(gdata->hwid_pinctrl_sus)) {
		printk(KERN_ERR "%s: Unable to get hwid_pinctrl_sus state handle\n", __func__);
		goto gpio_pinctrl_err;		
	}
#ifdef HDMI_GPIO_DEF_REMOVE_DONE
	gdata->proj_id1_gpio = of_get_named_gpio(node, "cei,project_id1_gpio", 0);
#endif
	gdata->proj_id2_gpio = of_get_named_gpio(node, "cei,project_id2_gpio", 0);
	gdata->hw_id1_gpio   = of_get_named_gpio(node, "cei,hw_id1_gpio", 0);
#ifdef TERT_I2S_GPIO_DEF_REMOVE_DONE
	gdata->hw_id2_gpio   = of_get_named_gpio(node, "cei,hw_id2_gpio", 0);
	gdata->hw_id3_gpio   = of_get_named_gpio(node, "cei,hw_id3_gpio", 0);
#endif	
	gdata->ddr_id_gpio   = of_get_named_gpio(node, "cei,ddr_id-gpio", 0);
	gdata->touch_id_gpio = of_get_named_gpio(node, "cei,touch_id-gpio", 0);

#ifdef HDMI_GPIO_DEF_REMOVE_DONE
	if (!gpio_is_valid(gdata->proj_id1_gpio)) {
		printk(KERN_ERR "gdata->proj_id1_gpio %d is invalid.\n", gdata->proj_id1_gpio);
		gpio_is_invalid = 1;
	}
#endif	
	if (!gpio_is_valid(gdata->proj_id2_gpio)) {
		printk(KERN_ERR "gdata->proj_id2_gpio %d is invalid.\n", gdata->proj_id2_gpio);
		gpio_is_invalid = 1;
	}
	if (!gpio_is_valid(gdata->hw_id1_gpio)) {
		printk(KERN_ERR "gdata->hw_id1_gpio %d is invalid.\n", gdata->hw_id1_gpio);
		gpio_is_invalid = 1;
	}
#ifdef TERT_I2S_GPIO_DEF_REMOVE_DONE
	if (!gpio_is_valid(gdata->hw_id2_gpio)) {
		printk(KERN_ERR "gdata->hw_id2_gpio %d is invalid.\n", gdata->hw_id2_gpio);
		gpio_is_invalid = 1;
	}
	if (!gpio_is_valid(gdata->hw_id3_gpio)) {
		printk(KERN_ERR "gdata->hw_id3_gpio %d is invalid.\n", gdata->hw_id3_gpio);
		gpio_is_invalid = 1;
	}
#endif
	if (!gpio_is_valid(gdata->ddr_id_gpio)) {
		printk(KERN_ERR "gdata->ddr_id_gpio %d is invalid.\n", gdata->ddr_id_gpio);
		gpio_is_invalid = 1;;
	}
	if (!gpio_is_valid(gdata->touch_id_gpio)) {
		printk(KERN_ERR "gdata->touch_id_gpio %d is invalid.\n", gdata->touch_id_gpio);
		gpio_is_invalid = 1;
	}	
	
	printk(KERN_INFO "GPIO check: %d %d %d %d %d %d %d\n", 
#ifdef HDMI_GPIO_DEF_REMOVE_DONE
		gdata->proj_id1_gpio,
#else
		-1,
#endif
		gdata->proj_id2_gpio,
		gdata->hw_id1_gpio,
#ifdef TERT_I2S_GPIO_DEF_REMOVE_DONE
		gdata->hw_id2_gpio,
		gdata->hw_id3_gpio,
#else
		-1,
		-1,
#endif
		gdata->ddr_id_gpio,
		gdata->touch_id_gpio);
	
	/* Set pinctrl active and gpio direction */	
	pinctrl_select_state(gdata->hwid_pinctrl, gdata->hwid_pinctrl_act);

#ifdef HDMI_GPIO_DEF_REMOVE_DONE
	gpio_request(gdata->proj_id1_gpio, "gpio_project_id_1");
#endif	
	gpio_request(gdata->proj_id2_gpio, "gpio_project_id_2");
	gpio_request(gdata->hw_id1_gpio, "gpio_hw_id_1");
#ifdef TERT_I2S_GPIO_DEF_REMOVE_DONE
	gpio_request(gdata->hw_id2_gpio, "gpio_hw_id_2");
	gpio_request(gdata->hw_id3_gpio, "gpio_hw_id_3");
#endif	
	gpio_request(gdata->ddr_id_gpio, "gpio_ddr_id");
	gpio_request(gdata->touch_id_gpio, "gpio_touch_id");

#ifdef HDMI_GPIO_DEF_REMOVE_DONE
	gpio_direction_input(gdata->proj_id1_gpio);
#endif
	gpio_direction_input(gdata->proj_id2_gpio);
	gpio_direction_input(gdata->hw_id1_gpio);
#ifdef TERT_I2S_GPIO_DEF_REMOVE_DONE
	gpio_direction_input(gdata->hw_id2_gpio);
	gpio_direction_input(gdata->hw_id3_gpio);
#endif	
	gpio_direction_input(gdata->ddr_id_gpio);
	gpio_direction_input(gdata->touch_id_gpio);

	/* Get gpio value */
#ifdef HDMI_GPIO_DEF_REMOVE_DONE
	projid += (gpio_get_value(gdata->proj_id1_gpio) << 0);
#endif	
	projid += (gpio_get_value(gdata->proj_id2_gpio) << 1);
	hwid += (gpio_get_value(gdata->hw_id1_gpio) << 0);
#ifdef TERT_I2S_GPIO_DEF_REMOVE_DONE
	hwid += (gpio_get_value(gdata->hw_id2_gpio) << 1);
	hwid += (gpio_get_value(gdata->hw_id3_gpio) << 2);
#endif	
	ddrid = (gpio_get_value(gdata->ddr_id_gpio) << 0);
	touchid = (gpio_get_value(gdata->touch_id_gpio) << 0);

	cei_hwid_info.project_id = projid;
	cei_hwid_info.hw_id = hwid;
	cei_hwid_info.ddr_id = ddrid;
	cei_hwid_info.touch_id = touchid;	
	
	if (gpio_is_invalid == 1)
		return -EINVAL;
	return 0;
	
gpio_pinctrl_err:
	return -EINVAL;
}

/*
 * API to get CEI HWID information:
 *
 * get_cei_hw_id()-      return enum cei_hw_type
 * get_cei_project_id()- return enum cei_project_type
 * get_cei_ddr_id()-     return enum cei_ddr_type
 * get_cei_touch_id()- return enum cei_touch_type
 *
 * Enum definition is defined in cei_hw_id.h
 */

enum cei_hw_type get_cei_hw_id(void)
{
	return cei_hwid_info.hw_id;
}
EXPORT_SYMBOL(get_cei_hw_id);

enum cei_project_type get_cei_project_id(void)
{
	return cei_hwid_info.project_id;
}
EXPORT_SYMBOL(get_cei_project_id);

enum cei_ddr_type get_cei_ddr_id(void)
{
	return cei_hwid_info.ddr_id;
}
EXPORT_SYMBOL(get_cei_ddr_id);

enum cei_touch_type get_cei_touch_id(void)
{
	return cei_hwid_info.touch_id;
}
EXPORT_SYMBOL(get_cei_touch_id);

static int cei_hwid_info_read(struct seq_file *m, void *v)
{
	seq_printf(m, "%d %d %d %d\n",
				cei_hwid_info.project_id,
				cei_hwid_info.hw_id,
				cei_hwid_info.ddr_id,
				cei_hwid_info.touch_id);
	return 0;
}

static int cei_hwid_info_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, cei_hwid_info_read, NULL);
}

static const struct file_operations cei_hwid_info_fops = {
    .open       = cei_hwid_info_proc_open,
    .read       = seq_read,
    .llseek     = seq_lseek,
    .release    = single_release,
};

static int cei_hwid_info_string_read(struct seq_file *m, void *v)
{
	seq_printf(m, "%s=%s %s=%s %s=%s %s=%s\n", 
				"projectid", cei_project_type_str [cei_hwid_info.project_id ], 
				"hwid", cei_hw_type_str[ cei_hwid_info.hw_id ], 
				"ddrid", cei_ddr_type_str[ cei_hwid_info.ddr_id ], 
				"touchid", cei_touch_type_str[ cei_hwid_info.touch_id ]);
	return 0;
}

static int cei_hwid_info_string_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, cei_hwid_info_string_read, NULL);
}

static const struct file_operations cei_hwid_info_string_fops = {
    .open       = cei_hwid_info_string_proc_open,
    .read       = seq_read,
    .llseek     = seq_lseek,
    .release    = single_release,
};

static const struct of_device_id cei_hw_id_of_match[] = {
	{ .compatible = "cei,gpio-hwid", },
	{ },
};
MODULE_DEVICE_TABLE(of, cei_hw_id_of_match);


static int cei_hw_id_probe(struct platform_device *pdev)
{
	int ret = 0;

	printk(KERN_INFO "cei_hwid_parse_dt_gpio\n");

	/* Initialize cei_hwid_info */
	cei_hwid_info.project_id = CEI_PROJECT_INVALID;
	cei_hwid_info.hw_id = CEI_HW_INVALID;
	cei_hwid_info.ddr_id = CEI_DDR_SRC_INVALID;
	cei_hwid_info.touch_id = CEI_TOUCH_SRC_INVALID;

	/* Get cei hwid info from dt */
	ret = cei_hwid_parse_dt_gpio(&pdev->dev);
	if (ret)
		printk(KERN_INFO "cei_hwid_parse_dt_gpio error=%d\n", ret);

	/* Verify cei hwid is valid or not */
	cei_hwid_info.project_id = is_cei_valid_project();
	cei_hwid_info.hw_id = is_cei_valid_board();
	cei_hwid_info.ddr_id = is_cei_valid_ddr_src();
	cei_hwid_info.touch_id = is_cei_valid_touch_src();

	printk(KERN_INFO "cei_project_id=%d, cei_hw_id=%d, cei_ddr_id=%d, cei_touch_id=%d\n",
				cei_hwid_info.project_id,
				cei_hwid_info.hw_id,
				cei_hwid_info.ddr_id,
				cei_hwid_info.touch_id);

	proc_create("cei_hwid_info", 0, NULL, &cei_hwid_info_fops);
	proc_create("cei_hwid_info_string", 0, NULL, &cei_hwid_info_string_fops);
	
	return 0;
}

static int cei_hw_id_remove(struct platform_device *pdev)
{
	printk(KERN_INFO "CEI HWID remove\n");
	//TODO: check what to do here
	return 0;
}

static struct platform_driver cei_hw_id_driver = {
	.probe      = cei_hw_id_probe,
	.remove     = cei_hw_id_remove,
	.driver = {
		.name = "cei-hwid-driver",
		.owner = THIS_MODULE,
		.of_match_table = cei_hw_id_of_match,
	},
};

static int __init cei_hw_id_init(void)
{
	printk(KERN_INFO "cei_hw_id_init\n");
	return platform_driver_register(&cei_hw_id_driver);
}
static void __exit cei_hw_id_exit(void)
{
	printk(KERN_INFO "cei_hw_id_exit\n");
	platform_driver_unregister(&cei_hw_id_driver);
}

module_init(cei_hw_id_init);
module_exit(cei_hw_id_exit);

MODULE_DESCRIPTION("CEI hardware ID driver");
MODULE_AUTHOR("Chewei Liang <chewei_liang@compal.com>");
MODULE_LICENSE("GPL");
