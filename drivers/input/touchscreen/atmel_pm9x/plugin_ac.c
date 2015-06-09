/*
 * Atmel maXTouch Touchscreen driver Plug in
 *
 * Copyright (C) 2013 Atmel Co.Ltd
 *
 * Author: Pitter Liao <pitter.liao@atmel.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

/****************************************************************  
	Pitter Liao add for macro for the global platform
		email:  pitter.liao@atmel.com 
		mobile: 13244776877
-----------------------------------------------------------------*/
#define PLUG_AC_VERSION 0x0001
/*----------------------------------------------------------------

*/

//first version for AC Plug: just watch T72 status

#include "plug.h"

#define AC_FLAG_RESETING					(1<<0)
#define AC_FLAG_CALING					(1<<1)

#define AC_FLAG_RESET					(1<<4)
#define AC_FLAG_CAL						(1<<5)
#define AC_FLAG_RESUME					(1<<6)

#define AC_FLAG_STABLE					(1<<16)
#define AC_FLAG_NOISE					(1<<17)
#define AC_FLAG_VERY_NOISE				(1<<18)
#define AC_FLAG_STATE_CHANGE			(1<<19)

#define AC_FLAG_WORKAROUND_HALT		(1<<31)

#define AC_FLAG_MASK_LOW			(0x000f0)
#define AC_FLAG_MASK_NORMAL		(0x00f00)
#define AC_NOISE_MASK				(0xf0000)
#define AC_FLAG_MASK				(-1)

/* Log */
#define AC_LOG_TAG "atmel AC"

#define AC_DYNAMIC_LOG

#define AC_LOG_LEVEL_ERROR   1
#define AC_LOG_LEVEL_WARRING 2
#define AC_LOG_LEVEL_INFO    4
#define AC_LOG_LEVEL_DEBUG   8

#ifdef AC_DYNAMIC_LOG
static unsigned int ac_dynamic_log_level = AC_LOG_LEVEL_INFO;

