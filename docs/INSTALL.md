# Installation runbook

Three ways to install, all equivalent. Each one builds the same DKMS package,
`recoil16`, which contains four modules:

| Module | Purpose |
|---|---|
| `uniwill-laptop` | Fn keys, power profiles, charge modes, battery health, Sc key, sensors (replaces the stock module) |
| `ite8291-mono` | Keyboard backlight |
| `ite8233-lightbar` | Lightbar |
| `copilot-rctrl` | Copilot key → Right Ctrl |

After installing, run `recoil16ctl check` (see [Verify](#verify)).

## Prerequisites

```sh
sudo pacman -S --needed base-devel git dkms linux-headers
```

Use the headers package that matches your kernel (`linux-lts-headers`,
`linux-zen-headers`, …). `uniwill-laptop` needs kernel 7.2 or newer; on older
kernels (e.g. a 6.18 LTS fallback) DKMS builds only the keyboard backlight,
lightbar and Copilot modules.

If you already loaded the modules by hand (for example with
`scripts/load-test.sh`), reboot first so the installed versions load cleanly.

---

## A. Arch Linux: AUR package (recommended)

Two package pairs; install one drivers package and one `recoil16ctl` package:

| Package | Builds |
|---|---|
| `recoil16-dkms` | The latest `vX.Y.Z` tag, checked against a pinned checksum |
| `recoil16-dkms-git` | The latest commit on `main` |
| `recoil16ctl` | The latest `vX.Y.Z` tag of [amad3v/recoil16ctl](https://github.com/amad3v/recoil16ctl) |
| `recoil16ctl-git` | The latest commit on `main` of recoil16ctl |

With an AUR helper:

```sh
yay -S recoil16-dkms recoil16ctl          # or: paru -S recoil16-dkms recoil16ctl
```

Without one:

```sh
git clone https://aur.archlinux.org/recoil16-dkms.git
cd recoil16-dkms
makepkg -si
```

then build and install `recoil16ctl` the same way, from its own AUR package
(see [amad3v/recoil16ctl](https://github.com/amad3v/recoil16ctl)).

The same `PKGBUILD`s are in this repository under
[`packaging/aur/`](../packaging/aur). To build a local checkout, including
committed but unpushed changes:

```sh
cd recoil16/packaging/aur/recoil16-dkms-git
RECOIL16_GIT="file://$(realpath ../../..)" makepkg -si
```

**Upgrading from recoil16-dkms 1.3.0 or older:** recoil16ctl 1.4.0 and
recoil16-dkms 1.3.0 can't both own `/usr/bin/recoil16ctl` and friends, and
pacman would install them in separate transactions; upgrading the drivers
first removes the files, then `yay -S recoil16ctl` reinstalls them from its
own package. Run `yay -Syu` first, then `yay -S recoil16ctl`.

**What pacman does:**
1. Installs the module sources to `/usr/src/recoil16-<version>/`. The DKMS
   pacman hook builds them for every installed kernel, and again after each
   kernel update.
2. Installs the `recoil16(7)` man page.

**Then:**

```sh
sudo reboot
recoil16ctl                           # status of everything
sudo recoil16ctl battery mode long-life   # optional: charge to ~93% (trickle: ~90%)
```

**Update:** `git pull`, then `makepkg -si` again, then reboot.

**Remove:** `sudo pacman -R recoil16-dkms` (or `recoil16-dkms-git`), then reboot; remove
`recoil16ctl` the same way if it's installed. Removing `recoil16ctl` deletes its
charge-mode udev rule; the EC is back to Standard after the reboot.

---

## B. Install script (any distribution with DKMS)

```sh
git clone https://github.com/amad3v/recoil16
cd recoil16
sudo scripts/install.sh long-life     # charge mode: standard | long-life | trickle
```

The script:
1. Removes any earlier recoil16 DKMS install.
2. Copies the sources to `/usr/src/recoil16-<version>/` and runs `dkms install`.
3. Installs the `recoil16(7)` man page.
4. Loads the installed modules and restarts KDE's PowerDevil.
5. Sets the charge mode, if `recoil16ctl` is installed (see
   [amad3v/recoil16ctl](https://github.com/amad3v/recoil16ctl) — otherwise it
   prints how to get it).

A reboot is not required, but it's the best test that everything loads by
itself.

**Remove:** `sudo scripts/uninstall.sh`, then reboot. This leaves `recoil16ctl`
and the charge-mode boot rule alone; remove `recoil16ctl` separately to drop
those too.

---

## C. Fully manual

These are the steps the script automates, for when you want to see or
control each one.

```sh
git clone https://github.com/amad3v/recoil16
cd recoil16
VER=$(sed -n 's/^PACKAGE_VERSION="\(.*\)"/\1/p' dkms.conf)

# 1. sources for DKMS
sudo mkdir -p /usr/src/recoil16-$VER
sudo cp -r Kbuild dkms.conf ite8291-mono ite8233-lightbar copilot-rctrl uniwill-laptop-pcs /usr/src/recoil16-$VER/

# 2. build and install for the running kernel (and future kernels, AUTOINSTALL=yes)
sudo dkms install recoil16/$VER
dkms status recoil16                  # recoil16/<ver>, <kernel>, x86_64: installed

# 3. check that the DKMS copy wins over the stock uniwill-laptop
modinfo -F filename uniwill-laptop    # must be .../updates/dkms/uniwill-laptop.ko*

# 4. the control command: build and install it from its own repository
#    (https://github.com/amad3v/recoil16ctl), or via its AUR package

# 5. reboot, then set the charge mode
sudo reboot
sudo recoil16ctl battery mode long-life
```

**Remove:**

```sh
sudo dkms remove recoil16/$VER --all
sudo rm -rf /usr/src/recoil16-$VER
sudo reboot
```

Remove `recoil16ctl` separately (see its own repository) to drop the control
command and the charge-mode udev rule.

---

## After any install

This section and the troubleshooting table are also installed as the
`recoil16(7)` man page (`man recoil16`).


1. **Charge mode:** `sudo recoil16ctl battery mode long-life` (about 93%) or
   `trickle` (about 90%); `standard` charges fully. The EC still reports
   100% / Full when a mode stops charging early. The command writes a udev
   rule that re-applies the mode whenever the driver loads, because the EC
   forgets it at shutdown. `recoil16ctl battery status` shows the battery's
   real health and cycle count.
2. **Sc key (KDE):** works after the next login once `recoil16ctl` is
   installed. It adds a default shortcut, listed as **Recoil 16 → Rotate
   Screen 180°** under System Settings → Keyboard → Shortcuts, where you can
   change it. If you bound Sc by hand earlier, delete that shortcut,
   otherwise the two clash.
3. **Lightbar (optional):** it starts off. Run for example
   `sudo recoil16ctl lightbar blue 60`; the setting is saved and applied at
   every boot.
4. **Verify:** see below.

## Verify

```sh
recoil16ctl check
```

It checks the kernel, the loaded modules, DKMS, the charge mode and its
boot rule (and a leftover 1.0.0 charge-limit rule), the power profile and
power-profiles-daemon, the keyboard, lightbar, Copilot filter, sensors and
the KDE Sc shortcut. It prints one line per check (`ok`, `warn` or `FAIL`)
and exits non-zero if anything failed.
Then it lists the checks that need a person: Fn keys, the mode button, Sc,
Copilot, and suspend/resume.

Run it again after upgrades and kernel updates. On kernels older than 7.2,
the `uniwill-laptop` checks show as warnings, not failures.

## Troubleshooting

| Symptom | Check |
|---|---|
| `dkms install` fails | `linux-headers` matches `uname -r`; see `/var/lib/dkms/recoil16/<ver>/build/make.log` |
| `Missing <version> kernel headers` from the DKMS hook | A leftover `/usr/lib/modules/<version>` of a removed kernel. If `pacman -Qo /usr/lib/modules/<version>` says no package owns it, delete it |
| `recoil16ctl <Tab>` doesn't complete in zsh | The completion cache predates the install: `rm ~/.zcompdump*`, then open a new shell |
| Nothing works after boot | `modinfo -F filename uniwill-laptop` shows the stock module: run `sudo depmod -a` and reboot |
| KDE's power daemon crashed after loading the modules by hand | Known PowerDevil 6.7 bug when the keyboard light appears after login: `systemctl --user restart plasma-powerdevil` |
| `copilot-rctrl` doesn't load: "cannot install i8042 filter" | Another driver owns the i8042 filter: `lsmod`, then check `dmesg` for the owner |
| Charge mode is `Standard` after boot | `sudo recoil16ctl battery mode long-life` writes the boot rule |
| Battery shows 100% / Full although a mode is set | Expected: the EC reports the lower voltage as full. `cat /sys/class/power_supply/BAT0/current_now` is 0 when charging stopped |
| `check` warns about a leftover limit rule | From 1.0.0: `sudo recoil16ctl battery clear-rule`, then set a mode |
| `recoil16ctl` says "permission denied … run with sudo" | Changing settings needs root; reading them (`recoil16ctl`, `recoil16ctl battery`) does not |
| `recoil16ctl screen rotate` says "not with sudo" | Run it as your desktop user: it talks to your KDE session |
