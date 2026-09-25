#!/bin/sh
# Remove what scripts/install.sh installed. The stock uniwill-laptop module
# (which ignores this laptop) is restored by DKMS; reboot afterwards.
set -e
cd "$(dirname "$0")/.."
. scripts/common.sh
require_root

# the EC keeps its limit after the driver is gone, so lift it while we still can
T=/sys/class/power_supply/BAT0/charge_control_end_threshold
[ -w "$T" ] && echo 100 > "$T" && echo "battery charge limit reset to 100%"

# shellcheck disable=SC2086 # module list
modprobe -r $MODULES 2>/dev/null || true
dkms_remove_all
rm -f /usr/local/bin/recoil16ctl /usr/local/share/kglobalaccel/recoil16.desktop
remove_ctl_extras /usr/local/share
remove_old_commands
rm -f /etc/udev/rules.d/90-recoil16-charge-limit.rules \
      /etc/udev/rules.d/90-recoil16-charge-profile.rules \
      /etc/udev/rules.d/90-uniwill-charge-profile.rules
udevadm control --reload
echo "Uninstalled. Reboot to return to the stock drivers."
