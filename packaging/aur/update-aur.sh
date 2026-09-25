#!/bin/sh
# Copy an AUR package's files into a clone of its AUR repository and
# regenerate .SRCINFO. Commit and push in the clone afterwards.
#
# Usage: packaging/aur/update-aur.sh <recoil16-dkms|recoil16-dkms-git> <aur clone>
#   first time: git clone ssh://aur@aur.archlinux.org/<package>.git <aur clone>
set -e
pkg="${1:?usage: $0 <recoil16-dkms|recoil16-dkms-git> <aur clone>}"
dest="${2:?usage: $0 <recoil16-dkms|recoil16-dkms-git> <aur clone>}"
here=$(cd "$(dirname "$0")" && pwd)

[ -f "$here/$pkg/PKGBUILD" ] || { echo "unknown package: $pkg" >&2; exit 1; }
[ -d "$dest/.git" ] || {
	echo "$dest is not a git clone; run: git clone ssh://aur@aur.archlinux.org/$pkg.git $dest" >&2
	exit 1
}

# -L: recoil16.install is a symlink in this repository, the AUR needs the file
cp -L "$here/$pkg/PKGBUILD" "$here/$pkg/recoil16.install" "$dest/"
cd "$dest"

if [ "$pkg" = recoil16-dkms-git ]; then
	# fetch the source and let pkgver() write the current version into PKGBUILD
	makepkg --nobuild --nodeps --noprepare --cleanbuild >/dev/null
else
	# the release archive must match the pinned checksum
	makepkg --verifysource --nodeps >/dev/null
fi
makepkg --printsrcinfo >.SRCINFO

# the AUR repository holds only these files
printf '*\n!PKGBUILD\n!recoil16.install\n!.SRCINFO\n!.gitignore\n' >.gitignore

echo "== $pkg $(sed -n 's/^\tpkgver = //p' .SRCINFO)-$(sed -n 's/^\tpkgrel = //p' .SRCINFO)"
git status --short
echo "Review, then: git add PKGBUILD recoil16.install .SRCINFO .gitignore && git commit -m '...' && git push"
