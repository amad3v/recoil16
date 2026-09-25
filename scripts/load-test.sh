#!/bin/sh
# Build and load all modules for this boot only (nothing is installed).
set -e
cd "$(dirname "$0")/.."
. scripts/common.sh
require_root

# build as the invoking user so the tree does not end up owned by root
if [ -n "${SUDO_USER:-}" ]; then su "$SUDO_USER" -c make >/dev/null; else make >/dev/null; fi
modprobe -a sparse-keymap wmi led-class-multicolor
rmmod uniwill_laptop ite8291_mono ite8233_lightbar copilot_rctrl 2>/dev/null || true
insmod uniwill-laptop-pcs/uniwill-laptop.ko
insmod ite8291-mono/ite8291-mono.ko
insmod ite8233-lightbar/ite8233-lightbar.ko
insmod copilot-rctrl/copilot-rctrl.ko
restart_powerdevil
sleep 1
dmesg | grep -E -i "uniwill|ite8291|ite8233|lightbar|copilot" | tail -15
