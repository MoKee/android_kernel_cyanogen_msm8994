/*
 * Simple synchronous userspace interface to SPI devices
 *
 * Copyright (C) 2006 SWAPP
 *	Andrea Paterniani <a.paterniani@swapp-eng.it>
 * Copyright (C) 2007 David Brownell (simplification, cleanup)
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
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/platform_device.h> //add silead 20150328
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/compat.h>
#include <linux/of.h> // add silead 20150421 for Qualcomm only
#include <linux/of_device.h> // add silead 20150421 for Qualcomm only
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h> // add silead 20150421 for Qualcomm only
#include <linux/interrupt.h>
#include <linux/seq_file.h>

#include <linux/jiffies.h>
#include <linux/timex.h>  //silead 0826
#include <linux/timer.h>  //silead 0826
#include <linux/wakelock.h>
#define VERBOSE  0
#include <asm/uaccess.h>
#define SL_MAX_FRAME_NUM 2

#include <linux/sched.h>
//#include <mach/gpio.h> //Kylix
//#include <plat/gpio-cfg.h> //Kylix
#include <linux/cdev.h>
#include "slspi.h"
#include <linux/regulator/consumer.h>//jonny S
#include "sl_proc.h"
/*
 * This supports access to SPI devices using normal userspace I/O calls.
 * Note that while traditional UNIX/POSIX I/O semantics are half duplex,
 * and often mask message boundaries, full SPI support requires full duplex
 * transfers.  There are several kinds of internal message boundaries to
 * handle chipselect management and other protocol options.
 *
 * SPI has a character major number assigned.  We allocate minor numbers
 * dynamically using a bitmask.  You must use hotplug tools, such as udev
 * (or mdev with busybox) to create and destroy the /dev/spidevB.C device
 * nodes, since there is no fixed association of minor numbers with any
 * particular SPI bus or device.
 */
//#define SPIDEV_MAJOR			153	/* assigned */
#define N_SPI_MINORS 32	/* ... up to 256 */


//#define LSB_TO_MSB

//jonny S
//#define GSL6313_POWER_CTRL 1

#if  GSL6313_POWER_CTRL
/*VDD 2.375V-3.46V VLOGIC 1.8V +-5%*/
#define GSL6313_VDD_MIN_UV	2000000
#define GSL6313_VDD_MAX_UV	3300000
#define GSL6313_VIO_MIN_UV	1750000
#define GSL6313_VIO_MAX_UV	1950000
#endif
//jonny E

/* [PM99] S- BUG#274 Jonny_Chan shutdown active/suspend */
#define PINCTRL_STATE_ACTIVE    "fp_active"
#define PINCTRL_STATE_SUSPEND   "fp_suspend"
/* [PM99] E- BUG#274 Jonny_Chan shutdown active/suspend */

#ifdef GSL6313_INTERRUPT_CTRL
#define PINCTRL_STATE_INTERRUPT "fp_interrupt"  /* [PM99] BUG#xxx Jonny_Chan IRQ wake-up control */
#endif

static DECLARE_BITMAP(minors, N_SPI_MINORS);
/* Bit masks for spi_device.mode management.  Note that incorrect
 * settings for some settings can cause *lots* of trouble for other
 * devices on a shared bus:
 *
 *  - CS_HIGH ... this device will be active when it shouldn't be
 *  - 3WIRE ... when active, it won't behave as it should
 *  - NO_CS ... there will be no explicit message boundaries; this
 *	is completely incompatible with the shared bus model
 *  - READY ... transfers may proceed when they shouldn't.
 *
 * REVISIT should changing those flags be privileged?
 */
#define SPI_MODE_MASK		(SPI_CPHA | SPI_CPOL | SPI_CS_HIGH \
				| SPI_LSB_FIRST | SPI_3WIRE | SPI_LOOP \
				| SPI_NO_CS | SPI_READY)

//static char tmp[1024]; //Kylix
/// @ 2015 add by joker
static int spidev_reset_low(void);
static int spidev_reset_hight(void);
static int irq_counter = 0;
/// @ 2015 add by joker
//static void dump_g_addr(char *ddr) //Kylix
//{
//    int i=0;
//    memset(tmp, 0, sizeof(tmp));
//    for(i=0; i <32; ++i) {
//        if (i %16==0) sprintf(tmp+strlen(tmp),"\n");
//        sprintf(tmp+strlen(tmp), " %02x", ddr[i]);
//    }
//    sprintf(tmp+strlen(tmp),"\n");
//    printk(KERN_INFO "%s", tmp);
//}

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);

static struct spidev_data	*fp_spidev = NULL;
static unsigned int spidev_major = 0;
struct cdev spicdev;

static unsigned bufsiz = 4096;
module_param(bufsiz, uint, S_IRUGO);
MODULE_PARM_DESC(bufsiz, "data bytes in biggest supported SPI message");

//jonny S
/*--------------------------------------------------------------------------
 *power control function
 *--------------------------------------------------------------------------*/
 #if  GSL6313_POWER_CTRL
static int GSL6313_power_on(struct spidev_data *data){

	int rc = 0;
	rc = regulator_enable(data->vdd);
		if (rc) {
			dev_err(&data->spi->dev,"Regulator vdd enable failed rc=%d\n", rc);
			return rc;
		}

		rc = regulator_enable(data->vio);
		if (rc) {
			dev_err(&data->spi->dev,"Regulator vio enable failed rc=%d\n", rc);
			regulator_disable(data->vdd);
			return rc;
		}
	return rc;

}


static int GSL6313_power_init(struct spidev_data *data, bool on)
{
	int rc;

	if (!on) {
		if (regulator_count_voltages(data->vdd) > 0)
			regulator_set_voltage(data->vdd, 0, GSL6313_VDD_MAX_UV);

		regulator_put(data->vdd);

		if (regulator_count_voltages(data->vio) > 0)
			regulator_set_voltage(data->vio, 0, GSL6313_VIO_MAX_UV);

		regulator_put(data->vio);
	} else {
		data->vdd = regulator_get(&data->spi->dev, "vdd");
		if (IS_ERR(data->vdd)) {
			rc = PTR_ERR(data->vdd);
			dev_err(&data->spi->dev,
				"Regulator get failed vdd rc=%d\n", rc);
			return rc;
		}

		if (regulator_count_voltages(data->vdd) > 0) {
			rc = regulator_set_voltage(data->vdd, GSL6313_VDD_MIN_UV,
						   GSL6313_VDD_MAX_UV);
			if (rc) {
				dev_err(&data->spi->dev,
					"Regulator set failed vdd rc=%d\n",
					rc);
				goto reg_vdd_put;
			}
		}

		data->vio = regulator_get(&data->spi->dev, "vio");
		if (IS_ERR(data->vio)) {
			rc = PTR_ERR(data->vio);
			dev_err(&data->spi->dev,
				"Regulator get failed vio rc=%d\n", rc);
			goto reg_vdd_set;
		}

		if (regulator_count_voltages(data->vio) > 0) {
			rc = regulator_set_voltage(data->vio, GSL6313_VIO_MIN_UV,
						   GSL6313_VIO_MAX_UV);
			if (rc) {
				dev_err(&data->spi->dev,
				"Regulator set failed vio rc=%d\n", rc);
				goto reg_vio_put;
			}
		}
	}

	return 0;

reg_vio_put:
	regulator_put(data->vio);
reg_vdd_set:
	if (regulator_count_voltages(data->vdd) > 0)
		regulator_set_voltage(data->vdd, 0, GSL6313_VDD_MAX_UV);
reg_vdd_put:
	regulator_put(data->vdd);
	return rc;
}
#endif
//jonny E


