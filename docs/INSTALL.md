# Installation runbook

Three ways to install, all equivalent. Each one builds the same DKMS package,
`recoil16`, which contains four modules:

| Module | Purpose |
|---|---|
| `uniwill-laptop` | Fn keys, power profiles, charge limit, Sc key, sensors (replaces the stock module) |
| `ite8291-mono` | Keyboard backlight |
| `ite8233-lightbar` | Lightbar |
| `copilot-rctrl` | Copilot key → Right Ctrl |

After installing, run `recoil16ctl check` (see [Verify](#verify)).

## Prerequisites

```sh
sudo pacman -S --needed base-devel git dkms linux-headers rust
```

**Rust:** either Arch's `rust` package, or rustup. If you installed rustup
yourself (in `~/.cargo/bin`), `makepkg -s` asks which package provides
`cargo`: choose `rustup`. It uses your existing `~/.rustup` toolchains and
needs a `stable` toolchain (`rustup toolchain install stable`). You can also
skip the question with `makepkg -di` (see below).

Use the headers package that matches your kernel (`linux-lts-headers`,
`linux-zen-headers`, …). `uniwill-laptop` needs kernel 7.2 or newer; on older
kernels (e.g. a 6.18 LTS fallback) DKMS builds only the keyboard backlight,
lightbar and Copilot modules.

If you already loaded the modules by hand (for example with
`scripts/load-test.sh`), reboot first so the installed versions load cleanly.

---

## A. Arch package (recommended)

```sh
git clone https://github.com/amad3v/recoil16
cd recoil16/packaging/arch
makepkg -si
```

To build from a local checkout with unpushed commits:

```sh
cd recoil16/packaging/arch
RECOIL16_GIT="file://$(realpath ../..)" makepkg -si
```

Only committed changes are built. Commit first.

With a self-installed rustup, `makepkg -di` skips the build-dependency check
and uses the `cargo` on your `PATH`. pacman still checks the runtime
dependencies when it installs the package.

**What pacman does:**
1. Installs the module sources to `/usr/src/recoil16-<version>/`. The DKMS
   pacman hook builds them for every installed kernel, and again after each
   kernel update.
2. Builds and installs `recoil16ctl`, the control command, with bash, zsh
   and fish completions.
3. Installs the `recoil16ctl` man pages (`man recoil16ctl`) and shell
   completions.

**Then:**

```sh
sudo reboot
recoil16ctl                           # status of everything
sudo recoil16ctl battery limit 90     # optional; KDE/GNOME settings work too
```

**Update:** `git pull`, then `makepkg -si` again, then reboot.

**Remove:** `sudo pacman -R recoil16-dkms-git`, then reboot. Removing the
package resets the charge limit to 100% and deletes its udev rule.

---

## B. Install script (any distribution with DKMS)

```sh
git clone https://github.com/amad3v/recoil16
cd recoil16
sudo scripts/install.sh 90            # charge limit in percent; 100 = no limit
```

The script:
1. Removes any earlier recoil16 DKMS install.
2. Copies the sources to `/usr/src/recoil16-<version>/` and runs `dkms install`.
3. Builds `recoil16ctl` with cargo, as your user, and installs it to
   `/usr/local/bin`.
4. Loads the installed modules and restarts KDE's PowerDevil.
5. Sets the charge limit.

A reboot is not required, but it's the best test that everything loads by
itself.

**Remove:** `sudo scripts/uninstall.sh`, then reboot. This also resets the
charge limit to 100%.

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

# 4. the control command (as your user, not root)
cargo build --release --locked --manifest-path recoil16ctl/Cargo.toml
sudo install -m 755 recoil16ctl/target/release/recoil16ctl /usr/local/bin/recoil16ctl

# 5. reboot, then set the charge limit
sudo reboot
sudo recoil16ctl battery limit 90
```

**Remove:**

```sh
echo 100 | sudo tee /sys/class/power_supply/BAT0/charge_control_end_threshold
sudo dkms remove recoil16/$VER --all
sudo rm -rf /usr/src/recoil16-$VER
sudo rm -f /usr/local/bin/recoil16ctl /etc/modprobe.d/ite8233-lightbar.conf \
           /etc/udev/rules.d/90-recoil16-charge-limit.rules
sudo reboot
```

---

## After any install

This section and the troubleshooting table are also installed as the
`recoil16(7)` man page (`man recoil16`).


1. **Charge limit:** set it in **System Settings → Power Management**, or
   with `sudo recoil16ctl battery limit <percent>`. The EC remembers the
   limit across reboots. The command also writes a udev rule that re-applies
   the limit whenever the driver loads.
2. **Sc key (KDE):** works after the next login. The install adds a
   default shortcut, listed as **Recoil 16 → Rotate Screen 180°** under
   System Settings → Keyboard → Shortcuts, where you can change it. If you
   bound Sc by hand earlier, delete that shortcut, otherwise the two clash.
3. **Lightbar (optional):** it starts off. Run for example
   `sudo recoil16ctl lightbar blue 60`; the setting is saved and applied at
   every boot.
4. **Verify:** see below.

## Verify

```sh
recoil16ctl check
```

It checks the kernel, the loaded modules, DKMS, the charge limit and its
boot rule, the power profile and power-profiles-daemon, the keyboard,
lightbar, Copilot filter, sensors and the KDE Sc shortcut. It prints one line
per check (`ok`, `warn` or `FAIL`) and exits non-zero if anything failed.
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
| Charge limit shows 100 after boot | `sudo recoil16ctl battery limit 90` writes the boot rule |
| `recoil16ctl` says "permission denied … run with sudo" | Changing settings needs root; reading them (`recoil16ctl`, `recoil16ctl battery`) does not |
| `recoil16ctl screen rotate` says "not with sudo" | Run it as your desktop user: it talks to your KDE session |
