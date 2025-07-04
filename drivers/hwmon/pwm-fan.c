// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * pwm-fan.c - Hwmon driver for fans connected to PWM lines.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *
 * Author: Kamil Debski <k.debski@samsung.com>
 */

#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/interrupt.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/pwm.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/thermal.h>
#include <linux/timer.h>

#define MAX_PWM 255

struct pwm_fan_tach {
	int irq;
	atomic_t pulses;
	unsigned int rpm;
};

enum pwm_fan_enable_mode {
	pwm_off_reg_off,
	pwm_disable_reg_enable,
	pwm_enable_reg_enable,
	pwm_disable_reg_disable,
};

struct pwm_fan_ctx {
	struct device *dev;

	struct mutex lock;
	struct pwm_device *pwm;
	struct pwm_state pwm_state;
	struct regulator *reg_en;
	enum pwm_fan_enable_mode enable_mode;
	bool regulator_enabled;
	bool enabled;

	int tach_count;
	struct pwm_fan_tach *tachs;
	u32 *pulses_per_revolution;
	ktime_t sample_start;
	struct timer_list rpm_timer;

	unsigned int pwm_value;
	unsigned int pwm_fan_state;
	unsigned int pwm_fan_max_state;
	unsigned int *pwm_fan_cooling_levels;
	struct thermal_cooling_device *cdev;

	struct hwmon_chip_info info;
	struct hwmon_channel_info fan_channel;

	u64 pwm_duty_cycle_from_stopped;
	u32 pwm_usec_from_stopped;
};

/* This handler assumes self resetting edge triggered interrupt. */
static irqreturn_t pulse_handler(int irq, void *dev_id)
{
	struct pwm_fan_tach *tach = dev_id;

	atomic_inc(&tach->pulses);

	return IRQ_HANDLED;
}

static void sample_timer(struct timer_list *t)
{
	struct pwm_fan_ctx *ctx = timer_container_of(ctx, t, rpm_timer);
	unsigned int delta = ktime_ms_delta(ktime_get(), ctx->sample_start);
	int i;

	if (delta) {
		for (i = 0; i < ctx->tach_count; i++) {
			struct pwm_fan_tach *tach = &ctx->tachs[i];
			int pulses;

			pulses = atomic_read(&tach->pulses);
			atomic_sub(pulses, &tach->pulses);
			tach->rpm = (unsigned int)(pulses * 1000 * 60) /
				(ctx->pulses_per_revolution[i] * delta);
		}

		ctx->sample_start = ktime_get();
	}

	mod_timer(&ctx->rpm_timer, jiffies + HZ);
}

static void pwm_fan_enable_mode_2_state(int enable_mode,
					struct pwm_state *state,
					bool *enable_regulator)
{
	switch (enable_mode) {
	case pwm_disable_reg_enable:
		/* disable pwm, keep regulator enabled */
		state->enabled = false;
		*enable_regulator = true;
		break;
	case pwm_enable_reg_enable:
		/* keep pwm and regulator enabled */
		state->enabled = true;
		*enable_regulator = true;
		break;
	case pwm_off_reg_off:
	case pwm_disable_reg_disable:
		/* disable pwm and regulator */
		state->enabled = false;
		*enable_regulator = false;
	}
}

static int pwm_fan_switch_power(struct pwm_fan_ctx *ctx, bool on)
{
	int ret = 0;

	if (!ctx->reg_en)
		return ret;

	if (!ctx->regulator_enabled && on) {
		ret = regulator_enable(ctx->reg_en);
		if (ret == 0)
			ctx->regulator_enabled = true;
	} else if (ctx->regulator_enabled && !on) {
		ret = regulator_disable(ctx->reg_en);
		if (ret == 0)
			ctx->regulator_enabled = false;
	}
	return ret;
}

static int pwm_fan_power_on(struct pwm_fan_ctx *ctx)
{
	struct pwm_state *state = &ctx->pwm_state;
	int ret;

	if (ctx->enabled)
		return 0;

	ret = pwm_fan_switch_power(ctx, true);
	if (ret < 0) {
		dev_err(ctx->dev, "failed to enable power supply\n");
		return ret;
	}

	state->enabled = true;
	ret = pwm_apply_might_sleep(ctx->pwm, state);
	if (ret) {
		dev_err(ctx->dev, "failed to enable PWM\n");
		goto disable_regulator;
	}

	ctx->enabled = true;

	return 0;

disable_regulator:
	pwm_fan_switch_power(ctx, false);
	return ret;
}