/* [PM99] S- BUG#274 Jonny_Chan shutdown active/suspend */
static int silead_fp_pinctrl_init(struct spidev_data *data)
{
    int ret;

    /* Get pinctrl if target uses pinctrl */
    data->fp_pinctrl = devm_pinctrl_get(&data->spi->dev);
    if (IS_ERR_OR_NULL(data->fp_pinctrl)) {
        ret = PTR_ERR(data->fp_pinctrl);
        dev_err(&data->spi->dev,
            "Target does not use pinctrl %d\n", ret);
        goto err_pinctrl_get;
    }

    data->pinctrl_state_active
        = pinctrl_lookup_state(data->fp_pinctrl,
                PINCTRL_STATE_ACTIVE);
    if (IS_ERR_OR_NULL(data->pinctrl_state_active)) {
        ret = PTR_ERR(data->pinctrl_state_active);
        dev_err(&data->spi->dev,
            "Can not lookup %s pinstate %d\n",
            PINCTRL_STATE_ACTIVE, ret);
        goto err_pinctrl_lookup;
    }

    data->pinctrl_state_suspend
        = pinctrl_lookup_state(data->fp_pinctrl,
            PINCTRL_STATE_SUSPEND);
    if (IS_ERR_OR_NULL(data->pinctrl_state_suspend)) {
        ret = PTR_ERR(data->pinctrl_state_suspend);
        dev_err(&data->spi->dev,
            "Can not lookup %s pinstate %d\n",
            PINCTRL_STATE_SUSPEND, ret);
        goto err_pinctrl_lookup;
    }

/* [PM99] S- BUG#xxx Jonny_Chan IRQ wake-up control */
#ifdef GSL6313_INTERRUPT_CTRL
    data->pinctrl_state_interrupt
        = pinctrl_lookup_state(data->fp_pinctrl,
            PINCTRL_STATE_INTERRUPT);
    if (IS_ERR_OR_NULL(data->pinctrl_state_interrupt)) {
        ret = PTR_ERR(data->pinctrl_state_interrupt);
        dev_err(&data->spi->dev,
            "Can not lookup %s pinstate %d\n",
            PINCTRL_STATE_INTERRUPT, ret);
        goto err_pinctrl_lookup;
    }
#endif
/* [PM99] E- BUG#xxx Jonny_Chan IRQ wake-up control */

    printk("%s --\n", __func__);
    return 0;

err_pinctrl_lookup:
    devm_pinctrl_put(data->fp_pinctrl);
err_pinctrl_get:
    data->fp_pinctrl = NULL;
    return ret;
}
/* [PM99] E- BUG#274 Jonny_Chan shutdown active/suspend */
/* [PM99] S- BUG#xxx Jonny_Chan IRQ wake-up control */
/*--------------------------------------------------------------------------
 * work function
 *--------------------------------------------------------------------------*/
 #ifdef GSL6313_INTERRUPT_CTRL
static void finger_interrupt_work(struct work_struct *work)
{
	//int value; //Silead 0824
	//struct spidev_data *spidev = container_of(work, struct spidev_data, int_work.work);  //for delay work
	struct spidev_data *spidev = container_of(work, struct spidev_data, int_work);

 	//silead 0831 char *event[2] = {"silead_fp", NULL};
	char*   env_ext[2] = {"SILEAD_FP_EVENT=IRQ", NULL};
	//printk("irq bottom half spidev_irq_work enter \n"); Silead 0824
        //msleep(100); //jonny 0820 0819
	kobject_uevent_env(&spidev->spi->dev.kobj, KOBJ_CHANGE, env_ext ); 
	
	//mutex_lock(&hsppad->lock);
	//printk("%s", __func__);
	//msleep(200);
	//value = gpio_get_value_cansleep(spidev->int_wakeup_gpio); Silead 0824
	//printk("[%s] E IRQ %d , GPIO %d state is %d\n", __func__, spidev->int_irq,spidev->int_wakeup_gpio,value); Silead 0824

	//enable_irq(spidev->int_irq); //jonny_test
	//enable_irq_wake(spidev->int_irq);
	//__pm_relax(spidev->spi->dev.power.wakeup);

	//mutex_unlock(&hsppad->lock);
}

static irqreturn_t finger_interrupt_handler(int irq, void *dev)
{
	int value;
        struct timex txc; //silead 0826
	struct spidev_data *spidev = dev;
        do_gettimeofday(&(txc.time)); //silead 0826
        printk("txc.time.tv_sec=%ld,txc.time.tv_usec=%ld \n",txc.time.tv_sec,txc.time.tv_usec);  //silead 0826
        //printk("[%s] S interrupt top half has entered!\n",__func__); //Silead 0825
	//disable_irq(irq);
        wake_lock_timeout(&spidev->wake_lock, 10*HZ);  //jonny 0820 0819
	disable_irq_nosync(irq);  //re-enable
        irq_counter--;  //jonny 0820
	//disable_irq_wake(irq);
	value = gpio_get_value_cansleep(spidev->int_wakeup_gpio);
	//__pm_stay_awake(spidev->spi->dev.power.wakeup);

	//printk("[%s]S IRQ %d , irq_counter is %d GPIO %d state is %d\n", __func__, irq, irq_counter,spidev->int_wakeup_gpio,value); Silead 0824
	//printk("S [%s]  state is %d\n", __func__, value);
	//msleep(2000);
	//value = gpio_get_value_cansleep(spidev->int_wakeup_gpio);
	//printk("E[%s] IRQ %d , GPIO %d state is %d\n", __func__, irq,spidev->int_wakeup_gpio,value);

	//mutex_lock(&spidev->buf_lock);  //
		//schedule_delayed_work(&spidev->int_work, msecs_to_jiffies(2000));
	queue_work(spidev->int_wq,&spidev->int_work);
	//mutex_unlock(&spidev->buf_lock);
	
	//enable_irq_wake(irq);
	//printk("[%s] E \n",__func__); Silead 0824
	//__pm_relax(spidev->spi->dev.power.wakeup);
	return IRQ_HANDLED;
}
#endif
/* [PM99] E- BUG#xxx Jonny_Chan IRQ wake-up control */
#define SL_READ  0x00 //read flags
#define SL_WRITE 0xFF //write flags
static void spidev_work(struct work_struct *work);
static inline void spidev_schedule_work(struct spidev_data *spidev)
{
    if (work_pending(&spidev->work)) {
        return;
    }
    if (spidev->wqueue) {
        queue_work(spidev->wqueue, &spidev->work);
    } else {
        schedule_work(&spidev->work);
    }
}

static int  put_buffer(struct spidev_data *spidev)
{
    spidev->k_mmap_buf += sizeof(struct sl_frame);
    if (spidev->k_mmap_buf - spidev->mmap_buf == sizeof(struct sl_frame)*spidev->max_frame_num) {
        spidev->k_mmap_buf = spidev->mmap_buf;
    }

    atomic_inc(&spidev->frame_num);
    if (atomic_read(&spidev->frame_num) == spidev->max_frame_num-1) {//spidev->k_mmap_buf == spidev->u_mmap_buf) {
        //buffer is full
        dev_dbg(&spidev->spi->dev, "Receive buffer is full\n");
        return 0;
    }
    return 0;
}
static int get_buffer(struct spidev_data *spidev)
{
    int offset = -EAGAIN;
    if (atomic_read(&spidev->frame_num) == 0) {//buffer is empty
        dev_dbg(&spidev->spi->dev, "Receive buffer is empyt\n");
        spidev_schedule_work(spidev);
        return offset;
    }

    atomic_dec(&spidev->frame_num);
    offset = spidev->u_mmap_buf- spidev->mmap_buf;
    spidev->u_mmap_buf += sizeof(struct sl_frame);
    if ((spidev->u_mmap_buf - spidev->mmap_buf) == sizeof(struct sl_frame)*spidev->max_frame_num) {
        spidev->u_mmap_buf = spidev->mmap_buf;
    }
    spidev_schedule_work(spidev);
    return offset;
}