#define ac_dynamic_print(level, fmt, arg...) \
	do { \
		if (level <= ac_dynamic_log_level) \
			printk(fmt, ##arg); \
	} while(0)
#else
#define ac_dynamic_print(level, fmt, arg...) \
	printk(fmt, ##arg);
#endif /* CLP_DYNAMIC_LOG */

#define AC_LOGE(fmt, arg...) \
	ac_dynamic_print(AC_LOG_LEVEL_ERROR, \
			"[%s][error] %s: "fmt, AC_LOG_TAG, __func__, ##arg)
#define AC_LOGW(fmt, arg...) \
	ac_dynamic_print(AC_LOG_LEVEL_WARRING, \
			"[%s][warring] %s: "fmt, AC_LOG_TAG, __func__, ##arg)
#define AC_LOGI(fmt, arg...) \
	ac_dynamic_print(AC_LOG_LEVEL_INFO, \
			"[%s][info] %s: "fmt, AC_LOG_TAG, __func__, ##arg)
#define AC_LOGD(fmt, arg...) \
	ac_dynamic_print(AC_LOG_LEVEL_DEBUG, \
			"[%s][debug] %s: "fmt, AC_LOG_TAG, __func__, ##arg)

struct ac_observer{
	unsigned long flag;
};

struct ac_config{
	unsigned long rsv;
};

static void plugin_ac_hook_t6(struct plugin_ac *p, u8 status)
{
	struct ac_observer *obs = p->obs;

	if (status & (MXT_T6_STATUS_RESET|MXT_T6_STATUS_CAL)) {
		AC_LOGD("AC hook T6 0x%x\n", status);
		
		if (status & MXT_T6_STATUS_CAL) {
			set_and_clr_flag(AC_FLAG_CALING,
					0, &obs->flag);
		}
		
		if (status & MXT_T6_STATUS_RESET) {
			set_and_clr_flag(AC_FLAG_RESETING,
					AC_FLAG_MASK_NORMAL, &obs->flag);
		}
	} else {
		if (test_flag(AC_FLAG_RESETING, &obs->flag))
			set_and_clr_flag(AC_FLAG_RESET,
					AC_FLAG_RESETING, &obs->flag);
		if (test_flag(AC_FLAG_CALING, &obs->flag))
			set_and_clr_flag(AC_FLAG_CAL,
					AC_FLAG_CALING, &obs->flag);

		AC_LOGD("AC hook T6 end\n");
	}

	AC_LOGD("mxt ac flag=0x%lx %x\n", obs->flag, status);
}

static void plugin_ac_hook_t72(struct plugin_ac *p, u8 *msg)
{
	struct ac_observer *obs = (struct ac_observer *)p->obs;
	int state,dualx;
	
	state = msg[2] & T72_NOISE_STATE_MASK;
	dualx = msg[2] & T72_NOISE_DUALX_MASK;

	AC_LOGI("mxt hook ac %d(%d,%d,%d)\n",
		state, NOISE_STABLE, NOISE_NOISY, NOISE_VERY_NOISY);

	if (state == NOISE_STABLE) {
		if (!test_flag(AC_FLAG_STABLE, &obs->flag)) {
			set_and_clr_flag(AC_FLAG_STABLE | AC_FLAG_STATE_CHANGE,
					AC_NOISE_MASK,
					&obs->flag);

			p->set_and_clr_flag(p->dev, 0, PL_STATUS_FLAG_NOISE_MASK);
		}
	} else if (state == NOISE_NOISY) {
		if (!test_flag(AC_FLAG_NOISE, &obs->flag)) {
			set_and_clr_flag(AC_FLAG_NOISE | AC_FLAG_STATE_CHANGE,
					AC_NOISE_MASK,
					&obs->flag);

			p->set_and_clr_flag(p->dev, PL_STATUS_FLAG_NOISE,
					PL_STATUS_FLAG_NOISE_MASK);
		}
	} else if (state == NOISE_VERY_NOISY) {
		if (!test_flag(AC_FLAG_VERY_NOISE | AC_FLAG_STATE_CHANGE, &obs->flag)) {
			set_and_clr_flag(AC_FLAG_VERY_NOISE, AC_NOISE_MASK, &obs->flag);

			p->set_and_clr_flag(p->dev, PL_STATUS_FLAG_VERY_NOISE,
					PL_STATUS_FLAG_NOISE_MASK);
		}
	} else {
		AC_LOGI("mxt hook ac unknow status %d\n", state);
	}

	if (dualx) {
		AC_LOGI("mxt hook ac dualx %d\n", dualx);
		p->set_and_clr_flag(p->dev, PL_STATUS_FLAG_DUALX, 0);
	} else {
		p->set_and_clr_flag(p->dev, 0, PL_STATUS_FLAG_DUALX);
	}
}

static int mxt_proc_noise_msg(struct plugin_ac *p, unsigned long pl_flag)
{
	struct ac_observer *obs = p->obs;

	AC_LOGI("mxt ac at mxt_proc_noise_msg flag 0x%lx pl_flag 0x%lx\n",
			obs->flag, pl_flag);
	
	//very noise
	if (test_flag(AC_FLAG_VERY_NOISE, &obs->flag)) {
		AC_LOGI("mxt ac enter very noise state\n");
	//noise
	} else if (test_flag(AC_FLAG_NOISE, &obs->flag)) {
		AC_LOGI("mxt ac enter noise state\n");
	//stable
	} else {
		AC_LOGI("mxt ac enter very stable state\n");
	}

	clear_flag(AC_FLAG_STATE_CHANGE, &obs->flag);
	p->set_and_clr_flag(p->dev, PL_STATUS_FLAG_NOISE_CHANGE, 0);

	return 0;
}

static void plugin_ac_start(struct plugin_ac *p, bool resume)
{
	struct ac_observer *obs = p->obs;

	clear_flag(AC_FLAG_WORKAROUND_HALT, &obs->flag);

	if (resume)
		set_flag(AC_FLAG_RESUME, &obs->flag);
}

static void plugin_ac_stop(struct plugin_ac *p)
{
	struct ac_observer *obs = p->obs;

	set_and_clr_flag(AC_FLAG_WORKAROUND_HALT, AC_FLAG_RESUME, &obs->flag);
}

static long plugin_ac_post_process_messages(struct plugin_ac *p,
		unsigned long pl_flag)
{
	struct ac_observer *obs = p->obs;
	long interval = MAX_SCHEDULE_TIMEOUT;
	
	if (test_flag(AC_FLAG_WORKAROUND_HALT, &obs->flag))
		return interval;

	if (test_flag(AC_FLAG_RESETING | AC_FLAG_CALING, &obs->flag))
		return interval;

	if (test_flag(AC_FLAG_STATE_CHANGE, &obs->flag)) {
		mxt_proc_noise_msg(p, pl_flag);
	}

	clear_flag(AC_FLAG_MASK_LOW, &obs->flag);

	return interval;
}

static int plugin_ac_show(struct plugin_ac *p)
{
	struct ac_observer * obs = p->obs;

	AC_LOGI("PLUG_AC_VERSION: 0x%x\n",PLUG_AC_VERSION);
	
	if (!p->init)
		return 0;

	AC_LOGI("AC cfg :\n");

	AC_LOGI("AC obs :\n");
	AC_LOGI("status: Flag=0x%08lx\n", obs->flag);

	return 0;
}


static int plugin_ac_store(struct plugin_ac *p, const char *buf, size_t count)
{
	struct ac_observer * obs = p->obs;

	AC_LOGI("ac store:%s\n", buf);

	if (!p->init)
		return 0;

	if (sscanf(buf, "status: Flag=0x%lx\n", &obs->flag) > 0) {
		AC_LOGI("OK\n");
	} else {
		AC_LOGI("BAD\n");
	}
	
	return 0;
}

static int init_ac_object(struct plugin_ac *p)
{ 
	return 0;
}

static int deinit_ac_object(struct plugin_ac *p)
{
	return 0;
}


static int plugin_ac_init(struct plugin_ac *p)
{
	AC_LOGI("plugin ac version 0x%x\n", PLUG_AC_VERSION);

	p->obs = kzalloc(sizeof(struct ac_observer), GFP_KERNEL);
	if (!p->obs) {
		AC_LOGE("Failed to allocate memory for ac observer\n");
		return -ENOMEM;
	}

	p->cfg = kzalloc(sizeof(struct ac_config), GFP_KERNEL);
	if (!p->cfg) {
		AC_LOGE("Failed to allocate memory for ac cfg\n");
		kfree(p->obs);
		p->obs =NULL;
		return -ENOMEM;
	}

	if (init_ac_object(p) != 0) {
		AC_LOGE("Failed to allocate memory for ac cfg\n");
		kfree(p->obs);
		p->obs = NULL;
		kfree(p->cfg);
		p->cfg = NULL;
	}
	
	return  0;
}

static void plugin_ac_deinit(struct plugin_ac *p)
{
	if (p->obs) {
		deinit_ac_object(p);
		kfree(p->obs);
	}
	if (p->cfg)
		kfree(p->cfg);
}

static struct plugin_ac mxt_plugin_ac_if = {
	.init = plugin_ac_init,
	.deinit = plugin_ac_deinit,
	.start = plugin_ac_start,
	.stop = plugin_ac_stop,
	.hook_t6 = plugin_ac_hook_t6,
	.hook_t72 = plugin_ac_hook_t72,
	.post_process = plugin_ac_post_process_messages,
	.show = plugin_ac_show,
	.store = plugin_ac_store,
};

int plugin_interface_ac_init(struct plugin_ac *p)
{
	memcpy(p, &mxt_plugin_ac_if, sizeof(struct plugin_ac));

	return 0;
}

