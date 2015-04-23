/*
 * STMicroelectronics lsm6ds3 i2c driver
 *
 * Copyright 2014 STMicroelectronics Inc.
 *
 * Giuseppe Barba <giuseppe.barba@st.com>
 * v 1.1.0
 * Licensed under the GPL-2.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hrtimer.h>
#include <linux/input.h>
#include <linux/types.h>

#include	<linux/platform_data/lsm6ds3.h>
#include	"lsm6ds3_core.h"

// S- [PM99] Grace_Chang add for sensor power
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>

/* POWER SUPPLY VOLTAGE RANGE */
#define LSM6DS3_VDD_MIN_UV	1700000
#define LSM6DS3_VDD_MAX_UV	1900000

static int lsm6ds3_power_set(struct lsm6ds3_data *data, bool on)
{
	int rc;

	pr_info("[Sensor] %s , enter,  on = %d\n", __FUNCTION__, on);

	if (!on) {
		if (regulator_count_voltages(data->vdd) > 0)
			regulator_set_voltage(data->vdd, 0,
				LSM6DS3_VDD_MAX_UV);

		regulator_put(data->vdd);
 	} else {
		data->vdd = regulator_get(data->dev, "vdd");
		if (IS_ERR(data->vdd)) {
			rc = PTR_ERR(data->vdd);
			dev_err(data->dev,
				"Regulator get failed vdd rc=%d\n", rc);
			return rc;
		}
		
		rc = regulator_enable(data->vdd);
		if (rc) {
			dev_err(data->dev,
				"Regulator vdd enable failed rc=%d\n", rc);
  			goto reg_vdd_put;
		}
		pr_info("[Sensor] %s , get & enable regulator\n", __FUNCTION__);

		/*
		 * The max time for the power supply rise time is 50ms.
		 * Use 80ms to make sure it meets the requirements.
		 */
		msleep(80);
	}

	pr_info("[Sensor] %s , exit\n", __FUNCTION__);
	return 0;

reg_vdd_put:
	pr_info("[Sensor] %s , exit,  rc = %d", __FUNCTION__, rc);
	regulator_put(data->vdd);
	return rc;
}
// E- [PM99] Grace_Chang add for sensor power

static int lsm6ds3_i2c_read(struct lsm6ds3_data *cdata, u8 reg_addr, int len,
							u8 *data, bool b_lock)
{
	int err = 0;
	struct i2c_msg msg[2];
	struct i2c_client *client = to_i2c_client(cdata->dev);

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].len = 1;
	msg[0].buf = &reg_addr;

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].len = len;
	msg[1].buf = data;

	if (b_lock) {
		mutex_lock(&cdata->bank_registers_lock);
		err = i2c_transfer(client->adapter, msg, 2);
		mutex_unlock(&cdata->bank_registers_lock);
	} else
		err = i2c_transfer(client->adapter, msg, 2);

	return err;
}

static int lsm6ds3_i2c_write(struct lsm6ds3_data *cdata, u8 reg_addr, int len,
							u8 *data, bool b_lock)
{
	int err = 0;
	u8 send[len + 1];
	struct i2c_msg msg;
	struct i2c_client *client = to_i2c_client(cdata->dev);

	send[0] = reg_addr;
	memcpy(&send[1], data, len * sizeof(u8));
	len++;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = len;
	msg.buf = send;

	if (b_lock) {
		mutex_lock(&cdata->bank_registers_lock);
		err = i2c_transfer(client->adapter, &msg, 1);
		mutex_unlock(&cdata->bank_registers_lock);
	} else
		err = i2c_transfer(client->adapter, &msg, 1);

	return err;
}


static const struct lsm6ds3_transfer_function lsm6ds3_tf_i2c = {
	.write = lsm6ds3_i2c_write,
	.read = lsm6ds3_i2c_read,
};

static int lsm6ds3_i2c_probe(struct i2c_client *client,
						const struct i2c_device_id *id)
{
	int err;
	struct lsm6ds3_data *cdata;

	pr_info("[Sensor] %s , enter", __FUNCTION__ );
	cdata = kmalloc(sizeof(*cdata), GFP_KERNEL);
	if (!cdata)
		return -ENOMEM;

	cdata->dev = &client->dev;
	cdata->name = client->name;
	cdata->tf = &lsm6ds3_tf_i2c;
	i2c_set_clientdata(client, cdata);

// S- [PM99] Grace_Chang add for sensor power
	/* Set I2C power */
	err = lsm6ds3_power_set(cdata, 1);
	if (err < 0)
		goto exit1;
// E- [PM99] Grace_Chang add for sensor power

	err = lsm6ds3_common_probe(cdata, client->irq, BUS_I2C);
	if (err < 0)
		goto free_data;

	pr_info("[Sensor] %s , exit", __FUNCTION__ );
	return 0;

exit1:
	//lsm6ds3_power_init(cdata, 0);
free_data:
	kfree(cdata);
	return err;
}

static int lsm6ds3_i2c_remove(struct i2c_client *client)
{
	/* TODO: check the function */
	struct lsm6ds3_data *cdata = i2c_get_clientdata(client);

	lsm6ds3_common_remove(cdata, client->irq);
	dev_info(cdata->dev, "%s: removed\n", LSM6DS3_ACC_GYR_DEV_NAME);
	kfree(cdata);
	return 0;
}

#ifdef CONFIG_PM
static int lsm6ds3_suspend(struct device *dev)
{
	struct lsm6ds3_data *cdata = i2c_get_clientdata(to_i2c_client(dev));

	return lsm6ds3_common_suspend(cdata);
}

static int lsm6ds3_resume(struct device *dev)
{
	struct lsm6ds3_data *cdata = i2c_get_clientdata(to_i2c_client(dev));

	return lsm6ds3_common_resume(cdata);
}

static const struct dev_pm_ops lsm6ds3_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(lsm6ds3_suspend, lsm6ds3_resume)
};

#define LSM6DS3_PM_OPS		(&lsm6ds3_pm_ops)
#else /* CONFIG_PM */
#define LSM6DS3_PM_OPS		NULL
#endif /* CONFIG_PM */


static const struct i2c_device_id lsm6ds3_ids[] = {
	{"lsm6ds3", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, lsm6ds3_ids);

#ifdef CONFIG_OF
static const struct of_device_id lsm6ds3_id_table[] = {
	{.compatible = "st,lsm6ds3", },
	{ },
};
MODULE_DEVICE_TABLE(of, lsm6ds3_id_table);
#endif

static struct i2c_driver lsm6ds3_i2c_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = LSM6DS3_ACC_GYR_DEV_NAME,
		.pm = LSM6DS3_PM_OPS,
#ifdef CONFIG_OF
		.of_match_table = lsm6ds3_id_table,
#endif
	},
	.probe    = lsm6ds3_i2c_probe,
	.remove   = lsm6ds3_i2c_remove,
	.id_table = lsm6ds3_ids,
};

module_i2c_driver(lsm6ds3_i2c_driver);

MODULE_DESCRIPTION("STMicroelectronics lsm6ds3 i2c driver");
MODULE_AUTHOR("Giuseppe Barba");
MODULE_LICENSE("GPL v2");