static int gsl_fp_rdinit(struct spidev_data *spidev, unsigned char reg)
{
    uint8_t tx[] = {
        reg, SL_READ,
    };
    unsigned rx[ARRAY_SIZE(tx)] = {0};
    struct spi_message	m;
    struct spi_transfer	t = {
        .rx_buf		= rx,
        .tx_buf		= tx,
        .len		= ARRAY_SIZE(tx),
        .bits_per_word = SPI_BITS,
        .delay_usecs = SPI_DELAY,
        .speed_hz = SPI_SPEED,
    };

    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    return spidev_sync(spidev, &m);
}

void init_frame(struct spidev_data *spidev)
{
    unsigned int ret=0;
    unsigned long timeout;
// init page point
    spidev_write_reg(spidev, 0x00, 0xBF);
// start scanning
    spidev_write_reg(spidev, (0xFF080024>>7), 0xF0);
    spidev_write_reg(spidev, 0x2007FFFF, (0xFF080024%0x80));
    /* Wait  2 seconds for scanning done */
    timeout = jiffies + 2*HZ;
    while (time_before(jiffies, timeout)) {
        ret = spidev_read_reg(spidev, 0xBF);
        dev_dbg(&spidev->spi->dev, "0xBF=0x%02x\n", ret);
        if (ret != 0) {
            break;
        }
        udelay(100);
    }
    dev_dbg(&spidev->spi->dev, "last ret 0xBF=0x%02x\n", ret);
    spidev_write_reg(spidev, 0x00, 0xF0);
    gsl_fp_rdinit(spidev, 0);
}


static void spidev_work(struct work_struct *work)
{
    struct spidev_data *spidev = container_of(work, struct spidev_data, work);
    struct spi_message	m;
    struct spi_transfer	t[SL_ONE_FRAME_PAGES];
    int ret = 0, i;

	struct sched_param param = {.sched_priority = 1};
	sched_setscheduler(current, SCHED_RR, &param);

    if (atomic_read(&spidev->is_cal_mode)){
        return ;
    }

    t[0].rx_buf	= spidev->k_mmap_buf;
    t[0].tx_buf	= spidev->tx_mmap_buf;
    t[0].len	= SL_HEAD_SIZE +SL_PAGE_SIZE;
    t[0].bits_per_word = SPI_BITS;
    t[0].delay_usecs = SPI_DELAY;
    t[0].speed_hz = SPI_SPEED;
    spi_message_init(&m);
    spi_message_add_tail(&t[0], &m);
    for (i=1; i < SL_ONE_FRAME_PAGES; ++i) {
        t[i].rx_buf	= spidev->k_mmap_buf+i*SL_PAGE_SIZE + SL_HEAD_SIZE;
        t[i].tx_buf	= spidev->tx_mmap_buf+i*SL_PAGE_SIZE + SL_HEAD_SIZE;
        t[i].len    = SL_PAGE_SIZE;
        t[i].bits_per_word = SPI_BITS;
        t[i].delay_usecs = SPI_DELAY;
        t[i].speed_hz = SPI_SPEED;
        spi_message_add_tail(&t[i], &m);
    }

    if (!atomic_read(&spidev->frame_num)) {
        init_frame(spidev);
        ret = spidev_sync(spidev, &m);
        if (ret >0) {
            put_buffer(spidev);
        } else {
            dev_notice(&spidev->spi->dev, "sync fialed %d\n", ret);
        }
    } else {
        dev_dbg(&spidev->spi->dev, "Receive buffer is full=%d\n", atomic_read(&spidev->frame_num));
    }

    //if interrupt config, enable it
    //
}

static irqreturn_t spidev_irq_routing(int irq, void* dev)
{
    struct spidev_data *spidev = (struct spidev_data*)dev;
    wake_lock_timeout(&spidev->wake_lock, 5*HZ);
    return IRQ_HANDLED;
}
/*-------------------------------------------------------------------------*/

#ifdef LSB_TO_MSB
static inline unsigned char reversalBits(unsigned char reg)
{
    int i;
    unsigned char ret = 0;
    unsigned char tmp = 0;
    unsigned char tmp1 = 0;
    unsigned char mask = 1;
    for (i = 7; i >= 0; i--) {
        tmp = reg >> (7 - i);
        tmp1 = (tmp << i) & (mask << i);
        ret |= tmp1;
    }
    return ret;
}

static void Modifybuf(u8 *buf, size_t len)
{
    u8 tmp;
    int i;

    for (i = 0; i < len; i++) {
        tmp = reversalBits(buf[i]);
        //pr_err("%s:%d buf:%d\n", __func__, __LINE__, tmp);
        buf[i] = tmp;
    }
}
#endif

/*
 * We can't use the standard synchronous wrappers for file I/O; we
 * need to protect against async removal of the underlying spi_device.
 */
static void spidev_complete(void *arg)
{
    complete(arg);
}

ssize_t spidev_sync(struct spidev_data *spidev, struct spi_message *message)
{
    DECLARE_COMPLETION_ONSTACK(done);
    int status;

#ifdef LSB_TO_MSB
    struct list_head *p;
    struct spi_transfer *t;

    list_for_each(p, &message->transfers) {
        //pr_err("%s:%d\n", __func__, __LINE__);
        t = list_entry(p, struct spi_transfer, transfer_list);
        if (t->tx_buf) {
            Modifybuf((u8*)t->tx_buf, t->len);
        }
    }
#endif

    message->complete = spidev_complete;
    message->context = &done;

    if (spidev->spi == NULL) {
        status = -ESHUTDOWN;
    } else {
        spin_lock_irq(&spidev->spi_lock);
        status = spi_async(spidev->spi, message);
        spin_unlock_irq(&spidev->spi_lock);
    }

    if (status == 0) {
        //wait_for_completion(&done);------> deadlock
        /*unsigned long ret = */wait_for_completion_timeout(&done, msecs_to_jiffies(3000)); 
        status = message->status;
        if (status == 0)
            status = message->actual_length;

#ifdef LSB_TO_MSB
        list_for_each(p, &message->transfers) {
            //pr_err("%s:%d\n", __func__, __LINE__);
            t = list_entry(p, struct spi_transfer, transfer_list);
            if (t->rx_buf) {
                Modifybuf((u8*)t->rx_buf, t->len);
            }
        }
#endif
    }

    return status;
}

static inline ssize_t spidev_sync_write(struct spidev_data *spidev, size_t len)
{
    struct spi_transfer	t = {
        .tx_buf		= spidev->buffer,
        .len		= len,
    };
    struct spi_message	m;

    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    return spidev_sync(spidev, &m);
}

static inline ssize_t
spidev_sync_read(struct spidev_data *spidev, size_t len)
{
    struct spi_transfer	t = {
        .rx_buf		= spidev->buffer,
        .len		= len,
    };
    struct spi_message	m;

    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    return spidev_sync(spidev, &m);
}

static inline ssize_t
__spidev_sync_read(struct spidev_data *spidev, size_t offset, size_t len)
{
    struct spi_transfer	t = {
        .rx_buf		= spidev->mmap_buf + offset,
        .tx_buf		= spidev->mmap_buf + offset,
        .len		= len,
    };
    struct spi_message	m;

    spi_message_init(&m);
    spi_message_add_tail(&t, &m);

    return spidev_sync(spidev, &m);
}


static inline ssize_t
__spidev_async_read(struct spidev_data *spidev, size_t offset, size_t len)
{

    struct spi_transfer	t = {
        .rx_buf		= spidev->mmap_buf + offset,
        .tx_buf		= spidev->mmap_buf + offset,
        .len		= len,
    };
    struct spi_message	m;

    //unsigned		is_dma_mapped:1;
    spi_message_init(&m);
    spi_message_add_tail(&t, &m);

    return spidev_sync(spidev, &m);
}

static inline void sl_fp_read_init(struct spidev_data *spidev, u8 addr)
{
    struct spi_transfer		t;
    struct spi_message	m;
    u8 tx[6] = {0};
    u8 rx[6] = {0};

    tx[0] = addr;

    t.tx_buf = tx;
    t.rx_buf = rx;
    t.len = 6;
    t.bits_per_word = SPI_BITS;
    t.delay_usecs = SPI_DELAY;
    t.speed_hz = SPI_SPEED;

    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    spidev_sync(spidev, &m);
}

