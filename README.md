# recoil16

Linux kernel drivers for the **PCSpecialist Recoil 16 AMD**, a rebadged
**TUXEDO Stellaris 16 Gen7 AMD** (Uniwill `X6FR5` platform).

| Feature                                                                    | Module                                         | Interface                                                   |
| -------------------------------------------------------------------------- | ---------------------------------------------- | ----------------------------------------------------------- |
| Keyboard backlight (monochrome white, colour-corrected)                    | `ite8291-mono`                                 | `/sys/class/leds/ite8291:white:kbd_backlight`               |
| RGB lightbar                                                               | `ite8233-lightbar`                             | `/sys/class/leds/rgb:lightbar`                              |
| Fn+F6 / Fn+F7 backlight keys with desktop OSD                              | `uniwill-laptop`                               | `KEY_KBDILLUMDOWN` / `KEY_KBDILLUMUP`                       |
| Sc key (next to F12): rotate the screen 180°                               | `uniwill-laptop` + `recoil16ctl screen rotate` | `KEY_ROTATE_DISPLAY`                                        |
| Copilot key back to Right Ctrl                                             | `copilot-rctrl`                                | built-in keyboard (no virtual device)                       |
| Power modes (office / balance / turbo) with mode button and desktop slider | `uniwill-laptop`                               | `/sys/firmware/acpi/platform_profile`                       |
| Battery charge modes (full / ~93% / ~90%), true battery health            | `uniwill-laptop`                               | `/sys/class/power_supply/BAT0/charge_types`                 |
| Fn lock, Super key lock, fan/temperature sensors, NVIDIA cTGP              | `uniwill-laptop`                               | `/sys/bus/platform/devices/INOU0000:00/`                    |

