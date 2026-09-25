// SPDX-License-Identifier: GPL-2.0
/*
 * Monochrome keyboard backlight driver for ITE 8291 (rev 0.03) per-key RGB
 * controllers found in Uniwill X6FR/X6AR based laptops (TUXEDO Stellaris 16
 * Gen7, PCSpecialist Recoil 16, ...).
 *
 * The whole keyboard is driven with a single colour-corrected white and
 * exposed as one "*::kbd_backlight" LED, so UPower and desktop environments
 * pick it up and handle KEY_KBDILLUMUP/DOWN (Fn+F6/F7) including their OSD.
 *
 * Protocol handling is based on hid-ite8291r3 by Barnabás Pőcze, colour
 * correction values are taken from tuxedo-drivers (ite_8291).
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/dmi.h>
#include <linux/hid.h>
#include <linux/jiffies.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/usb.h>

/* ========================================================================== */

#define ITE8291_NUM_ROWS	6
#define ITE8291_NUM_COLS	21
#define ITE8291_MAX_BRIGHTNESS	50

#define ITE8291_ROW_COLOR_OFFSET 2
#define ITE8291_ROW_RED_OFFSET   (ITE8291_ROW_COLOR_OFFSET + 2 * ITE8291_NUM_COLS)
#define ITE8291_ROW_GREEN_OFFSET (ITE8291_ROW_COLOR_OFFSET + 1 * ITE8291_NUM_COLS)
#define ITE8291_ROW_BLUE_OFFSET  (ITE8291_ROW_COLOR_OFFSET + 0 * ITE8291_NUM_COLS)

#define ITE8291_HID_REPORT_LENGTH 9

#define ITE8291_SET_EFFECT	8
#define ITE8291_SET_BRIGHTNESS	9
#define ITE8291_SET_ROW_INDEX	22
#define ITE8291_GET_FW_VERSION	128

#define ITE8291_EFFECT_ON	0x02
#define ITE8291_MODE_USER	0x33

#define ITE8291_LED_NAME	"ite8291:white:" LED_FUNCTION_KBD_BACKLIGHT

/* ========================================================================== */

static int red_scale = -1;
module_param(red_scale, int, 0444);
MODULE_PARM_DESC(red_scale, "Red channel scale 0-255 (-1 = auto from DMI)");

static int green_scale = -1;
module_param(green_scale, int, 0444);
MODULE_PARM_DESC(green_scale, "Green channel scale 0-255 (-1 = auto from DMI)");

static int blue_scale = -1;
module_param(blue_scale, int, 0444);
MODULE_PARM_DESC(blue_scale, "Blue channel scale 0-255 (-1 = auto from DMI)");

static int default_brightness = ITE8291_MAX_BRIGHTNESS / 2;
module_param(default_brightness, int, 0444);
MODULE_PARM_DESC(default_brightness, "Brightness applied at probe, 0-50 (systemd-backlight restores the saved value afterwards)");

/* ========================================================================== */

struct ite8291_white {
	u8 red, green, blue;
};

/*
 * Scaling applied to full white so the keyboard does not look pink/green.
 * Values from tuxedo-drivers' color_scaling(); dmi_match() is a substring
 * match, so "X6FR5" covers both TUXEDO's "X6FR5xxY" and PCSpecialist's
 * "X6FR57TY" board names.
 */
static const struct dmi_system_id ite8291_white_table[] = {
	{
		.ident = "Stellaris 16 Gen7 AMD / Recoil 16 AMD",
		.matches = { DMI_MATCH(DMI_BOARD_NAME, "X6FR5") },
		.driver_data = &(struct ite8291_white){ 170, 255, 125 },
	},
	{
		.ident = "Stellaris 16 Gen7 Intel",
		.matches = { DMI_MATCH(DMI_BOARD_NAME, "X6AR5") },
		.driver_data = &(struct ite8291_white){ 170, 255, 125 },
	},
	{ }
};

/* tuxedo-drivers' fallback for unlisted devices */
static const struct ite8291_white ite8291_white_default = { 255, 126, 120 };

/* ========================================================================== */

struct ite8291_priv {
	struct hid_device *hdev;
	struct led_classdev led;

	struct timer_list put_timer;
	bool intf_gotten;

	struct ite8291_white white;
	u8 brightness;	/* last requested, 0 = off */
	bool lit;	/* user mode + colour rows written since last off/reset */

	struct mutex lock; /* protects everything below hdev */

	u8 transfer_buf[ITE8291_HID_REPORT_LENGTH];
	u8 row_buf[ITE8291_ROW_COLOR_OFFSET + 3 * ITE8291_NUM_COLS];
};