static inline ssize_t sl_fp_read(struct spidev_data *spidev, u8 addr, u8 *pdata, size_t len)
{
    ssize_t	status;
    struct spi_transfer		t;
    struct spi_message	m;
    u8 tx[SPI_BUF_SIZE + 3] = {0};
    u8 rx[SPI_BUF_SIZE + 3] = {0};
    u8 offset = 2;
    int i;

    if(len > SPI_BUF_SIZE) {
        printk("%s  too long len = %zu!\n", __func__, len);
        return -1;
    }

    if(addr < 0x80) {
        offset = 3;
        sl_fp_read_init(spidev, addr);
    }

    tx[0] = addr;

    t.tx_buf = tx;
    t.rx_buf = rx;
    t.len = len + offset;
    t.bits_per_word = SPI_BITS;
    t.delay_usecs = SPI_DELAY;
    t.speed_hz = SPI_SPEED;

    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    status = spidev_sync(spidev, &m);
    if(status > 0) {
        for(i = 0; i < len; i ++) {
            *(pdata + i) = rx[i + offset];
        }
    } else {
        printk("%s  error status = %zu\n", __func__, status);
    }
    return status;
}

static inline ssize_t
sl_fp_write(struct spidev_data *spidev, uint8_t reg, uint32_t w_data)
{
    struct spi_transfer		t;
    struct spi_message	m;

    uint8_t tx[] = {
        reg, 0xFF,
        (w_data >>0) &0xFF,
        (w_data >>8) &0xFF,
        (w_data >>16) &0xFF,
        (w_data >>24) &0xFF,
    };
    u8 rx[sizeof(tx)] = {0};
    t.tx_buf = tx;
    t.rx_buf = rx;
    t.len = sizeof(tx);
    t.bits_per_word = SPI_BITS;
    t.delay_usecs = SPI_DELAY;
    t.speed_hz = SPI_SPEED;

    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    return spidev_sync(spidev, &m);
}

union {
    unsigned char temp_char[4];
    unsigned int get_reg_data;
} temp_data;


unsigned int spidev_read_reg(struct spidev_data *spidev, unsigned char reg)
{
    struct spi_message	m;
    unsigned char rx[6];
    unsigned char tx[] = {
        reg, SL_READ, 0x00, 0x00, 0x00, 0x00
    };
    struct spi_transfer	t = {
        .rx_buf		= rx,
        .tx_buf		= tx,
        .len		= ARRAY_SIZE(tx),
        .bits_per_word = SPI_BITS,
        .delay_usecs = SPI_DELAY,
        .speed_hz = SPI_SPEED,
    };
    if (!(reg>0x80 && reg <0x100)) {
        gsl_fp_rdinit(spidev, reg);
    }
    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    spidev_sync(spidev, &m);
    memcpy(temp_data.temp_char, (rx+2), 4);
    return temp_data.get_reg_data;
}

int spidev_write_reg(struct spidev_data *spidev, unsigned int data, unsigned char reg)
{
    struct spi_message	m;
    uint8_t rx[6] = {0};
    uint8_t tx[] = {
        //reg, 0xFF,
        reg, SL_WRITE,
        (data >>0) &0xFF,
        (data >>8) &0xFF,
        (data >>16) &0xFF,
        (data >>24) &0xFF,
    };
    struct spi_transfer	t = {
        .rx_buf		= rx,
        .tx_buf		= tx,
        .len		= ARRAY_SIZE(tx),
        .bits_per_word = SPI_BITS,
        .delay_usecs = SPI_DELAY,
        .speed_hz = SPI_SPEED,
    };

    spi_message_init(&m);
    spi_message_add_tail(&t, &m);

    return spidev_sync(spidev, &m);
}

/*-------------------------------------------------------------------------*/

/* Read-only message with current device setup */
static ssize_t
spidev_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct spidev_data	*spidev;
    ssize_t			status = 0;

    /* chipselect only toggles at start or end of operation */
    if (count > bufsiz)
        return -EMSGSIZE;

    spidev = filp->private_data;

    mutex_lock(&spidev->buf_lock);
    status = spidev_sync_read(spidev, count);
    if (status > 0) {
        unsigned long	missing;

        missing = copy_to_user(buf, spidev->buffer, status);
        if (missing == status)
            status = -EFAULT;
        else
            status = status - missing;
    }
    mutex_unlock(&spidev->buf_lock);

    return status;
}

/* Write-only message with current device setup */
static ssize_t
spidev_write(struct file *filp, const char __user *buf,
             size_t count, loff_t *f_pos)
{
    struct spidev_data	*spidev;
    ssize_t			status = 0;
    unsigned long		missing;

    /* chipselect only toggles at start or end of operation */
    if (count > bufsiz)
        return -EMSGSIZE;

    spidev = filp->private_data;

    mutex_lock(&spidev->buf_lock);
    missing = copy_from_user(spidev->buffer, buf, count);
    if (missing == 0) {
        status = spidev_sync_write(spidev, count);
    } else
        status = -EFAULT;
    mutex_unlock(&spidev->buf_lock);

    return status;
}

static int spidev_message(struct spidev_data *spidev,
                          struct spi_ioc_transfer *u_xfers, unsigned n_xfers)
{
    struct spi_message	msg;
    struct spi_transfer	*k_xfers;
    struct spi_transfer	*k_tmp;
    struct spi_ioc_transfer *u_tmp;
    unsigned		n, total;
    u8			*buf;
    int			status = -EFAULT;

    spi_message_init(&msg);
    k_xfers = kcalloc(n_xfers, sizeof(*k_tmp), GFP_KERNEL);
    if (k_xfers == NULL)
        return -ENOMEM;

    /* Construct spi_message, copying any tx data to bounce buffer.
     * We walk the array of user-provided transfers, using each one
     * to initialize a kernel version of the same transfer.
     */
    buf = spidev->buffer;
    total = 0;
    for (n = n_xfers, k_tmp = k_xfers, u_tmp = u_xfers;
            n;
            n--, k_tmp++, u_tmp++) {
        k_tmp->len = u_tmp->len;

        total += k_tmp->len;
        if (total > bufsiz) {
            status = -EMSGSIZE;
            goto done;
        }

        if (u_tmp->rx_buf) {
            k_tmp->rx_buf = buf;
            if (!access_ok(VERIFY_WRITE, (u8 __user *)
                           (uintptr_t) u_tmp->rx_buf,
                           u_tmp->len))
                goto done;
        }
        if (u_tmp->tx_buf) {
            k_tmp->tx_buf = buf;
            if (copy_from_user(buf, (const u8 __user *)
                               (uintptr_t) u_tmp->tx_buf,
                               u_tmp->len))
                goto done;
        }
        buf += k_tmp->len;

        k_tmp->cs_change = !!u_tmp->cs_change;
        k_tmp->bits_per_word = u_tmp->bits_per_word;
        k_tmp->delay_usecs = u_tmp->delay_usecs;
        k_tmp->speed_hz = u_tmp->speed_hz;
#if VERBOSE
        dev_dbg(&spidev->spi->dev,
                "  xfer len %zd %s%s%s%dbits %u usec %uHz\n",
                u_tmp->len,
                u_tmp->rx_buf ? "rx " : "",
                u_tmp->tx_buf ? "tx " : "",
                u_tmp->cs_change ? "cs " : "",
                u_tmp->bits_per_word ? : spidev->spi->bits_per_word,
                u_tmp->delay_usecs,
                u_tmp->speed_hz ? : spidev->spi->max_speed_hz);
#endif
        spi_message_add_tail(k_tmp, &msg);
    }