All of it can be controlled with **`recoil16ctl`** (see [below](#recoil16ctl)).

## Why this is needed

Mainline Linux already ships `uniwill-laptop` with support for the Stellaris 16
Gen7, but it only binds when DMI reports `sys_vendor=TUXEDO` and
`board_name=X6FR5xxY`. The Recoil 16 reports `PCSpecialist` / `X6FR57TY`, so
nothing loads. TUXEDO's own `tuxedo-drivers` matches on the same strings.

The keyboard uses an ITE 8291 (rev 0.03) per-key RGB controller
(`048d:600b`). Without a driver it's bound to `hid-generic` and nothing on the
desktop can control it.

## How it works

- **`uniwill-laptop-pcs/`**: the mainline
  `drivers/platform/x86/uniwill` driver (v7.2) with four changes, all in
  [`patches/`](uniwill-laptop-pcs/patches) as an upstream-style series:
  1. a DMI entry for the Recoil 16 (the Stellaris 16 Gen7 AMD feature set,
     with its charge modes);
  2. `platform_profile` support for the firmware power modes, including the
     mode button (**experimental**: not upstream, see [Power modes](#power-modes));
  3. the Sc key (WMI event `0xd0`) mapped to `KEY_ROTATE_DISPLAY`;
  4. the battery's learned capacity and cycle count from the EC
     (`state_of_health`, `battery_cycle_count`, `battery_full_capacity`).

  As with any DKMS module that shares a name with an in-tree one, DKMS moves
  the stock `uniwill-laptop.ko` to `/var/lib/dkms/recoil16/original_module/`
  while installed and puts it back on uninstall. Expect `pacman -Qkk linux`
  to report it missing.
- **`ite8291-mono/`**: a small HID driver based on
  [hid-ite8291r3](https://github.com/pobrn/hid-ite8291r3):
  - It switches the controller to static mode and paints every key with a
    single white, using the colour correction from
    [tuxedo-drivers](https://gitlab.com/tuxedocomputers/development/packages/tuxedo-drivers)
    (R 170, G 255, B 125) so the keys look white instead of pink.
  - It registers one LED named `*::kbd_backlight` with a fixed name, so
    UPower, KDE, GNOME and `systemd-backlight` all find it and restore it.
  - It re-applies the colour after suspend and after a USB reset.

- **`copilot-rctrl/`**: an i8042 filter that turns the Copilot key into
  Right Ctrl. See [Copilot key](#copilot-key).
- **`ite8233-lightbar/`**: a HID driver for the ITE 8233 lightbar controller
  (`048d:7001`), using the static-colour command from tuxedo-drivers'
  `ite_8291_lb`. It registers a multicolor LED named `rgb:lightbar` (the name
  tuxedo-drivers uses).

The Fn keys produce Uniwill WMI events (`0xb1` / `0xb2`). `uniwill-laptop`
translates them into standard backlight keys. The desktop (through UPower)
then changes the LED brightness and shows its own on-screen indicator. No
desktop-specific code is involved.

## Requirements

- Kernel 7.2 or newer for everything. `uniwill-laptop` tracks the 7.2 stable
  branch and needs 7.2 APIs. On older kernels (e.g. 6.18 LTS), DKMS builds only
  the keyboard backlight, lightbar and Copilot modules. The Fn keys, power
  profiles, charge modes and Sc key need `uniwill-laptop`.
- Kernel headers and `dkms` (Arch: `pacman -S linux-headers dkms`)

## Kernel updates

DKMS rebuilds the modules for every new kernel. Nothing in the modules or in
`recoil16ctl` is tied to a specific kernel release; the only version check is
the 7.2 minimum for `uniwill-laptop`:

| New kernel                             | Result                                                                                                                       |
| -------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| 7.2.x, 7.3, 8.x, …                     | All four modules rebuilt; `recoil16ctl check` expects them all                                                               |
| Older than 7.2 (e.g. 6.19.x, 6.18 LTS) | Only the keyboard backlight, lightbar and Copilot modules; `check` reports the missing `uniwill-laptop` features as warnings |

`uniwill-laptop-pcs/` is a copy of the 7.2 driver. A later kernel that
changes an internal API can break its DKMS build. `recoil16ctl check` then
reports `dkms: … not installed for <kernel>`, and the stock kernel keeps
working without the Recoil 16 features until the copy is updated from that
kernel's `drivers/platform/x86/uniwill/`. The lasting fix is upstreaming
[`patches/`](uniwill-laptop-pcs/patches), after which the stock module
supports the laptop.

## Install

All four modules are packaged as a single DKMS package, `recoil16`, which is
rebuilt automatically on kernel updates. See [docs/INSTALL.md](docs/INSTALL.md)
for the full runbook. The short version:

```sh
# Arch Linux (AUR): latest release, or recoil16-dkms-git for the latest commit
paru -S recoil16-dkms

# or, on any distribution with DKMS
cd recoil16 && sudo scripts/install.sh long-life   # charge mode: standard | long-life | trickle
```

Then reboot and run `recoil16ctl check`.

To try the modules for the current boot only, without installing anything:

```sh
sudo scripts/load-test.sh
```

## Battery charge modes

The EC has three charge modes. Instead of stopping at a percentage, each
one lowers the voltage the battery is charged to, which is what actually
slows battery wear. They're exposed as the standard `charge_types`
attribute:

| Mode        | `charge_types` | Charges to               | Share of a full charge |
| ----------- | -------------- | ------------------------ | ---------------------- |
| `standard`  | `Standard`     | 4.35 V per cell (17.4 V) | 100%                   |
| `long-life` | `Long_Life`    | 4.30 V per cell (17.2 V) | about 93%              |
| `trickle`   | `Trickle`      | 4.20 V per cell (16.8 V) | about 90%              |

Measured on the Recoil 16's 99 Wh battery (not the "~90% / ~80%" often quoted for
these modes). The lower voltage is what slows battery wear: `trickle` keeps the cells
0.15 V below full charge for about 10% less capacity.

```sh
cat /sys/class/power_supply/BAT0/charge_types
sudo recoil16ctl battery mode long-life         # now and at every boot
sudo recoil16ctl battery mode trickle --temp    # this boot only
```

The EC still reports **100% / Full** when a mode stops charging early: the
battery is "full" at the lower voltage. `current_now` drops to 0 when
charging stops. The EC forgets the mode when the driver releases control at
shutdown, so `recoil16ctl battery mode` also writes a udev rule that
re-applies it whenever the driver loads.

`recoil16ctl battery status` shows the battery's real health and cycle count. The
firmware reports the design capacity as "full" and 0 cycles to the OS until the
battery has done 50 cycles, so `charge_full` and `cycle_count` in sysfs (and
tools that read them) always show 100% and 0. The driver reads the learned
values from the embedded controller instead (`state_of_health`,
`battery_cycle_count`, `battery_full_capacity`).

### Why there's no percentage limit any more

1.0.0 used the EC's percentage limit (register `0x07B9`, `CGLM`). The driver's
maintainer warned that Uniwill treats it as a preview feature on models other
than Intel NUCs, and that it may permanently damage the battery. There is at
least one such report
([r/XMG_gg](https://www.reddit.com/r/XMG_gg/comments/ld9yyf/battery_limit_hidden_function_discovered_on/)).
1.1.0 removes it. The EC resets the register by itself at power-off, so there
is nothing to undo.

**Upgrading from 1.0.0:** the package removes the old limit rule. Choose a mode
with `sudo recoil16ctl battery mode long-life` (or `trickle`), and run
`recoil16ctl check`.

## Power modes

The firmware has three power modes, shown by the colour of the button next
to the power button. They're exposed as a standard platform profile, so
power-profiles-daemon, KDE's battery popup and GNOME's power menu control
them directly:

| Profile                   | Firmware mode | Button light | NVIDIA GPU power budget |
| ------------------------- | ------------- | ------------ | ----------------------- |
| `low-power` (power saver) | office        | green        | 55 W                    |
| `balanced`                | balance       | blue         | 55 W                    |
| `performance`             | turbo         | purple       | 105 W                   |

The mode button cycles `low-power → balanced → performance`, and the desktop
slider follows it. A mode set from the desktop takes effect in the embedded
controller straight away: it changes the light colour and the GPU power
limits itself.

This part is **experimental and not upstream**: the driver's maintainer wants
the fan control and platform profile support to get more work before it's
enabled in mainline, so it only exists in this package for now.

```sh
cat /sys/firmware/acpi/platform_profile
powerprofilesctl set performance
```

power-profiles-daemon restores the last profile chosen on the desktop at
login, so that profile takes priority over the one set in the BIOS.

How it works: the EC stores the mode in register `0x0751` (the ACPI tables
call bit 4 `TBME` and bit 7 `UFME`). The driver writes the same values the EC
uses itself. Once the OS has set a mode, the EC stops cycling on its own and
only reports the key press (WMI event `0xb0`). The driver then cycles the
profile itself.

## Keyboard backlight

```sh
L=/sys/class/leds/ite8291:white:kbd_backlight
cat $L/max_brightness             # 50
echo 30 | sudo tee $L/brightness
```

Fn+F6 / Fn+F7 only report key presses: the embedded controller doesn't change
this keyboard's light itself. **By default the desktop handles them** (KDE's
PowerDevil, GNOME's settings daemon, …), so they work in a desktop session and
on its lock screen, but not at the login screen or on a text console.

**Optional: let the driver handle them** with `key_step=10` (see below). Then
the keys work everywhere, including the login screen and text consoles: the
driver takes the two keys before any desktop sees them, steps the brightness
itself and reports it as a hardware change (`brightness_hw_changed`), which
UPower passes on to the desktop (tested: KDE Plasma shows its indicator and
moves the slider).

**Known issue with UPower 1.91.4** (the version in Arch as of 2026-09-27): it
stops watching `brightness_hw_changed` at startup, so with `key_step` set the
desktop doesn't see the key changes (no indicator, and the slider stays where
it was, which can also make the desktop restore a wrong brightness after
idling). This is fixed upstream (UPower commit `ce25d69f`, not yet released),
and that's why the option is off by default.

Writing `brightness` directly works too, but the desktop isn't told about it.

Module parameters (e.g. in `/etc/modprobe.d/ite8291-mono.conf`):

| Parameter                                | Default    | Description                                                              |
| ---------------------------------------- | ---------- | ------------------------------------------------------------------------ |
| `red_scale`, `green_scale`, `blue_scale` | auto (DMI) | White balance, 0-255 per channel                                         |
| `default_brightness`                     | 25         | Brightness at probe, before `systemd-backlight` restores the saved value |
| `key_step`                               | 0          | Brightness step for Fn+F6/F7, 0-50; 0 leaves the keys to the desktop     |

```
options ite8291-mono red_scale=160 blue_scale=120
options ite8291-mono key_step=10      # Fn+F6/F7 handled by the driver (see above)
```

## Lightbar

```sh
recoil16ctl lightbar                      # show state and the boot setting
sudo recoil16ctl lightbar blue            # colour; turns it on if it was off
sudo recoil16ctl lightbar ff8800 40       # colour (RRGGBB, #RRGGBB or "R G B") + brightness 0-100
sudo recoil16ctl lightbar 70              # brightness only
sudo recoil16ctl lightbar off             # off; "on" returns to the last brightness
recoil16ctl lightbar colours              # white red green blue cyan magenta purple yellow orange pink
```

Every change is saved to `/etc/modprobe.d/ite8233-lightbar.conf` and applied
at boot. Add `--temp` to change it for this boot only. Underneath, it writes
the standard LED files `/sys/class/leds/rgb:lightbar/{multi_intensity,brightness}`.

## recoil16ctl

A small Rust command that controls everything the drivers expose. Reading
works as a normal user; changing settings needs `sudo`, except `screen`,
which must run as your desktop user.

```sh
recoil16ctl                                # status: battery, profile, keyboard, lightbar, gpu, fans
recoil16ctl check                          # verify the installation (alias: verify)
recoil16ctl power                          # battery draw (5 s average), time left, GPU, panel
recoil16ctl power --watch --battery BAT0   # every 2 s until Ctrl+C, one battery only
recoil16ctl gpu                            # NVIDIA GPU power state, driver, which of your apps use it
recoil16ctl gpu run -- freecad             # run a program on the NVIDIA GPU
recoil16ctl gpu test                       # prove offloading works (wakes the GPU briefly)
recoil16ctl battery                        # charge, voltage, charge mode, boot rule
recoil16ctl battery status                 # true health, cycles, charge mode (from the EC)
sudo recoil16ctl battery mode long-life [--temp] # standard | long-life | trickle; saved for boot unless --temp
sudo recoil16ctl battery clear-rule        # drop the boot rule (Standard from the next boot)
sudo recoil16ctl lightbar blue 60 [--temp] # see Lightbar
sudo recoil16ctl profile performance       # low-power | balanced | performance | cycle
recoil16ctl keyboard                       # backlight level, Fn lock, Super key
sudo recoil16ctl keyboard fn-lock on       # F1-F12 without Fn
sudo recoil16ctl keyboard super-key off    # same as Fn+F2
recoil16ctl screen                         # built-in panel orientation (KDE)
recoil16ctl screen rotate                  # flip it 180°; run as your user, not sudo
recoil16ctl completions zsh                # bash | zsh | fish | elvish | powershell
```

The Arch package installs the man pages (`man recoil16` for setup, files,
removal and troubleshooting; `man recoil16ctl` for the command) and the bash,
zsh and fish completions. `profile`
changes only the handler registered by `uniwill-laptop`, and
power-profiles-daemon (the KDE/GNOME slider) follows it.

## Usage examples

**Is everything working?**

```sh
recoil16ctl check          # drivers, settings, and a "Power" group with fixes for anything off
recoil16ctl                # one line per feature
```

**How long will the battery last?**

```sh
recoil16ctl power          # waits 5 s, then: draw in W, % and time left, GPU and panel state
recoil16ctl power --watch  # a fresh 5 s average every 2 s; Ctrl+C to stop
```

**Keep the battery healthy**

```sh
sudo recoil16ctl battery mode long-life       # stop at ~93 %, also after reboots
sudo recoil16ctl battery mode trickle --temp  # ~90 %, this boot only
recoil16ctl battery status                    # learned health and cycle count
```

**Run an app on the NVIDIA GPU**

```sh
recoil16ctl gpu run -- freecad    # or blender, a game, anything drawn with OpenGL/Vulkan
recoil16ctl gpu                   # asleep? which of your apps hold it?
recoil16ctl gpu test              # checks that offloaded apps really land on it
```

Everything else runs on the AMD GPU and the NVIDIA GPU sleeps. Only apps that
draw with OpenGL or Vulkan get faster; offloading others (Inkscape, a browser)
only wakes the GPU and costs battery. In KDE, right-click an app in the menu →
**Edit Application… → Advanced → Run using dedicated graphics card** makes it
permanent. `gpu test` needs `mesa-utils` and `vulkan-tools`.

**Faster or quieter**

```sh
sudo recoil16ctl profile performance   # turbo; low-power / balanced; cycle = mode button
```

**Lights and keys**

```sh
sudo recoil16ctl lightbar purple 40    # colour and brightness, also after reboots
sudo recoil16ctl lightbar off
sudo recoil16ctl keyboard fn-lock on   # F1-F12 without holding Fn
recoil16ctl screen rotate              # flip the screen 180° (the Sc key does this too)
```

## Power on the hybrid GPU

The internal panel runs on the AMD GPU; the NVIDIA GPU should be fully off
until an app is offloaded to it. Out of the box several things keep it on.
`recoil16ctl check` lists them in its **Power** group; these are the fixes.

**Let the kernel suspend the NVIDIA GPU** (it otherwise idles at ~9 W).
`/etc/udev/rules.d/80-nvidia-pm.rules`:

```
ACTION=="bind", SUBSYSTEM=="pci", ATTR{vendor}=="0x10de", ATTR{class}=="0x030000", TEST=="power/control", ATTR{power/control}="auto"
ACTION=="bind", SUBSYSTEM=="pci", ATTR{vendor}=="0x10de", ATTR{class}=="0x040300", TEST=="power/control", ATTR{power/control}="auto"
ACTION=="unbind", SUBSYSTEM=="pci", ATTR{vendor}=="0x10de", ATTR{class}=="0x030000", TEST=="power/control", ATTR{power/control}="on"
```

and `/etc/modprobe.d/nvidia-pm.conf`:

```
options nvidia "NVreg_DynamicPowerManagement=0x02"
```

**Let the network, card reader and NVMe controllers suspend** (the PCI classes
`check` looks at: Ethernet, Wi-Fi, SD host, NVMe).
`/etc/udev/rules.d/81-pci-runtime-pm.rules`:

```
ACTION=="add", SUBSYSTEM=="pci", ATTR{class}=="0x0200*", TEST=="power/control", ATTR{power/control}="auto"
ACTION=="add", SUBSYSTEM=="pci", ATTR{class}=="0x0280*", TEST=="power/control", ATTR{power/control}="auto"
ACTION=="add", SUBSYSTEM=="pci", ATTR{class}=="0x0805*", TEST=="power/control", ATTR{power/control}="auto"
ACTION=="add", SUBSYSTEM=="pci", ATTR{class}=="0x0108*", TEST=="power/control", ATTR{power/control}="auto"
```

**Install the AMD Vulkan driver**: `sudo pacman -S vulkan-radeon`. Without it
every Vulkan app runs on the NVIDIA GPU.

**Don't early-load the NVIDIA modules**: keep `nvidia*` out of `MODULES=` in
`/etc/mkinitcpio.conf`. The AMD GPU drives the panel at boot, and the NVIDIA
firmware adds about 110 MB to every boot image.

Reboot, then `recoil16ctl gpu` should show `suspended` when no app uses it.
Don't check with `nvidia-smi`: it wakes the GPU to answer.

## Keyboard keys

| Key           | Behaviour                                                | Handled by                                                    |
| ------------- | -------------------------------------------------------- | ------------------------------------------------------------- |
| Fn+F2         | Locks/unlocks the Super (Windows) key                    | EC; state in `INOU0000:00/super_key_enable`                   |
| Fn+F6 / Fn+F7 | Keyboard backlight down/up, with the desktop's indicator | `uniwill-laptop` → UPower                                     |
| Fn+Sc         | Camera on/off (the USB camera disappears from the bus)   | EC only, nothing reaches the OS                               |
| Sc            | Rotates the screen 180° (for the lay-flat hinge)         | `uniwill-laptop` (WMI `0xd0` → `KEY_ROTATE_DISPLAY`) + script |
| Mode button   | Cycles the power modes                                   | `uniwill-laptop`                                              |
| Copilot       | Right Ctrl                                               | `copilot-rctrl`                                               |

### Screen rotation (Sc)

`recoil16ctl screen rotate` toggles the built-in panel between normal and
inverted using `kscreen-doctor`, so it is KDE Plasma only. Run it as your
desktop user, not with sudo.

The package installs a KDE default global shortcut for it
(`/usr/share/kglobalaccel/recoil16.desktop`, Sc = `Rotate Windows`), so Sc
works from the next login with no setup. It is listed as **Recoil 16 →
Rotate Screen 180°** in System Settings → Keyboard → Shortcuts, where it can
be changed or removed.

### Copilot key

The keyboard controller reports the Copilot key as a burst of three key
presses, Left Meta + Left Shift + F23 (`E0 5B 2A 6E`, about 3 ms apart), and
repeats the whole burst while the key is held. A remap in udev hwdb can't fix
that, because the fake Meta and Shift would still be held.

`copilot-rctrl` installs an i8042 filter, a hook on the raw bytes from the
built-in keyboard. It hides the fake modifiers and gives the keyboard driver
the real Right Ctrl codes (`E0 1D` / `E0 9D`). Key events still come from the
built-in keyboard, and no virtual input device is created, so key remapping
elsewhere (the Fn keys, UPower, the desktop) is unaffected.

- The Super key starts with the same `E0 5B` bytes, so a lone Super press is
  held back for at most 15 ms, until the next byte shows it's not a Copilot
  burst.
- The module loads only on the Recoil 16 (DMI match).
- Turn it off at runtime with
  `echo 0 | sudo tee /sys/module/copilot_rctrl/parameters/enabled`.
- The kernel allows only one i8042 filter at a time, so the module refuses to
  load if another driver already has one.

## Other controls

```sh
D=/sys/bus/platform/devices/INOU0000:00
cat $D/fn_lock $D/super_key_enable
sensors | grep -A5 uniwill
```

## Uninstall

`sudo pacman -R recoil16-dkms` (or `recoil16-dkms-git`) for the Arch package, or `sudo scripts/uninstall.sh`
(script install), then reboot. Both remove the charge-mode boot rule; the
EC is back to Standard after the reboot.

## Tested on

| Model                      | DMI board  | BIOS        | Kernel        |
| -------------------------- | ---------- | ----------- | ------------- |
| PCSpecialist Recoil 16 AMD | `X6FR57TY` | N.1.39PCS10 | 7.2.6-arch2-1 |

Other ITE 8291 rev 0.03 keyboards (`048d:6004/6006/600b/ce00`) should work
with `ite8291-mono` too. Unlisted boards get tuxedo-drivers' default white
balance.

## Credits

- `uniwill-laptop`: Armin Wolf and the mainline Linux contributors
- ITE 8291 protocol: Barnabás Pőcze,
  [hid-ite8291r3](https://github.com/pobrn/hid-ite8291r3)
- Colour correction and EC knowledge: TUXEDO Computers,
  [tuxedo-drivers](https://gitlab.com/tuxedocomputers/development/packages/tuxedo-drivers)

## Versioning

`recoil16ctl` and the DKMS modules share one version, in
`recoil16ctl/Cargo.toml` and `dkms.conf`; a test fails if they differ.
Releases are tagged `vX.Y.Z`, and the Arch package version is derived from
the tag (`1.0.0.r<commits since the tag>.g<hash>`). Changes per release are in
[CHANGELOG.md](CHANGELOG.md).

## License

GPL-2.0. `uniwill-laptop-pcs/` sources are GPL-2.0-or-later as in mainline.
See [LICENSE](LICENSE).