static int pwm_fan_power_off(struct pwm_fan_ctx *ctx, bool force_disable)
{
	struct pwm_state *state = &ctx->pwm_state;
	bool enable_regulator = false;
	int ret;

	if (!ctx->enabled)
		return 0;

	pwm_fan_enable_mode_2_state(ctx->enable_mode,
				    state,
				    &enable_regulator);

	if (force_disable)
		state->enabled = false;
	state->duty_cycle = 0;
	ret = pwm_apply_might_sleep(ctx->pwm, state);
	if (ret) {
		dev_err(ctx->dev, "failed to disable PWM\n");
		return ret;
	}

	pwm_fan_switch_power(ctx, enable_regulator);

	ctx->enabled = false;

	return 0;
}

static int  __set_pwm(struct pwm_fan_ctx *ctx, unsigned long pwm)
{
	struct pwm_state *state = &ctx->pwm_state;
	unsigned long final_pwm = pwm;
	unsigned long period;
	bool update = false;
	int ret = 0;

	if (pwm > 0) {
		if (ctx->enable_mode == pwm_off_reg_off)
			/* pwm-fan hard disabled */
			return 0;

		period = state->period;
		update = state->duty_cycle < ctx->pwm_duty_cycle_from_stopped;
		if (update)
			state->duty_cycle = ctx->pwm_duty_cycle_from_stopped;
		else
			state->duty_cycle = DIV_ROUND_UP(pwm * (period - 1), MAX_PWM);
		ret = pwm_apply_might_sleep(ctx->pwm, state);
		if (ret)
			return ret;
		ret = pwm_fan_power_on(ctx);
		if (!ret && update) {
			pwm = final_pwm;
			state->duty_cycle = DIV_ROUND_UP(pwm * (period - 1), MAX_PWM);
			usleep_range(ctx->pwm_usec_from_stopped,
				     ctx->pwm_usec_from_stopped * 2);
			ret = pwm_apply_might_sleep(ctx->pwm, state);
		}
	} else {
		ret = pwm_fan_power_off(ctx, false);
	}
	if (!ret)
		ctx->pwm_value = pwm;

	return ret;
}

static int set_pwm(struct pwm_fan_ctx *ctx, unsigned long pwm)
{
	int ret;

	mutex_lock(&ctx->lock);
	ret = __set_pwm(ctx, pwm);
	mutex_unlock(&ctx->lock);

	return ret;
}

static void pwm_fan_update_state(struct pwm_fan_ctx *ctx, unsigned long pwm)
{
	int i;

	for (i = 0; i < ctx->pwm_fan_max_state; ++i)
		if (pwm < ctx->pwm_fan_cooling_levels[i + 1])
			break;

	ctx->pwm_fan_state = i;
}

static int pwm_fan_update_enable(struct pwm_fan_ctx *ctx, long val)
{
	int ret = 0;
	int old_val;

	mutex_lock(&ctx->lock);

	if (ctx->enable_mode == val)
		goto out;

	old_val = ctx->enable_mode;
	ctx->enable_mode = val;

	if (val == 0) {
		/* Disable pwm-fan unconditionally */
		if (ctx->enabled)
			ret = __set_pwm(ctx, 0);
		else
			ret = pwm_fan_switch_power(ctx, false);
		if (ret)
			ctx->enable_mode = old_val;
		pwm_fan_update_state(ctx, 0);
	} else {
		/*
		 * Change PWM and/or regulator state if currently disabled
		 * Nothing to do if currently enabled
		 */
		if (!ctx->enabled) {
			struct pwm_state *state = &ctx->pwm_state;
			bool enable_regulator = false;

			state->duty_cycle = 0;
			pwm_fan_enable_mode_2_state(val,
						    state,
						    &enable_regulator);

			pwm_apply_might_sleep(ctx->pwm, state);
			pwm_fan_switch_power(ctx, enable_regulator);
			pwm_fan_update_state(ctx, 0);
		}
	}
out:
	mutex_unlock(&ctx->lock);

	return ret;
}