    //retval =  __spidev_sync_read(spidev, 0, tmp);
    status = spidev_sync(spidev, &msg);
    if (status < 0) {
        dev_err(&spidev->spi->dev, "spidev sync failed %d\n", status);
        goto done;
    }

    /* copy any rx data out of bounce buffer */
    buf = spidev->buffer;

    // add silead 20150331
    //printk("spi buf = %02x %02x %02x %02x %02x %02x %02x %02x \n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]); // sileadinc 20150624
    // add silead end

    for (n = n_xfers, u_tmp = u_xfers; n; n--, u_tmp++) {
        if (u_tmp->rx_buf) {
            if (__copy_to_user((u8 __user *)
                               (uintptr_t) u_tmp->rx_buf, buf,
                               u_tmp->len)) {
                status = -EFAULT;
                goto done;
            }
        }
        buf += u_tmp->len;
    }
    status = total;

done:
    kfree(k_xfers);
    return status;
}

static int spidev_mmap(struct file* filep, struct vm_area_struct *vma)
{
    struct spidev_data	*spidev = filep->private_data;

    vma->vm_flags |= VM_RESERVED;
    vma->vm_flags |= VM_LOCKED;
    if (NULL == spidev->mmap_buf) {
        dev_err(&spidev->spi->dev,"frame buffer is not alloc\n");
        return -ENOMEM;
    }
    return remap_pfn_range( vma, vma->vm_start,
                            virt_to_phys((void*)((unsigned long)spidev->mmap_buf))>>PAGE_SHIFT,
                            vma->vm_end - vma->vm_start, PAGE_SHARED);
}
static int spidev_reset_hw(struct spidev_data *spidev)
{
int rc = 0;
int value = 2;
//   //soft reset;
//   spidev_write_reg(fp_spidev, (0x000000E0 >>7), 0xF0);
//   spidev_write_reg(fp_spidev, 0x00000088, 0x000000E0%0x80);
//   // hw reset  
//   if (spidev->reset_hw_gpio){
//       s3c2410_gpio_setpin(spidev->reset_hw_gpio, 0);
//       mdelay(5);
//       s3c2410_gpio_setpin(spidev->reset_hw_gpio, 1);
//   }
//   dev_info(&spidev->spi->dev, "Reset silead hw\n");

    /* 20150817 Kylix add start */
printk("[%s] S\n", __func__);
    //setup gpio mode
    //setup gpio output high
    //pull gpio output low;
        if(spidev->fp_pinctrl)
        {	
            rc = pinctrl_select_state(spidev->fp_pinctrl, spidev->pinctrl_state_suspend);
            if(rc)
                dev_err(&spidev->spi->dev, "[silead]cannot get suspend pinctrl state\n");
        }
    mdelay(5);
	value = gpio_get_value_cansleep(spidev->shutdown_gpio);
	printk("[%s] GPIO %d state is %d\n", __func__,spidev->shutdown_gpio,value);

    //pull gpio output high;
        if(spidev->fp_pinctrl)
        {
            rc = pinctrl_select_state(spidev->fp_pinctrl, spidev->pinctrl_state_active);
            if(rc)
                dev_err(&spidev->spi->dev, "[silead]cannot get active pinctrl state\n");
        }
    mdelay(5);
	value = gpio_get_value_cansleep(spidev->shutdown_gpio);
	printk("[%s] GPIO %d state is %d\n", __func__,spidev->shutdown_gpio,value);
printk("[%s] E\n", __func__);
    /* 20150817 Kylix add end */

     return 0; //Kylix
}

static int spidev_shutdown_hw(struct spidev_data *spidev)
{
//   //soft reset;
//   spidev_write_reg(fp_spidev, (0x000000E0 >>7), 0xF0);
//   spidev_write_reg(fp_spidev, 0x00000088, 0x000000E0%0x80);
//   // hw reset  
//   if (spidev->reset_hw_gpio){
//       s3c2410_gpio_setpin(spidev->reset_hw_gpio, 0);
//       mdelay(5);
//       s3c2410_gpio_setpin(spidev->reset_hw_gpio, 1);
//   }
//   dev_info(&spidev->spi->dev, "Reset silead hw\n");
    return 0;
}

