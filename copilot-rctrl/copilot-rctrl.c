// SPDX-License-Identifier: GPL-2.0
/*
 * Turn the Copilot key back into Right Ctrl on the PCSpecialist Recoil 16 AMD
 * (TUXEDO Stellaris 16 Gen7 chassis), without a virtual input device.
 *
 * The EC reports the key as a scancode burst on the i8042 keyboard port
 * (set 1, ~3 ms apart, repeated as a whole while held):
 *
 *   press:   E0 5B  2A  6E     (Left Meta, Left Shift, F23)
 *   release: EE  AA  E0 DB     (F23, Left Shift, Left Meta)
 *
 * An i8042 filter hides the fake modifiers and hands atkbd the Right Ctrl
 * codes instead (E0 1D / E0 9D). E0 prefixes are never held back; they
 * reach atkbd in place, so the substituted byte completes them.
 *
 * A lone Super press also starts with E0 5B: 5B is held back until the next
 * byte shows it is not the Copilot burst, or for at most HOLD_MS, and then
 * delivered unchanged.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/dmi.h>
#include <linux/i8042.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/serio.h>
#include <linux/spinlock.h>
#include <linux/timer.h>

#define I8042_STR_AUXDATA	0x20	/* status: byte is from the AUX (touchpad) port */
#define HOLD_MS			15	/* burst bytes arrive ~3 ms apart */

#define SC_E0			0xe0
#define SC_META			0x5b	/* after E0 */
#define SC_SHIFT		0x2a
#define SC_F23			0x6e
#define SC_CTRL			0x1d	/* after E0: Right Ctrl */
#define SC_BREAK		0x80

/* byte sequences after which the next byte completes the substitution */
static const u8 press_seq[] = { SC_META, SC_SHIFT };			/* then F23 */
static const u8 release_seq[] = { SC_F23 | SC_BREAK, SC_SHIFT | SC_BREAK };	/* then E0 DB */

enum match {
	MATCH_NONE,
	MATCH_PRESS,	/* holding press_seq[0..held) after an E0 */
	MATCH_RELEASE,	/* holding release_seq[0..held) */
	MATCH_RELEASE_E0,	/* release_seq held, E0 passed, waiting for DB */
};

static struct {
	/* protects the fields below; held while injecting into serio */
	spinlock_t lock;
	struct timer_list timer;
	struct serio *serio;
	bool last_was_e0;
	bool copilot_down;
	enum match match;
	u8 held[2];
	unsigned int n_held;
} st;

static bool enabled = true;
module_param(enabled, bool, 0644);
MODULE_PARM_DESC(enabled, "Translate the Copilot key to Right Ctrl (default: true)");

/* st.lock held. Deliver held bytes unchanged, in order. */
static void flush_held(void)
{
	unsigned int i, n = st.n_held;

	st.n_held = 0;
	st.match = MATCH_NONE;
	timer_delete(&st.timer);

	for (i = 0; i < n; i++)
		serio_interrupt(st.serio, st.held[i], 0);
}

static void hold(u8 data, enum match match)
{
	st.held[st.n_held++] = data;
	st.match = match;
	mod_timer(&st.timer, jiffies + msecs_to_jiffies(HOLD_MS));
}

