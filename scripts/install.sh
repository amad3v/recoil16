#!/bin/sh
# Install the recoil16 modules via DKMS (rebuilt on kernel updates) and a
# battery charge mode. Manual alternative to the Arch package.
# Usage: sudo scripts/install.sh [standard|long-life|trickle]   (default long-life, ~93%)
set -e
cd "$(dirname "$0")/.."
. scripts/common.sh
require_root
MODE="${1:-long-life}"

dkms_remove_all
src="/usr/src/$DKMS_NAME-$DKMS_VERSION"
mkdir -p "$src"
cp -r Kbuild dkms.conf ite8291-mono ite8233-lightbar copilot-rctrl uniwill-laptop-pcs "$src/"
make -C "$src" clean >/dev/null 2>&1 || true
dkms install "$DKMS_NAME/$DKMS_VERSION"

install -Dm644 man/recoil16.7 -t /usr/local/share/man/man7/
remove_old_commands

# Swap in the installed modules now
# shellcheck disable=SC2086 # module list
modprobe -r $MODULES 2>/dev/null || true
for m in $MODULES; do modprobe "$m"; done
restart_powerdevil
sleep 1

if command -v recoil16ctl >/dev/null; then
	recoil16ctl battery mode "$MODE"
else
	echo "==> recoil16ctl not found: install it to control the drivers"
	echo "==>     https://github.com/amad3v/recoil16ctl"
fi

echo
for m in $MODULES; do echo "$m: $(modinfo -F filename "$m")"; done