static long
spidev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int		err = 0;
    int			retval = 0;
    struct spidev_data	*spidev;
    struct spi_device	*spi;
    u32			tmp;
    unsigned		n_ioc;
    struct spi_ioc_transfer	*ioc;


    /* Check type and command number */
    if (_IOC_TYPE(cmd) != SPI_IOC_MAGIC)
        return -ENOTTY;

    /* Check access direction once here; don't repeat below.
     * IOC_DIR is from the user perspective, while access_ok is
     * from the kernel perspective; so they look reversed.
     */
    if (_IOC_DIR(cmd) & _IOC_READ)
        err = !access_ok(VERIFY_WRITE,
                         (void __user *)arg, _IOC_SIZE(cmd));
    if (err == 0 && _IOC_DIR(cmd) & _IOC_WRITE)
        err = !access_ok(VERIFY_READ,
                         (void __user *)arg, _IOC_SIZE(cmd));
    if (err)
        return -EFAULT;

    /* guard against device removal before, or while,
     * we issue this ioctl.
     */
    spidev = filp->private_data;
    spin_lock_irq(&spidev->spi_lock);
    spi = spi_dev_get(spidev->spi);
    spin_unlock_irq(&spidev->spi_lock);

    if (atomic_read(&spidev->is_cal_mode)){
        dev_dbg(&spidev->spi->dev, "Current stat is cal mode\n");
        return -EBUSY;
    }
     
    /* 20150820 Kylix remove start */
    if (atomic_read(&spidev->is_suspend)){
        dev_dbg(&spidev->spi->dev, "device is suspend\n");
        return -EBUSY;
    }
    /* 20150820 Kylix remove end */

    if (spi == NULL){
        return -ESHUTDOWN;
    }

    /* use the buffer lock here for triple duty:
     *  - prevent I/O (from us) so calling spi_setup() is safe;
     *  - prevent concurrent SPI_IOC_WR_* from morphing
     *    data fields while SPI_IOC_RD_* reads them;
     *  - SPI_IOC_MESSAGE needs the buffer locked "normally".
     */
    mutex_lock(&spidev->buf_lock);

    switch (cmd) {
    case SPI_HW_RESET :
         spidev_reset_hw(spidev);
         break;
    //silead 0831
    case SPI_HW_SHUTDOWN:
		spidev_shutdown_hw(spidev);
		break;
     //silead 0831 
    case SPI_SET_WAKE_UP:
        retval =  __get_user(tmp,  (u32 __user *)arg);
        dev_dbg(&spi->dev, "SPI_SET_WAKE_UP:\n");
        if ((retval == 0) && (tmp != 0)) {
            spidev->wake_up_gpio = tmp;
            spidev->irq = gpio_to_irq(spidev->wake_up_gpio);
            //s3c_gpio_cfgpin(spidev->irq, spidev->wake_up_gpio); //Kylix
            //s3c_gpio_setpull(spidev->irq, S3C_GPIO_PULL_NONE);
            if (request_irq(spidev->irq, spidev_irq_routing, IRQF_TRIGGER_RISING, "sl_wake_up", spidev) < 0 ) {
                dev_err(&spi->dev, "Request irq failed, irq(%d)=%d\n", spidev->wake_up_gpio, spidev->irq);

                retval = -ENODEV;
            }
        }
        break;
    case SPI_SYNC_READ:
        retval =  __get_user(tmp,  (u32 __user *)arg);
        dev_dbg(&spi->dev, "SPI_SYNC_READ: pagesize=%d\n", tmp);
        if (retval == 0) {
            retval = __spidev_sync_read(spidev, 0, tmp);
        } else {
            dev_err(&spi->dev, "SPI_SYNC_READ:failed get_user\n");
        }
        break;
    case SPI_ASYNC_READ_PRE:
        dev_dbg(&spi->dev, "SPI_ASYNC_READ_PRE\n");
        spidev->k_mmap_buf = spidev->u_mmap_buf =spidev->mmap_buf;
        atomic_set(&spidev->frame_num, 0);
        cancel_work_sync(&spidev->work);
	spidev_schedule_work(spidev);
        retval = 0;
        break;
    case SPI_ASYNC_READ:
        dev_dbg(&spi->dev, "SPI_ASYNC_READ\n");
        retval = get_buffer(spidev);
        break;
    case SPI_GET_BUFFER_SIZE:
        dev_dbg(&spi->dev, "SPI_GET_BUFFER_SIZE\n");
        retval = __put_user(spidev->max_buf_size,
                            (__u32 __user *)arg);
        /* read requests */
    case SPI_IOC_RD_MODE:
        retval = __put_user(spi->mode & SPI_MODE_MASK,
                            (__u8 __user *)arg);
        break;
    case SPI_IOC_RD_LSB_FIRST:
        retval = __put_user((spi->mode & SPI_LSB_FIRST) ?  1 : 0,
                            (__u8 __user *)arg);
        break;
    case SPI_IOC_RD_BITS_PER_WORD:
        retval = __put_user(spi->bits_per_word, (__u8 __user *)arg);
        break;
    case SPI_IOC_RD_MAX_SPEED_HZ:
        retval = __put_user(spi->max_speed_hz, (__u32 __user *)arg);
        break;

        /* write requests */
    case SPI_IOC_WR_MODE:
        retval = __get_user(tmp, (u8 __user *)arg);
        if (retval == 0) {
            u8	save = spi->mode;

            if (tmp & ~SPI_MODE_MASK) {
                retval = -EINVAL;
                break;
            }

            tmp |= spi->mode & ~SPI_MODE_MASK;
            spi->mode = (u8)tmp;
            retval = spi_setup(spi);
            if (retval < 0)
                spi->mode = save;
            else
                dev_dbg(&spi->dev, "spi mode %02x\n", tmp);
        }
        break;
    case SPI_IOC_WR_LSB_FIRST:
        retval = __get_user(tmp, (__u8 __user *)arg);
        if (retval == 0) {
            u8	save = spi->mode;

            if (tmp)
                spi->mode |= SPI_LSB_FIRST;
            else
                spi->mode &= ~SPI_LSB_FIRST;
            retval = spi_setup(spi);
            if (retval < 0)
                spi->mode = save;
            else
                dev_dbg(&spi->dev, "%csb first\n",
                        tmp ? 'l' : 'm');
        }
        break;
    case SPI_IOC_WR_BITS_PER_WORD:
        retval = __get_user(tmp, (__u8 __user *)arg);
        if (retval == 0) {
            u8	save = spi->bits_per_word;

            spi->bits_per_word = tmp;
            retval = spi_setup(spi);
            if (retval < 0)
                spi->bits_per_word = save;
            else
                dev_dbg(&spi->dev, "%d bits per word\n", tmp);
        }
        break;
    case SPI_IOC_WR_MAX_SPEED_HZ:
        retval = __get_user(tmp, (__u32 __user *)arg);
        if (retval == 0) {
            u32	save = spi->max_speed_hz;

            spi->max_speed_hz = tmp;
            retval = spi_setup(spi);
            if (retval < 0)
                spi->max_speed_hz = save;
            else
                dev_dbg(&spi->dev, "%d Hz (max)\n", tmp);
        }
        break;
/////////@ 2015 add by joker
    case SPI_IOC_RD_RESET_H:

		spidev_reset_hight();
		printk("[2015]: test spidev_reset_hight() end!\n");
		retval = 1;
		
        break;
		
    case SPI_IOC_WR_RESET_L:
		
		spidev_reset_low();
		printk("[2015]: test spidev_reset_low() end!\n");
		retval = 1;

		break;		
/////////@ 2015 add by joker
    	case SPI_HW_IRQ_ENBALE:
		//FP_DBUG("%s:%lu", __FUNCTION__, arg);
                //printk("%s:%lu \n", __func__, arg); //jonny 0820 Silead 0824
		if(arg)
		{
			//int mode
			//FP_DBUG("int mode");
                        //printk("int mode, irq_counter is %d\n",irq_counter); //jonny 0820 Silead 0824
			sl_fp_write(fp_spidev,0xbf,0);	
                        if (!irq_counter)  //jonny 0820
                        {
			enable_irq(spidev->int_irq);//BUG#xxx
                        irq_counter++;
                        //printk("enable_irq, irq_counter is %d\n",irq_counter); //jonny 0820 Silead 0824
                        }

		}
		else
		{
			//polling mode
			//disable_irq_nosync(spidev->int_irq);//re-enable							
			//FP_DBUG("polling mode");
                        //printk("polling mode\n"); //jonny 0820 Silead 0824
		}
		break;
    /* 20150810 by silead end */

    default:
        /* segmented and/or full-duplex I/O request */
        if (_IOC_NR(cmd) != _IOC_NR(SPI_IOC_MESSAGE(0))
                || _IOC_DIR(cmd) != _IOC_WRITE) {
            retval = -ENOTTY;
            break;
        }

        tmp = _IOC_SIZE(cmd);
        if ((tmp % sizeof(struct spi_ioc_transfer)) != 0) {
            retval = -EINVAL;
            break;
        }
        n_ioc = tmp / sizeof(struct spi_ioc_transfer);
        if (n_ioc == 0)
            break;

        /* copy into scratch area */
        ioc = kmalloc(tmp, GFP_KERNEL);
        if (!ioc) {
            retval = -ENOMEM;
            break;
        }
        if (__copy_from_user(ioc, (void __user *)arg, tmp)) {
            kfree(ioc);
            retval = -EFAULT;
            break;
        }

        /* translate to spi_message, execute */
        retval = spidev_message(spidev, ioc, n_ioc);
        kfree(ioc);
        break;
    }

    mutex_unlock(&spidev->buf_lock);
    spi_dev_put(spi);
    return retval;
}

#ifdef CONFIG_COMPAT
static long
spidev_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    return spidev_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#else
#define spidev_compat_ioctl NULL
#endif /* CONFIG_COMPAT */

static int spidev_open(struct inode *inode, struct file *filp)
{
    struct spidev_data	*spidev;
    int			status = -ENXIO;

    if (atomic_read(&fp_spidev->is_cal_mode)){
        dev_dbg(&spidev->spi->dev, "Current stat is cal mode\n");
        return -EACCES;
    }

    mutex_lock(&device_list_lock);

    list_for_each_entry(spidev, &device_list, device_entry) {
        if (spidev->devt == inode->i_rdev) {
            status = 0;
            break;
        }
    }
    if (status == 0) {
        if (!spidev->buffer) {
            spidev->buffer = kmalloc(bufsiz, GFP_KERNEL);
            if (!spidev->buffer) {
                dev_dbg(&spidev->spi->dev, "open/ENOMEM\n");
                status = -ENOMEM;
            }
        }
        if (status == 0) {
            spidev->users++;
            filp->private_data = spidev;
            nonseekable_open(inode, filp);
        }
    } else
        pr_debug("spidev: nothing for minor %d\n", iminor(inode));

    mutex_unlock(&device_list_lock);
    return status;
}

static int spidev_release(struct inode *inode, struct file *filp)
{
    struct spidev_data	*spidev;
    int			status = 0;

    mutex_lock(&device_list_lock);
    spidev = filp->private_data;
    filp->private_data = NULL;

    /* last close? */
    spidev->users--;
    if (!spidev->users) {
        int		dofree;
        if (spidev->buffer) {
            kfree(spidev->buffer);
            spidev->buffer = NULL;
        }
        /* ... after we unbound from the underlying device? */
        spin_lock_irq(&spidev->spi_lock);
        dofree = (spidev->spi == NULL);
        spin_unlock_irq(&spidev->spi_lock);

        if (dofree)
            kfree(spidev);
    }
    cancel_work_sync(&spidev->work);
    mutex_unlock(&device_list_lock);
    return status;
}

