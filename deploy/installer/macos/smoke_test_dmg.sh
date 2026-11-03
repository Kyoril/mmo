#!/bin/bash
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Checks a built AlestiaOnline.dmg without a GUI session:
#   - the image verifies and mounts,
#   - it holds "Alestia Online/Launcher.app", an Applications link and the Finder layout,
#   - the launcher reports the expected version, matches the given build and is validly signed,
#   - the launcher starts from the read-only image and from a copied install folder and
#     renders every page from its bundled art (--render-preview).
#
#   deploy/installer/macos/smoke_test_dmg.sh --dmg dist/AlestiaOnline.dmg --version 1.0.1 [--launcher bin/Launcher.app]
set -euo pipefail

dmg=""
version=""
launcher=""
while [ $# -gt 0 ]; do
	case "$1" in
		--dmg) dmg="$2"; shift 2 ;;
		--version) version="$2"; shift 2 ;;
		--launcher) launcher="$2"; shift 2 ;;
		*) echo "unknown argument $1" >&2; exit 2 ;;
	esac
done
[ -f "$dmg" ] && [ -n "$version" ] || { echo "usage: $0 --dmg <dmg> --version X.Y.Z [--launcher <Launcher.app>]" >&2; exit 2; }

work="$(mktemp -d)"
mount="$work/mount"
cleanup()
{
	hdiutil detach "$mount" -quiet -force 2>/dev/null || true
	rm -rf "$work"
}
trap cleanup EXIT

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

echo "verify" >&2
hdiutil verify "$dmg" -quiet || fail "hdiutil verify"

echo "mount" >&2
mkdir -p "$mount"
hdiutil attach "$dmg" -nobrowse -readonly -noautoopen -mountpoint "$mount" -quiet || fail "attach"

app="$mount/Alestia Online/Launcher.app"
[ -x "$app/Contents/MacOS/Launcher" ] || fail "Alestia Online/Launcher.app is missing"
[ "$(readlink "$mount/Applications")" = "/Applications" ] || fail "the Applications link is missing"
[ -f "$mount/.DS_Store" ] || fail "the Finder layout (.DS_Store) is missing"
ls "$mount"/.background* > /dev/null 2>&1 || fail "the window background is missing"

built="$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$app/Contents/Info.plist")"
[ "$built" = "$version" ] || fail "the launcher reports $built, expected $version"

# A broken bundle signature makes a downloaded launcher "damaged" and unopenable.
codesign --verify --strict "$app" || fail "the launcher's code signature is broken"

if [ -n "$launcher" ]; then
	# Signing rewrites the executable, so compare the Info.plist instead of the binary.
	cmp -s "$app/Contents/Info.plist" "$launcher/Contents/Info.plist" || fail "the DMG's launcher differs from $launcher"
fi

for res in splash.png font_display.ttf font_body.ttf; do
	[ -f "$app/Contents/Resources/$res" ] || fail "$res is not bundled"
done

# --render-preview runs before the install folder checks, so it works on the read-only image too.
echo "render from the image" >&2
"$app/Contents/MacOS/Launcher" --render-preview "$work/preview-image" > "$work/preview-image.log" 2>&1 || \
	{ cat "$work/preview-image.log" >&2; fail "the launcher failed to render from the image"; }
[ "$(ls "$work/preview-image" | wc -l)" -ge 30 ] || fail "too few preview pages from the image"

# The player's install: the folder dragged out of the image.
echo "render from a copied install folder" >&2
ditto "$mount/Alestia Online" "$work/Applications/Alestia Online"
"$work/Applications/Alestia Online/Launcher.app/Contents/MacOS/Launcher" --render-preview "$work/preview-install" \
	> "$work/preview-install.log" 2>&1 || { cat "$work/preview-install.log" >&2; fail "the installed launcher failed to render"; }
[ "$(ls "$work/preview-install" | wc -l)" -ge 30 ] || fail "too few preview pages from the install"

codesign --verify --strict "$work/Applications/Alestia Online/Launcher.app" || fail "the installed launcher's signature is broken"

echo "AlestiaOnline.dmg OK ($version)" >&2
