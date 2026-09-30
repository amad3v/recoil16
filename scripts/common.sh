# Shared helpers, sourced from the repository root.
MODULES="uniwill-laptop ite8291-mono ite8233-lightbar copilot-rctrl"

require_root() {
	[ "$(id -u)" -eq 0 ] || { echo "run as root (sudo)" >&2; exit 1; }
}

# dkms_field <KEY> -> value from the top-level dkms.conf
dkms_field() {
	sed -n "s/^$1=\"\{0,1\}\([^\"]*\)\"\{0,1\}$/\1/p" dkms.conf
}

DKMS_NAME=$(dkms_field PACKAGE_NAME)
DKMS_VERSION=$(dkms_field PACKAGE_VERSION)

# Remove the recoil16 DKMS package and the per-module packages of older versions.
dkms_remove_all() {
	for pkg in "$DKMS_NAME" ite8291-mono ite8233-lightbar copilot-rctrl uniwill-laptop-pcs; do
		for ver in $(dkms status "$pkg" 2>/dev/null | sed -n "s|^$pkg/\([^,:]*\).*|\1|p" | sort -u); do
			dkms remove "$pkg/$ver" --all >/dev/null 2>&1 || true
			rm -rf "/usr/src/$pkg-$ver"
		done
	done
}

# Helper scripts replaced by recoil16ctl
remove_old_commands() {
	rm -f /usr/local/bin/recoil16-rotate-screen /usr/local/bin/recoil16-set-charge-limit \
	      /usr/local/bin/recoil16-lightbar
}

# Restart KDE's power daemon for the desktop user: PowerDevil 6.7 crashes on a
# keyboard brightness key if the backlight LED appeared after it started.
restart_powerdevil() {
	user="${SUDO_USER:-}"
	[ -n "$user" ] || return 0
	systemctl --user -M "$user@" try-restart plasma-powerdevil.service >/dev/null 2>&1 || true
}