static const struct file_operations spidev_fops = {
    .owner =	THIS_MODULE,
    /* REVISIT switch to aio primitives, so that userspace
     * gets more complete API coverage.  It'll simplify things
     * too, except for the locking.
     */
    .write =	spidev_write,
    .read =		spidev_read,
    .unlocked_ioctl = spidev_ioctl,
    .compat_ioctl = spidev_compat_ioctl,
    .open =		spidev_open,
    .release =	spidev_release,
    .llseek =	no_llseek,
    .mmap = spidev_mmap,
};

/*-------------------------------------------------------------------------*/

/* The main reason to have this class is to make mdev/udev create the
 * /dev/spidevB.C character device nodes exposing our userspace API.
 * It also simplifies memory management.
 */

static struct class *spidev_class;

/*-------------------------------------------------------------------------*/
static int /*__devinit*/ spidev_probe(struct spi_device *spi) //Kylix
{
    struct spidev_data	*spidev;
    int			status;
    unsigned long		minor, page;
    int			error; //jonny
#ifdef GSL6313_INTERRUPT_CTRL
    int 	irq_flags; 	/* [PM99] BUG#xxx Jonny_Chan IRQ wake-up control */
    unsigned int tmp;
#endif
    
    printk("%s  S \n", __func__);  //jonny add
    
    
    printk("%s  S1 \n", __func__);  //jonny add

    /* Allocate driver data */
    spidev = kzalloc(sizeof(*spidev), GFP_KERNEL);
    if (!spidev)
        return -ENOMEM;

    printk("%s  S2 \n", __func__);  //jonny add
    
    /* Initialize the driver data */
    spidev->spi = spi;

//jonny S
#if GSL6313_POWER_CTRL
    error = GSL6313_power_init(spidev, true);
    if (error < 0) {
        //printk("%s "power init failed! err=%d"\n", __func__,error);  //jonny add
    	dev_err(&spi->dev, "power init failed! err=%d", error);
    	//goto err_pdata_exit;
    }
    error = GSL6313_power_on(spidev);
    if (error < 0) {
    	//printk("%s "power on failed! err=%d"\n", __func__,error);  //jonny add
    	dev_err(&spi->dev, "power on failed! err=%d\n", error);
    	//goto err_power_deinit;
    }
#endif
//jonny E

/* [PM99] S- BUG#274 Jonny_Chan shutdown active/suspend */
	printk("%s  S3-0 \n", __func__);  //jonny add
	/*Config gpio*/
	error = silead_fp_pinctrl_init(spidev);
	printk("%s  S3-1 \n", __func__);  //jonny add
    if(!error && spidev->fp_pinctrl){
        /*
         * Pinctrl handle is optional. If pinctrl handle is found
         * let pins to be configured in active state. If not
         * found continue further without error.
         */
        error = pinctrl_select_state(spidev->fp_pinctrl,
                    spidev->pinctrl_state_active);
        if (error < 0) {
            dev_err(&spi->dev,
                "[silead]failed to select pin to active state");
        }

	/* [PM99] S- BUG#xxx Jonny_Chan IRQ wake-up control */
#ifdef GSL6313_INTERRUPT_CTRL
        error = pinctrl_select_state(spidev->fp_pinctrl,
                    spidev->pinctrl_state_interrupt);
        if (error < 0) {
            dev_err(&spi->dev,
                "[silead]failed to select pin to interrupt state");
        }
#endif
	/* [PM99] E- BUG#xxx Jonny_Chan IRQ wake-up control */
    }
/* [PM99] E- BUG#274 Jonny_Chan shutdown active/suspend */
#ifdef CONFIG_OF
	spidev->shutdown_gpio = of_get_named_gpio(spi->dev.of_node,"shutdown_gpio",0);

	if (gpio_is_valid(spidev->shutdown_gpio)) {
		status = gpio_request(spidev->shutdown_gpio,
				"silead_shutdown_gpio");
		if (status) {
			dev_err(&spi->dev,
					"reset gpio request failed");
		} else {
                        //gpio_set_value_cansleep(spidev->shutdown_gpio, 1);
                        //gpio_direction_output(spidev->shutdown_gpio, 1);
			dev_err(&spi->dev,
					"reset gpio request success"); // add silead 20150330
		}
	}
/* [PM99] S- BUG#xxx Jonny_Chan IRQ wake-up control */
#ifdef GSL6313_INTERRUPT_CTRL
	irq_flags = IRQF_TRIGGER_RISING | IRQF_ONESHOT;

	//irq_flags = IRQF_TRIGGER_RISING  | IRQF_ONESHOT;
	//irq_flags = IRQF_TRIGGER_FALLING  | IRQF_ONESHOT;
	spidev->wakeup = of_property_read_bool(spi->dev.of_node, "linux,wakeup");
	
	//spidev->int_wakeup_gpio = of_get_named_gpio(spi->dev.of_node,"linux,gpio-int",0);
	spidev->int_wakeup_gpio = of_get_named_gpio_flags(spi->dev.of_node,"linux,gpio-int",0,&tmp);

	if (gpio_is_valid(spidev->int_wakeup_gpio)) {
		status = gpio_request_one(spidev->int_wakeup_gpio, (GPIOF_DIR_IN | GPIOF_INIT_LOW),
				"silead_int_gpio");
		if (status) {
			dev_err(&spi->dev,
					"int gpio request failed");
		} else {
			dev_err(&spi->dev,
					"int gpio request success"); 
		}
	}

	spidev->int_irq= gpio_to_irq(spidev->int_wakeup_gpio);
	status = devm_request_threaded_irq(&spi->dev, spidev->int_irq, NULL,
			finger_interrupt_handler,
			irq_flags, "silead_finger", spidev);
	if (status < 0) {
		dev_err(&spi->dev, "request irq failed : %d\n", spidev->int_irq);
		//goto free_gpio;
	}

	device_init_wakeup(&spi->dev, spidev->wakeup);
	enable_irq_wake(spidev->int_irq);
        irq_counter++;
	//disable_irq_nosync(spidev->int_irq);  //jonny 0820
	printk("%s  Interrupt  %d  wake up is %d irq flag is 0x%X irq_counter is %d\n", __func__,spidev->int_irq, spidev->wakeup,irq_flags,irq_counter);  //jonny add
#endif
/* [PM99] E- BUG#xxx Jonny_Chan IRQ wake-up control */
#endif

	printk("%s  S3 \n", __func__);  //jonny add
	
	spin_lock_init(&spidev->spi_lock);
	mutex_init(&spidev->buf_lock);

	INIT_LIST_HEAD(&spidev->device_entry);
	INIT_WORK(&spidev->work, spidev_work);
	wake_lock_init(&spidev->wake_lock, WAKE_LOCK_SUSPEND, "silead_wake_lock");
	spidev->wqueue = create_singlethread_workqueue("silead_wq");

	/* [PM99] S- BUG#xxx Jonny_Chan IRQ wake-up control */
#ifdef GSL6313_INTERRUPT_CTRL
	spidev->int_wq= create_singlethread_workqueue("int_silead_wq");
	INIT_WORK(&spidev->int_work, finger_interrupt_work);
#endif
	/* [PM99] E- BUG#xxx Jonny_Chan IRQ wake-up control */
	printk("%s  S4 \n", __func__);  //jonny add
	
	spidev->max_frame_num = SL_MAX_FRAME_NUM;
	spidev->max_buf_size =((sizeof(struct sl_frame)*spidev->max_frame_num+PAGE_SIZE)/PAGE_SIZE)*PAGE_SIZE;
	spidev->mmap_buf = kmalloc(spidev->max_buf_size, GFP_KERNEL);
        spidev->tx_mmap_buf = kmalloc(spidev->max_buf_size, GFP_KERNEL);
        memset(spidev->tx_mmap_buf, 0, spidev->max_buf_size);
        memset(spidev->mmap_buf, 0, spidev->max_buf_size);
	if (!spidev->mmap_buf) {
		dev_err(&spi->dev, "alloc kebuffer failedn\n");
		return -ENOMEM;
	}
	
	printk("%s  S5 \n", __func__);  //jonny add
	
	for(page = (unsigned long)spidev->mmap_buf;
			page < (unsigned long)spidev->mmap_buf+spidev->max_buf_size; page+= PAGE_SIZE) {
		SetPageReserved(virt_to_page(page));
	}
	/* If we can allocate a minor number, hook up this device.
	 * Reusing minors is fine so long as udev or mdev is working.
	 */
	mutex_lock(&device_list_lock);
	minor = find_first_zero_bit(minors, N_SPI_MINORS);
	if (minor < N_SPI_MINORS) {
		struct device *dev;

		spidev->devt = MKDEV(spidev_major, minor);
		dev = device_create(spidev_class, &spi->dev, spidev->devt,
				spidev, "silead_fp_dev");
		status = IS_ERR(dev) ? PTR_ERR(dev) : 0;
	} else {
		dev_dbg(&spi->dev, "no minor number available!\n");
		status = -ENODEV;
	}
	if (status == 0) {
		set_bit(minor, minors);
		list_add(&spidev->device_entry, &device_list);
	}
	mutex_unlock(&device_list_lock);

	if (status == 0)
		spi_set_drvdata(spi, spidev);
	else
		kfree(spidev);

	fp_spidev = spidev;
        sl_proc_init(spidev);
        atomic_set(&spidev->is_cal_mode, 0);///default is enroll mode
        //atomic_set(&spidev->is_suspend, 0);///default is enroll mode // 20150820 Kylix remove

	printk("%s  E \n", __func__);  //jonny add
	return status;
}