/* st.lock held; returns true if data is consumed */
static bool feed(u8 data)
{
	bool after_e0 = st.last_was_e0;

	st.last_was_e0 = data == SC_E0;

	switch (st.match) {
	case MATCH_PRESS:
		if (st.n_held < ARRAY_SIZE(press_seq) && data == press_seq[st.n_held]) {
			hold(data, MATCH_PRESS);
			return true;
		}
		if (st.n_held == ARRAY_SIZE(press_seq) && data == SC_F23) {
			/* atkbd already has the E0: complete it as Right Ctrl */
			st.n_held = 0;
			st.match = MATCH_NONE;
			timer_delete(&st.timer);
			st.copilot_down = true;
			serio_interrupt(st.serio, SC_CTRL, 0);
			return true;
		}
		flush_held();
		break;

	case MATCH_RELEASE:
		if (st.n_held < ARRAY_SIZE(release_seq) && data == release_seq[st.n_held]) {
			hold(data, MATCH_RELEASE);
			return true;
		}
		if (st.n_held == ARRAY_SIZE(release_seq) && data == SC_E0) {
			/*
			 * Let the E0 through; the Ctrl break byte will follow it.
			 * The held fake-modifier breaks are dropped for good.
			 */
			st.n_held = 0;
			st.match = MATCH_RELEASE_E0;
			timer_delete(&st.timer);
			return false;
		}
		flush_held();
		break;

	case MATCH_RELEASE_E0:
		st.match = MATCH_NONE;
		if (data == (SC_META | SC_BREAK)) {
			st.copilot_down = false;
			serio_interrupt(st.serio, SC_CTRL | SC_BREAK, 0);
			return true;
		}
		break;

	case MATCH_NONE:
		break;
	}

	/* start of a new candidate burst */
	if (after_e0 && data == press_seq[0]) {
		hold(data, MATCH_PRESS);
		return true;
	}
	if (st.copilot_down && data == release_seq[0]) {
		hold(data, MATCH_RELEASE);
		return true;
	}

	return false;
}

static bool copilot_filter(unsigned char data, unsigned char str, struct serio *serio,
			   void *context)
{
	unsigned long flags;
	bool consumed;

	if (str & I8042_STR_AUXDATA)
		return false;

	spin_lock_irqsave(&st.lock, flags);
	st.serio = serio;

	if (!READ_ONCE(enabled)) {
		if (st.n_held)
			flush_held();
		st.last_was_e0 = data == SC_E0;
		consumed = false;
	} else {
		consumed = feed(data);
	}

	spin_unlock_irqrestore(&st.lock, flags);

	return consumed;
}

/* No next byte arrived in time: the held bytes were a real key press */
static void hold_timeout(struct timer_list *t)
{
	unsigned long flags;

	spin_lock_irqsave(&st.lock, flags);
	if (st.n_held && st.serio)
		flush_held();
	st.match = MATCH_NONE;
	spin_unlock_irqrestore(&st.lock, flags);
}

static const struct dmi_system_id copilot_rctrl_dmi[] = {
	{
		.ident = "PCSpecialist Recoil 16 AMD",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "PCSpecialist"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "X6FR57TY"),
		},
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, copilot_rctrl_dmi);

static int __init copilot_rctrl_init(void)
{
	int err;

	if (!dmi_check_system(copilot_rctrl_dmi))
		return -ENODEV;

	spin_lock_init(&st.lock);
	timer_setup(&st.timer, hold_timeout, 0);

	err = i8042_install_filter(copilot_filter, NULL);
	if (err) {
		pr_err("cannot install i8042 filter (%d); another driver may own it\n", err);
		return err;
	}

	pr_info("Copilot key mapped to Right Ctrl\n");
	return 0;
}

static void __exit copilot_rctrl_exit(void)
{
	unsigned long flags;

	i8042_remove_filter(copilot_filter);
	timer_shutdown_sync(&st.timer);

	/* deliver anything still held and don't leave a key stuck in atkbd */
	spin_lock_irqsave(&st.lock, flags);
	if (st.n_held && st.serio)
		flush_held();
	if (st.copilot_down && st.serio) {
		serio_interrupt(st.serio, SC_E0, 0);
		serio_interrupt(st.serio, SC_CTRL | SC_BREAK, 0);
	}
	spin_unlock_irqrestore(&st.lock, flags);
}

module_init(copilot_rctrl_init);
module_exit(copilot_rctrl_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("amad3v");
MODULE_DESCRIPTION("Map the Copilot key (Meta+Shift+F23) to Right Ctrl via an i8042 filter");
