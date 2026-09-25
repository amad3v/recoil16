#!/bin/sh
# Install the recoil16 modules via DKMS (rebuilt on kernel updates), recoil16ctl,
# and a battery charge limit. Manual alternative to the Arch package.
# Needs cargo (Rust) to build recoil16ctl.
# Usage: sudo scripts/install.sh [percent]   (default 90; 100 = no limit)
set -e
cd "$(dirname "$0")/.."
. scripts/common.sh
require_root
LIMIT="${1:-90}"
command -v cargo >/dev/null || { echo "cargo not found: install Rust (e.g. pacman -S rust)" >&2; exit 1; }

# build recoil16ctl as the invoking user so target/ is not owned by root;
# CARGO_TARGET_DIR overrides any target-dir from a cargo config
build="CARGO_TARGET_DIR=recoil16ctl/target cargo build --release --locked --manifest-path recoil16ctl/Cargo.toml"
if [ -n "${SUDO_USER:-}" ]; then su "$SUDO_USER" -c "$build"; else sh -c "$build"; fi

dkms_remove_all
src="/usr/src/$DKMS_NAME-$DKMS_VERSION"
mkdir -p "$src"
cp -r Kbuild dkms.conf ite8291-mono ite8233-lightbar copilot-rctrl uniwill-laptop-pcs "$src/"
make -C "$src" clean >/dev/null 2>&1 || true
dkms install "$DKMS_NAME/$DKMS_VERSION"

install -m 755 recoil16ctl/target/release/recoil16ctl /usr/local/bin/recoil16ctl
install -Dm644 data/kglobalaccel/recoil16.desktop -t /usr/local/share/kglobalaccel/
install_ctl_extras /usr/local/bin/recoil16ctl /usr/local/share
remove_old_commands

# Swap in the installed modules now
# shellcheck disable=SC2086 # module list
modprobe -r $MODULES 2>/dev/null || true
for m in $MODULES; do modprobe "$m"; done
restart_powerdevil
sleep 1
/usr/local/bin/recoil16ctl battery limit "$LIMIT"

echo
for m in $MODULES; do echo "$m: $(modinfo -F filename "$m")"; done