static int /*__devexit*/ spidev_remove(struct spi_device *spi) //Kylix
{
	struct spidev_data	*spidev = spi_get_drvdata(spi);
	unsigned long page;

	/* make sure ops on existing fds can abort cleanly */
	spin_lock_irq(&spidev->spi_lock);
	spidev->spi = NULL;
	spi_set_drvdata(spi, NULL);
	spin_unlock_irq(&spidev->spi_lock);


	/* prevent new opens */
	mutex_lock(&device_list_lock);
	list_del(&spidev->device_entry);
	device_destroy(spidev_class, spidev->devt);
	clear_bit(MINOR(spidev->devt), minors);
	if (spidev->users == 0) {
		if (spidev->mmap_buf) {
			for(page = (unsigned long)spidev->mmap_buf;
					page < (unsigned long)spidev->mmap_buf+spidev->max_buf_size; page+= PAGE_SIZE) {
				ClearPageReserved(virt_to_page(page));
			}

			kfree(spidev->mmap_buf);
		}
		if (spidev->tx_mmap_buf){
		  kfree(spidev->tx_mmap_buf);
		}
		wake_lock_destroy(&spidev->wake_lock);
		kfree(spidev);
	}
	mutex_unlock(&device_list_lock);
//	spidev_exit(); //remove silead 20150328
	return 0;
}
//////@ 2015  add by joker
static int spidev_reset_low(void)
{
	printk("[2015]: spidev_reset_hight() function called!\n");

	return 0;
}
static int spidev_reset_hight(void)
{
	printk("[2015]: spidev_reset_hight() function called!\n");
	
	return 0;
}
//////@ 2015  add by joker

static int spidev_suspend(struct spi_device *spi, pm_message_t mesg)
{
    //struct spidev_data	*spidev = spi_get_drvdata(spi);  //jonny 0820

    printk("%s  S \n", __func__);  // [PM99] BUG#274 Jonny_Chan shutdown active/suspend

#if 0
    if (spidev->wake_up_gpio) {
        enable_irq(spidev->irq);
    } else {
        disable_irq(spidev->irq);
    }
#endif
		
    //atomic_set(&spidev->is_suspend, 1); // 20150820 Kylix remove

    printk("%s  E \n", __func__);  // [PM99] S- BUG#274 Jonny_Chan shutdown active/suspend
    return 0;
}
static int spidev_resume(struct spi_device *spi)
{
    //struct spidev_data	*spidev = spi_get_drvdata(spi);  //jonny 0820

    printk("%s  S \n", __func__); // [PM99] BUG#274 Jonny_Chan shutdown active/suspend

#if 0    
    if (spidev->wake_up_gpio) {
        enable_irq(spidev->irq);
    } else {
        disable_irq(spidev->irq);
    }
#endif

    //atomic_set(&spidev->is_suspend, 0); // 20150820 Kylix remove
    printk("%s  E \n", __func__); // [PM99] BUG#274 Jonny_Chan shutdown active/suspend
    return 0;
}

#ifdef CONFIG_OF
#define DEVICE_NAME "silead_fp"
static const struct of_device_id spidev_match_table[] = {
	{	.compatible = "silead_fp", },
	{ },
};

static const struct spi_device_id spidev_device_id[] = {
	{ DEVICE_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, spidev_device_id);
static struct spi_driver spidev_spi_driver = {
	.probe = spidev_probe,
	.remove = spidev_remove,
	.suspend = spidev_suspend,
	.resume  = spidev_resume,
	.id_table = spidev_device_id,
	.driver = {
		.name = DEVICE_NAME,
		.owner = THIS_MODULE,
		.bus = &spi_bus_type,
		.of_match_table = spidev_match_table,
	},
};
#else
static struct spi_driver spidev_spi_driver = {
    .driver = {
        .name =		"silead_fp",//spidev
        .owner =	THIS_MODULE,
    },
    .probe =	spidev_probe,
    .remove =	/*__devexit_p*/(spidev_remove), //Kylix

    .suspend = spidev_suspend,
    .resume  = spidev_resume,
    /* NOTE:  suspend/resume methods are not necessary here.
     * We don't do anything except pass the requests to/from
     * the underlying controller.  The refrigerator handles
     * most issues; the controller driver handles the rest.
     */
};
#endif

/*-------------------------------------------------------------------------*/

static int __init spidev_init(void)
{
    int status;
    dev_t devno;

    printk("==============slspi===========\n");

    /* Claim our 256 reserved device numbers.  Then register a class
     * that will key udev/mdev to add/remove /dev nodes.  Last, register
     * the driver which manages those device numbers.
     */
    BUILD_BUG_ON(N_SPI_MINORS > 256);

    /*
        status = register_chrdev(SPIDEV_MAJOR, "spi", &spidev_fops);
        if (status < 0)
            return status;
    */
    status = alloc_chrdev_region(&devno, 0,255, "sileadfp");
    if(status <0 )
        return status;

    spidev_major = MAJOR(devno);
    cdev_init(&spicdev, &spidev_fops);
    spicdev.owner = THIS_MODULE;
    status = cdev_add(&spicdev,MKDEV(spidev_major, 0),N_SPI_MINORS);
    if(status != 0)
        return status;

    spidev_class = class_create(THIS_MODULE, "spidev");
    if (IS_ERR(spidev_class)) {
        unregister_chrdev(spidev_major, spidev_spi_driver.driver.name);
        return PTR_ERR(spidev_class);
    }

    status = spi_register_driver(&spidev_spi_driver);
    if (status < 0) {
        class_destroy(spidev_class);
        unregister_chrdev(spidev_major, spidev_spi_driver.driver.name);
    }
    return status;
}

static void __exit spidev_exit(void)
{
    cdev_del(&spicdev);
    spi_unregister_driver(&spidev_spi_driver);
    class_destroy(spidev_class);
    unregister_chrdev(spidev_major, spidev_spi_driver.driver.name);
}

module_init(spidev_init);
module_exit(spidev_exit);

MODULE_AUTHOR("Andrea Paterniani, <a.paterniani@swapp-eng.it>");
MODULE_DESCRIPTION("User mode SPI device interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:spidev");
