/*------------------------------------------------------------------------------
 * by warren zhao 
 -----------------------------------------------------------------------------*/

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/major.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>

#include <linux/silead/sileadclass.h>

static struct class *silead_class;
static unsigned long silead_minors[8];
spinlock_t silead_lock;

static int silead_get_minor(uint32_t minor, uint32_t count)
{
	int i, cn;

	spin_lock(&silead_lock);

	if (minor == SILEAD_MINOR_DYNAMICAL) {
		cn = 0;
		i = minor = SILEAD_MINOR_DYN_MIN;
		for (; i < SILEAD_MINOR_DYN_MAX && cn < count; i++) {
			if (test_bit(i, silead_minors)) {
				cn = 0;
				minor = i + 1;

				continue;
			}

			cn++;
		}

		if (cn == count) {
			/* get & set the minors */
			for (i = minor; i < minor + count; i++)
				set_bit(i, silead_minors);
		} else {
			minor = -1;
		}

	} else if (minor >= SILEAD_MINOR_DYN_MIN) {
		minor = -1;
	}

	spin_unlock(&silead_lock);

	return minor;
}

static void silead_free_minor(uint32_t minor, uint32_t count)
{
	int i;

	if (minor < SILEAD_MINOR_DYN_MIN)
		return;

	spin_lock(&silead_lock);
	for (i = minor; i < minor + count; i++)
		clear_bit(i, silead_minors);
	spin_unlock(&silead_lock);
}

int silead_device_register(struct silead_device *silead, struct file_operations *ops)
{
	int i, minor;
	dev_t devno;
	
	minor = silead_get_minor(silead->minor, silead->count);
	if (minor < 0) {
		printk(KERN_ERR "No SILEAD minor available: %d!\n", minor);
		return -EBUSY;
	}

	silead->minor = minor;
	devno = MKDEV(SILEAD_MAJOR, silead->minor);
	cdev_init(&silead->cdev, ops);
	if (cdev_add(&silead->cdev, devno, silead->count) != 0) {
		silead_free_minor(silead->minor, silead->count);
		return -EIO;
	}

	if (silead->count > 1) {
		for (i = 0; i < silead->count; i++)
			device_create(silead_class, NULL,
				MKDEV(SILEAD_MAJOR, silead->minor + i),
				"%s%d", silead->name, i);
	} else {
		device_create(silead_class, NULL, devno, "%s", silead->name);
	}

	return 0;
}

int silead_device_unregister(struct silead_device *silead)
{
	int i;

	for (i = 0; i < silead->count; i++) {
		device_destroy(silead_class, MKDEV(SILEAD_MAJOR, silead->minor + i));
	}
	cdev_del(&silead->cdev);
	silead_free_minor(silead->minor, silead->count);

	return 0;
}

EXPORT_SYMBOL(silead_device_register);
EXPORT_SYMBOL(silead_device_unregister);

static int __init silead_init(void)
{
	int i;

	spin_lock_init(&silead_lock);

	/* set the preserved minors */
	for (i = 0; i < SILEAD_MINOR_DYN_MIN; i++) {
		set_bit(i, silead_minors);
	}

	silead_class = class_create(THIS_MODULE, "silead");
	if (IS_ERR(silead_class))
		return PTR_ERR(silead_class);

	return 0;
}
arch_initcall(silead_init);
