// SPDX-License-Identifier: GPL-2.0
/*
 * RGB lightbar driver for the ITE 8233 lightbar controller (048d:7001) found
 * in Uniwill X6FR/X6AR based laptops (TUXEDO Stellaris 16 Gen7,
 * PCSpecialist Recoil 16, ...).
 *
 * Exposed as a multicolor LED "rgb:lightbar": multi_intensity selects the
 * colour, brightness (0-100) the lightbar brightness.
 *
 * Protocol from tuxedo-drivers (ite_8291_lb).
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/hid.h>
#include <linux/led-class-multicolor.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>

#define LIGHTBAR_MAX_BRIGHTNESS	100
#define LIGHTBAR_REPORT_LENGTH	9	/* report ID 0 + 8 bytes */

#define LIGHTBAR_SET_COLOR	0x14
#define LIGHTBAR_SET_EFFECT	0x08
#define LIGHTBAR_EFFECT_7001	0x22
#define LIGHTBAR_MODE_STATIC	0x01

static int default_brightness;
module_param(default_brightness, int, 0444);
MODULE_PARM_DESC(default_brightness, "Brightness applied at probe, 0-100 (default 0 = off)");

static uint default_color = 0xffffff;
module_param(default_color, uint, 0444);
MODULE_PARM_DESC(default_color, "Colour applied at probe as 0xRRGGBB (default white)");

struct lightbar_priv {
	struct hid_device *hdev;
	struct led_classdev_mc mc;
	struct mc_subled subleds[3];
	struct mutex lock; /* serialises controller access */
	u8 buf[LIGHTBAR_REPORT_LENGTH];
};

static int lightbar_command(struct lightbar_priv *p, const u8 cmd[8])
{
	int err;

	p->buf[0] = 0;
	memcpy(&p->buf[1], cmd, 8);

	err = hid_hw_raw_request(p->hdev, 0, p->buf, sizeof(p->buf),
				 HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (err < 0)
		return err;

	return err == sizeof(p->buf) ? 0 : -EIO;
}

/* p->lock held */
static int lightbar_write(struct lightbar_priv *p, enum led_brightness brightness)
{
	const u8 set_color[8] = {
		LIGHTBAR_SET_COLOR, 0x00, 0x01,
		p->subleds[0].intensity, p->subleds[1].intensity, p->subleds[2].intensity,
	};
	const u8 set_static[8] = {
		LIGHTBAR_SET_EFFECT, LIGHTBAR_EFFECT_7001, LIGHTBAR_MODE_STATIC,
		0x01, brightness, 0x01,
	};
	int err;

	err = lightbar_command(p, set_color);
	if (err)
		return err;

	return lightbar_command(p, set_static);
}

static int lightbar_set(struct led_classdev *led_cdev, enum led_brightness brightness)
{
	struct led_classdev_mc *mc = lcdev_to_mccdev(led_cdev);
	struct lightbar_priv *p = container_of(mc, struct lightbar_priv, mc);
	int err;

	mutex_lock(&p->lock);
	err = lightbar_write(p, brightness);
	mutex_unlock(&p->lock);

	return err;
}

static int lightbar_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct lightbar_priv *p;
	int err;

	p = devm_kzalloc(&hdev->dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->hdev = hdev;
	mutex_init(&p->lock);
	hid_set_drvdata(hdev, p);

	err = hid_parse(hdev);
	if (err)
		return err;

	err = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (err)
		return err;

	err = hid_hw_open(hdev);
	if (err)
		goto err_stop;

	p->subleds[0] = (struct mc_subled){ .color_index = LED_COLOR_ID_RED,
					    .intensity = (default_color >> 16) & 0xff };
	p->subleds[1] = (struct mc_subled){ .color_index = LED_COLOR_ID_GREEN,
					    .intensity = (default_color >> 8) & 0xff };
	p->subleds[2] = (struct mc_subled){ .color_index = LED_COLOR_ID_BLUE,
					    .intensity = default_color & 0xff };

	p->mc.num_colors = ARRAY_SIZE(p->subleds);
	p->mc.subled_info = p->subleds;
	p->mc.led_cdev.name = "rgb:lightbar";
	p->mc.led_cdev.max_brightness = LIGHTBAR_MAX_BRIGHTNESS;
	p->mc.led_cdev.brightness = clamp(default_brightness, 0, LIGHTBAR_MAX_BRIGHTNESS);
	p->mc.led_cdev.brightness_set_blocking = lightbar_set;
	p->mc.led_cdev.flags = LED_CORE_SUSPENDRESUME;

	mutex_lock(&p->lock);
	err = lightbar_write(p, p->mc.led_cdev.brightness);
	mutex_unlock(&p->lock);
	if (err) {
		hid_err(hdev, "failed to initialise lightbar: %d\n", err);
		goto err_close;
	}

	err = led_classdev_multicolor_register(&hdev->dev, &p->mc);
	if (err)
		goto err_close;

	return 0;

err_close:
	hid_hw_close(hdev);
err_stop:
	hid_hw_stop(hdev);
	return err;
}

static void lightbar_remove(struct hid_device *hdev)
{
	struct lightbar_priv *p = hid_get_drvdata(hdev);

	led_classdev_multicolor_unregister(&p->mc);
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static int lightbar_reset_resume(struct hid_device *hdev)
{
	struct lightbar_priv *p = hid_get_drvdata(hdev);
	int err;

	mutex_lock(&p->lock);
	err = lightbar_write(p, p->mc.led_cdev.brightness);
	mutex_unlock(&p->lock);

	return err;
}

static const struct hid_device_id lightbar_device_ids[] = {
	{ HID_USB_DEVICE(0x048d, 0x7001) },
	{ }
};
MODULE_DEVICE_TABLE(hid, lightbar_device_ids);

static struct hid_driver lightbar_driver = {
	.name = KBUILD_MODNAME,
	.id_table = lightbar_device_ids,
	.probe = lightbar_probe,
	.remove = lightbar_remove,
#ifdef CONFIG_PM
	.reset_resume = lightbar_reset_resume,
#endif
};
module_hid_driver(lightbar_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("amad3v");
MODULE_DESCRIPTION("ITE 8233 (048d:7001) RGB lightbar driver");