/* ========================================================================== */

/*
 * The controller supports USB autosuspend; keep the interface awake while
 * talking to it and release it a few seconds after the last access.
 */
static void ite8291_put_timeout(struct timer_list *timer)
{
	struct ite8291_priv *p = container_of(timer, struct ite8291_priv, put_timer);

	if (!mutex_trylock(&p->lock))
		return;

	if (p->intf_gotten) {
		usb_autopm_put_interface(to_usb_interface(p->hdev->dev.parent));
		p->intf_gotten = false;
	}

	mutex_unlock(&p->lock);
}

static int ite8291_lock_and_get(struct ite8291_priv *p)
{
	int err;

	mutex_lock(&p->lock);

	if (!p->intf_gotten) {
		err = usb_autopm_get_interface(to_usb_interface(p->hdev->dev.parent));
		if (err) {
			mutex_unlock(&p->lock);
			return err;
		}
		p->intf_gotten = true;
	}

	return 0;
}

static void ite8291_put_and_unlock(struct ite8291_priv *p)
{
	mod_timer(&p->put_timer, jiffies + msecs_to_jiffies(5000));
	mutex_unlock(&p->lock);
}

/* ========================================================================== */

/* p->lock held; sends p->transfer_buf as a feature report */
static int ite8291_send(struct ite8291_priv *p)
{
	int err;

	err = hid_hw_raw_request(p->hdev, 0, p->transfer_buf, sizeof(p->transfer_buf),
				 HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (err < 0)
		return err;
	if (err != sizeof(p->transfer_buf))
		return -EIO;

	return 0;
}

static int ite8291_receive(struct ite8291_priv *p)
{
	int err;

	memset(p->transfer_buf, 0, sizeof(p->transfer_buf));
	err = hid_hw_raw_request(p->hdev, 0, p->transfer_buf, sizeof(p->transfer_buf),
				 HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (err < 0)
		return err;
	if (err != sizeof(p->transfer_buf))
		return -ENODATA;

	return 0;
}

static int ite8291_command(struct ite8291_priv *p, u8 cmd, u8 a, u8 b, u8 c, u8 d)
{
	memset(p->transfer_buf, 0, sizeof(p->transfer_buf));
	p->transfer_buf[1] = cmd;
	p->transfer_buf[2] = a;
	p->transfer_buf[3] = b;
	p->transfer_buf[4] = c;
	p->transfer_buf[5] = d;

	return ite8291_send(p);
}

/* p->lock held: switch to static user mode and paint every key white */
static int ite8291_write_state(struct ite8291_priv *p)
{
	unsigned int row, col;
	int err;

	err = ite8291_command(p, ITE8291_SET_EFFECT, ITE8291_EFFECT_ON,
			      ITE8291_MODE_USER, 0x00, p->brightness);
	if (err)
		return err;

	memset(p->row_buf, 0, sizeof(p->row_buf));
	for (col = 0; col < ITE8291_NUM_COLS; col++) {
		p->row_buf[ITE8291_ROW_RED_OFFSET + col]   = p->white.red;
		p->row_buf[ITE8291_ROW_GREEN_OFFSET + col] = p->white.green;
		p->row_buf[ITE8291_ROW_BLUE_OFFSET + col]  = p->white.blue;
	}

	for (row = 0; row < ITE8291_NUM_ROWS; row++) {
		err = ite8291_command(p, ITE8291_SET_ROW_INDEX, 0x00, row, 0x00, 0x00);
		if (err)
			return err;

		err = hid_hw_output_report(p->hdev, p->row_buf, sizeof(p->row_buf));
		if (err < 0)
			return err;
	}

	p->lit = true;
	return 0;
}

/*
 * p->lock held. Brightness 0 is a plain SET_BRIGHTNESS like in hid-ite8291r3:
 * the "effect off" command is not reliable on rev 0.03 firmware (keys can end
 * up fully lit), and staying in static user mode keeps the colour rows valid.
 */
static int ite8291_apply(struct ite8291_priv *p)
{
	if (!p->lit)
		return ite8291_write_state(p);

	return ite8291_command(p, ITE8291_SET_BRIGHTNESS, 0x02, p->brightness, 0, 0);
}

/* ========================================================================== */

static int ite8291_led_set(struct led_classdev *led_cdev, enum led_brightness value)
{
	struct ite8291_priv *p = container_of(led_cdev, struct ite8291_priv, led);
	int err;

	if (led_cdev->flags & LED_UNREGISTERING)
		return 0;

	err = ite8291_lock_and_get(p);
	if (err)
		return err;

	p->brightness = min_t(unsigned int, value, ITE8291_MAX_BRIGHTNESS);
	err = ite8291_apply(p);

	ite8291_put_and_unlock(p);
	return err;
}

/* ========================================================================== */

static void ite8291_pick_white(struct ite8291_priv *p)
{
	const struct dmi_system_id *id = dmi_first_match(ite8291_white_table);
	const struct ite8291_white *w = id ? id->driver_data : &ite8291_white_default;

	p->white = *w;

	if (red_scale >= 0)
		p->white.red = min(red_scale, 255);
	if (green_scale >= 0)
		p->white.green = min(green_scale, 255);
	if (blue_scale >= 0)
		p->white.blue = min(blue_scale, 255);

	hid_info(p->hdev, "white balance r=%u g=%u b=%u (%s)\n",
		 p->white.red, p->white.green, p->white.blue,
		 id ? id->ident : "default");
}

static int ite8291_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct ite8291_priv *p;
	int err;

	if (le16_to_cpu(usb_dev->descriptor.bcdDevice) != 0x0003) {
		hid_warn(hdev, "unsupported bcdDevice %#06x\n",
			 le16_to_cpu(usb_dev->descriptor.bcdDevice));
		return -ENODEV;
	}

	p = devm_kzalloc(&hdev->dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->hdev = hdev;
	mutex_init(&p->lock);
	timer_setup(&p->put_timer, ite8291_put_timeout, 0);
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

	ite8291_pick_white(p);

	err = ite8291_lock_and_get(p);
	if (err)
		goto err_close;

	err = ite8291_command(p, ITE8291_GET_FW_VERSION, 0, 0, 0, 0);
	if (!err)
		err = ite8291_receive(p);
	if (!err)
		hid_info(hdev, "firmware version %*ph\n", 4, &p->transfer_buf[2]);

	p->brightness = clamp(default_brightness, 0, ITE8291_MAX_BRIGHTNESS);
	if (!err)
		err = ite8291_apply(p);

	ite8291_put_and_unlock(p);
	if (err) {
		hid_err(hdev, "failed to initialise controller: %d\n", err);
		goto err_close;
	}

	p->led.name = ITE8291_LED_NAME;
	p->led.max_brightness = ITE8291_MAX_BRIGHTNESS;
	p->led.brightness = p->brightness;
	p->led.brightness_set_blocking = ite8291_led_set;
	p->led.flags = LED_CORE_SUSPENDRESUME | LED_RETAIN_AT_SHUTDOWN;

	err = led_classdev_register(&hdev->dev, &p->led);
	if (err)
		goto err_close;

	return 0;

err_close:
	timer_delete_sync(&p->put_timer);
	if (p->intf_gotten)
		usb_autopm_put_interface(intf);
	hid_hw_close(hdev);
err_stop:
	hid_hw_stop(hdev);
	return err;
}

static void ite8291_remove(struct hid_device *hdev)
{
	struct ite8291_priv *p = hid_get_drvdata(hdev);

	led_classdev_unregister(&p->led);
	timer_delete_sync(&p->put_timer);
	if (p->intf_gotten)
		usb_autopm_put_interface(to_usb_interface(hdev->dev.parent));

	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

/*
 * After a USB reset (e.g. resume from hibernation) the controller falls back
 * to its firmware effect, so the static white state has to be rewritten.
 */
static int ite8291_reset_resume(struct hid_device *hdev)
{
	struct ite8291_priv *p = hid_get_drvdata(hdev);
	int err;

	err = ite8291_lock_and_get(p);
	if (err)
		return err;

	p->lit = false;
	err = ite8291_apply(p);

	ite8291_put_and_unlock(p);
	return err;
}

/* ========================================================================== */

static const struct hid_device_id ite8291_device_ids[] = {
	{ HID_USB_DEVICE(0x048d, 0x6004) },
	{ HID_USB_DEVICE(0x048d, 0x6006) },
	{ HID_USB_DEVICE(0x048d, 0x600b) },
	{ HID_USB_DEVICE(0x048d, 0xce00) },
	{ }
};
MODULE_DEVICE_TABLE(hid, ite8291_device_ids);

static struct hid_driver ite8291_driver = {
	.name = KBUILD_MODNAME,
	.id_table = ite8291_device_ids,
	.probe = ite8291_probe,
	.remove = ite8291_remove,
#ifdef CONFIG_PM
	.reset_resume = ite8291_reset_resume,
#endif
};
module_hid_driver(ite8291_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("amad3v");
MODULE_AUTHOR("Barnabás Pőcze <pobrn@protonmail.com> (hid-ite8291r3)");
MODULE_DESCRIPTION("ITE8291 (rev 0.03) monochrome keyboard backlight driver");