static int pwm_fan_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	struct pwm_fan_ctx *ctx = dev_get_drvdata(dev);
	int ret;

	switch (attr) {
	case hwmon_pwm_input:
		if (val < 0 || val > MAX_PWM)
			return -EINVAL;
		ret = set_pwm(ctx, val);
		if (ret)
			return ret;
		pwm_fan_update_state(ctx, val);
		break;
	case hwmon_pwm_enable:
		if (val < 0 || val > 3)
			ret = -EINVAL;
		else
			ret = pwm_fan_update_enable(ctx, val);

		return ret;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int pwm_fan_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct pwm_fan_ctx *ctx = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			*val = ctx->pwm_value;
			return 0;
		case hwmon_pwm_enable:
			*val = ctx->enable_mode;
			return 0;
		}
		return -EOPNOTSUPP;
	case hwmon_fan:
		*val = ctx->tachs[channel].rpm;
		return 0;

	default:
		return -ENOTSUPP;
	}
}

static umode_t pwm_fan_is_visible(const void *data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	switch (type) {
	case hwmon_pwm:
		return 0644;

	case hwmon_fan:
		return 0444;

	default:
		return 0;
	}
}

static const struct hwmon_ops pwm_fan_hwmon_ops = {
	.is_visible = pwm_fan_is_visible,
	.read = pwm_fan_read,
	.write = pwm_fan_write,
};

/* thermal cooling device callbacks */
static int pwm_fan_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct pwm_fan_ctx *ctx = cdev->devdata;

	if (!ctx)
		return -EINVAL;

	*state = ctx->pwm_fan_max_state;

	return 0;
}

static int pwm_fan_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct pwm_fan_ctx *ctx = cdev->devdata;

	if (!ctx)
		return -EINVAL;

	*state = ctx->pwm_fan_state;

	return 0;
}

static int
pwm_fan_set_cur_state(struct thermal_cooling_device *cdev, unsigned long state)
{
	struct pwm_fan_ctx *ctx = cdev->devdata;
	int ret;

	if (!ctx || (state > ctx->pwm_fan_max_state))
		return -EINVAL;

	if (state == ctx->pwm_fan_state)
		return 0;

	ret = set_pwm(ctx, ctx->pwm_fan_cooling_levels[state]);
	if (ret) {
		dev_err(&cdev->device, "Cannot set pwm!\n");
		return ret;
	}

	ctx->pwm_fan_state = state;

	return ret;
}

static const struct thermal_cooling_device_ops pwm_fan_cooling_ops = {
	.get_max_state = pwm_fan_get_max_state,
	.get_cur_state = pwm_fan_get_cur_state,
	.set_cur_state = pwm_fan_set_cur_state,
};

static int pwm_fan_get_cooling_data(struct device *dev, struct pwm_fan_ctx *ctx)
{
	int num, i, ret;

	if (!device_property_present(dev, "cooling-levels"))
		return 0;

	ret = device_property_count_u32(dev, "cooling-levels");
	if (ret <= 0) {
		dev_err(dev, "Wrong data!\n");
		return ret ? : -EINVAL;
	}

	num = ret;
	ctx->pwm_fan_cooling_levels = devm_kcalloc(dev, num, sizeof(u32),
						   GFP_KERNEL);
	if (!ctx->pwm_fan_cooling_levels)
		return -ENOMEM;

	ret = device_property_read_u32_array(dev, "cooling-levels",
					     ctx->pwm_fan_cooling_levels, num);
	if (ret) {
		dev_err(dev, "Property 'cooling-levels' cannot be read!\n");
		return ret;
	}

	for (i = 0; i < num; i++) {
		if (ctx->pwm_fan_cooling_levels[i] > MAX_PWM) {
			dev_err(dev, "PWM fan state[%d]:%d > %d\n", i,
				ctx->pwm_fan_cooling_levels[i], MAX_PWM);
			return -EINVAL;
		}
	}

	ctx->pwm_fan_max_state = num - 1;

	return 0;
}

static void pwm_fan_cleanup(void *__ctx)
{
	struct pwm_fan_ctx *ctx = __ctx;

	timer_delete_sync(&ctx->rpm_timer);
	/* Switch off everything */
	ctx->enable_mode = pwm_disable_reg_disable;
	pwm_fan_power_off(ctx, true);
}

