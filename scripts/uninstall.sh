#!/bin/sh
# Remove what scripts/install.sh installed. The stock uniwill-laptop module
# (which ignores this laptop) is restored by DKMS; reboot afterwards.
# This leaves recoil16ctl's files and udev rules alone: remove it separately
# (see https://github.com/amad3v/recoil16ctl) if it's installed.
set -e
cd "$(dirname "$0")/.."
. scripts/common.sh
require_root

# shellcheck disable=SC2086 # module list
modprobe -r $MODULES 2>/dev/null || true
dkms_remove_all
rm -f /usr/local/share/man/man7/recoil16.7
remove_old_commands
echo "Uninstalled. Reboot to return to the stock drivers."