static int pwm_fan_probe(struct platform_device *pdev)
{
	struct thermal_cooling_device *cdev;
	struct device *dev = &pdev->dev;
	struct pwm_fan_ctx *ctx;
	struct device *hwmon;
	int ret;
	const struct hwmon_channel_info **channels;
	u32 initial_pwm, pwm_min_from_stopped = 0;
	u32 *fan_channel_config;
	int channel_count = 1;	/* We always have a PWM channel. */
	int i;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mutex_init(&ctx->lock);

	ctx->dev = &pdev->dev;
	ctx->pwm = devm_pwm_get(dev, NULL);
	if (IS_ERR(ctx->pwm))
		return dev_err_probe(dev, PTR_ERR(ctx->pwm), "Could not get PWM\n");

	platform_set_drvdata(pdev, ctx);

	ctx->reg_en = devm_regulator_get_optional(dev, "fan");
	if (IS_ERR(ctx->reg_en)) {
		if (PTR_ERR(ctx->reg_en) != -ENODEV)
			return PTR_ERR(ctx->reg_en);

		ctx->reg_en = NULL;
	}

	pwm_init_state(ctx->pwm, &ctx->pwm_state);

	/*
	 * PWM fans are controlled solely by the duty cycle of the PWM signal,
	 * they do not care about the exact timing. Thus set usage_power to true
	 * to allow less flexible hardware to work as a PWM source for fan
	 * control.
	 */
	ctx->pwm_state.usage_power = true;

	/*
	 * set_pwm assumes that MAX_PWM * (period - 1) fits into an unsigned
	 * long. Check this here to prevent the fan running at a too low
	 * frequency.
	 */
	if (ctx->pwm_state.period > ULONG_MAX / MAX_PWM + 1) {
		dev_err(dev, "Configured period too big\n");
		return -EINVAL;
	}

	ctx->enable_mode = pwm_disable_reg_enable;

	ret = pwm_fan_get_cooling_data(dev, ctx);
	if (ret)
		return ret;

	/* use maximum cooling level if provided */
	if (ctx->pwm_fan_cooling_levels)
		initial_pwm = ctx->pwm_fan_cooling_levels[ctx->pwm_fan_max_state];
	else
		initial_pwm = MAX_PWM;

	/*
	 * Set duty cycle to maximum allowed and enable PWM output as well as
	 * the regulator. In case of error nothing is changed
	 */
	ret = set_pwm(ctx, initial_pwm);
	if (ret) {
		dev_err(dev, "Failed to configure PWM: %d\n", ret);
		return ret;
	}
	timer_setup(&ctx->rpm_timer, sample_timer, 0);
	ret = devm_add_action_or_reset(dev, pwm_fan_cleanup, ctx);
	if (ret)
		return ret;

	ctx->tach_count = platform_irq_count(pdev);
	if (ctx->tach_count < 0)
		return dev_err_probe(dev, ctx->tach_count,
				     "Could not get number of fan tachometer inputs\n");
	dev_dbg(dev, "%d fan tachometer inputs\n", ctx->tach_count);

	if (ctx->tach_count) {
		channel_count++;	/* We also have a FAN channel. */

		ctx->tachs = devm_kcalloc(dev, ctx->tach_count,
					  sizeof(struct pwm_fan_tach),
					  GFP_KERNEL);
		if (!ctx->tachs)
			return -ENOMEM;

		ctx->fan_channel.type = hwmon_fan;
		fan_channel_config = devm_kcalloc(dev, ctx->tach_count + 1,
						  sizeof(u32), GFP_KERNEL);
		if (!fan_channel_config)
			return -ENOMEM;
		ctx->fan_channel.config = fan_channel_config;

		ctx->pulses_per_revolution = devm_kmalloc_array(dev,
								ctx->tach_count,
								sizeof(*ctx->pulses_per_revolution),
								GFP_KERNEL);
		if (!ctx->pulses_per_revolution)
			return -ENOMEM;

		/* Setup default pulses per revolution */
		for (i = 0; i < ctx->tach_count; i++)
			ctx->pulses_per_revolution[i] = 2;

		device_property_read_u32_array(dev, "pulses-per-revolution",
					       ctx->pulses_per_revolution, ctx->tach_count);
	}

	channels = devm_kcalloc(dev, channel_count + 1,
				sizeof(struct hwmon_channel_info *), GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	channels[0] = HWMON_CHANNEL_INFO(pwm, HWMON_PWM_INPUT | HWMON_PWM_ENABLE);

	for (i = 0; i < ctx->tach_count; i++) {
		struct pwm_fan_tach *tach = &ctx->tachs[i];

		tach->irq = platform_get_irq(pdev, i);
		if (tach->irq == -EPROBE_DEFER)
			return tach->irq;
		if (tach->irq > 0) {
			ret = devm_request_irq(dev, tach->irq, pulse_handler,
					       IRQF_NO_THREAD, pdev->name, tach);
			if (ret) {
				dev_err(dev,
					"Failed to request interrupt: %d\n",
					ret);
				return ret;
			}
		}

		if (!ctx->pulses_per_revolution[i]) {
			dev_err(dev, "pulses-per-revolution can't be zero.\n");
			return -EINVAL;
		}

		fan_channel_config[i] = HWMON_F_INPUT;

		dev_dbg(dev, "tach%d: irq=%d, pulses_per_revolution=%d\n",
			i, tach->irq, ctx->pulses_per_revolution[i]);
	}

	if (ctx->tach_count > 0) {
		ctx->sample_start = ktime_get();
		mod_timer(&ctx->rpm_timer, jiffies + HZ);

		channels[1] = &ctx->fan_channel;
	}

	ret = device_property_read_u32(dev, "fan-stop-to-start-percent",
				       &pwm_min_from_stopped);
	if (!ret && pwm_min_from_stopped) {
		ctx->pwm_duty_cycle_from_stopped =
			DIV_ROUND_UP_ULL(pwm_min_from_stopped *
					 (ctx->pwm_state.period - 1),
					 100);
	}
	ret = device_property_read_u32(dev, "fan-stop-to-start-us",
				       &ctx->pwm_usec_from_stopped);
	if (ret)
		ctx->pwm_usec_from_stopped = 250000;

	ctx->info.ops = &pwm_fan_hwmon_ops;
	ctx->info.info = channels;

	hwmon = devm_hwmon_device_register_with_info(dev, "pwmfan",
						     ctx, &ctx->info, NULL);
	if (IS_ERR(hwmon)) {
		dev_err(dev, "Failed to register hwmon device\n");
		return PTR_ERR(hwmon);
	}

	ctx->pwm_fan_state = ctx->pwm_fan_max_state;
	if (IS_ENABLED(CONFIG_THERMAL)) {
		cdev = devm_thermal_of_cooling_device_register(dev,
			dev->of_node, "pwm-fan", ctx, &pwm_fan_cooling_ops);
		if (IS_ERR(cdev)) {
			ret = PTR_ERR(cdev);
			dev_err(dev,
				"Failed to register pwm-fan as cooling device: %d\n",
				ret);
			return ret;
		}
		ctx->cdev = cdev;
	}

	return 0;
}

static void pwm_fan_shutdown(struct platform_device *pdev)
{
	struct pwm_fan_ctx *ctx = platform_get_drvdata(pdev);

	pwm_fan_cleanup(ctx);
}

static int pwm_fan_suspend(struct device *dev)
{
	struct pwm_fan_ctx *ctx = dev_get_drvdata(dev);

	return pwm_fan_power_off(ctx, true);
}

static int pwm_fan_resume(struct device *dev)
{
	struct pwm_fan_ctx *ctx = dev_get_drvdata(dev);

	return set_pwm(ctx, ctx->pwm_value);
}

static DEFINE_SIMPLE_DEV_PM_OPS(pwm_fan_pm, pwm_fan_suspend, pwm_fan_resume);

static const struct of_device_id of_pwm_fan_match[] = {
	{ .compatible = "pwm-fan", },
	{},
};
MODULE_DEVICE_TABLE(of, of_pwm_fan_match);

static struct platform_driver pwm_fan_driver = {
	.probe		= pwm_fan_probe,
	.shutdown	= pwm_fan_shutdown,
	.driver	= {
		.name		= "pwm-fan",
		.pm		= pm_sleep_ptr(&pwm_fan_pm),
		.of_match_table	= of_pwm_fan_match,
	},
};

module_platform_driver(pwm_fan_driver);

MODULE_AUTHOR("Kamil Debski <k.debski@samsung.com>");
MODULE_ALIAS("platform:pwm-fan");
MODULE_DESCRIPTION("PWM FAN driver");
MODULE_LICENSE("GPL");
