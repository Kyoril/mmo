#!/bin/bash
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# Builds AlestiaOnline.dmg, the macOS counterpart of AlestiaOnline-Setup.exe: a disk image
# with an "Alestia Online" folder holding Launcher.app, to be dragged to Applications. The
# launcher then downloads the game into that folder.
#
#   deploy/installer/macos/build_dmg.sh --version 1.0.1 --launcher bin/Launcher.app [--out-dir dist]
#
# Prints build output to stderr and only the DMG path to stdout.
#
# Signing switches on when the environment provides it (see README.md); without it the
# DMG is unsigned and players confirm the first start under Privacy & Security.
#   MACOS_SIGN_IDENTITY    "Developer ID Application: ..." in the keychain
#   MACOS_NOTARY_PROFILE   notarytool keychain profile (xcrun notarytool store-credentials)
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
version=""
launcher=""
outDir="$repo/dist"

while [ $# -gt 0 ]; do
	case "$1" in
		--version) version="$2"; shift 2 ;;
		--launcher) launcher="$2"; shift 2 ;;
		--out-dir) outDir="$2"; shift 2 ;;
		*) echo "unknown argument $1" >&2; exit 2 ;;
	esac
done

[ -n "$version" ] && [ -n "$launcher" ] || { echo "usage: $0 --version X.Y.Z --launcher <Launcher.app> [--out-dir <dir>]" >&2; exit 2; }
[ -d "$launcher/Contents/MacOS" ] || { echo "$launcher is not an app bundle" >&2; exit 1; }

# The bundle reports the version from the launcher-v tag it was built at; a mismatch means
# the DMG would be published under a version its launcher does not report.
built="$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$launcher/Contents/Info.plist")"
[ "$built" = "$version" ] || { echo "$launcher reports version $built, expected $version" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Stage. ditto keeps the bundle's symlinks, permissions and signature intact.
folder="$work/stage/Alestia Online"
mkdir -p "$folder"
ditto "$launcher" "$folder/Launcher.app"
xattr -cr "$folder"

# The linker signs only the executable, so the bundle's resources are not sealed. A
# downloaded (quarantined) app with that signature is reported as "damaged" with no way
# to open it; an ad-hoc signature over the whole bundle gets the normal "unidentified
# developer" prompt instead, which players can confirm under Privacy & Security.
if [ -n "${MACOS_SIGN_IDENTITY:-}" ]; then
	echo "Signing Launcher.app as $MACOS_SIGN_IDENTITY" >&2
	codesign --force --options runtime --timestamp --sign "$MACOS_SIGN_IDENTITY" "$folder/Launcher.app" >&2
else
	echo "Signing Launcher.app ad-hoc (no MACOS_SIGN_IDENTITY)" >&2
	codesign --force --sign - "$folder/Launcher.app" >&2
fi
codesign --verify --strict --verbose=2 "$folder/Launcher.app" >&2

# Background from the launcher's own art, 1x and 2x in one HiDPI tiff.
swift "$here/make_background.swift" "$repo/src/launcher/res" "$work/background.png" 1 >&2
swift "$here/make_background.swift" "$repo/src/launcher/res" "$work/background@2x.png" 2 >&2
tiffutil -cathidpicheck "$work/background.png" "$work/background@2x.png" -out "$work/background.tiff" >&2

# dmgbuild writes the Finder window layout (.DS_Store) itself, so no Finder scripting and
# no logged-in GUI session is needed.
python3 -m venv "$work/venv" >&2
"$work/venv/bin/pip" install --quiet --disable-pip-version-check "dmgbuild==1.6.7" >&2

mkdir -p "$outDir"
dmg="$(cd "$outDir" && pwd)/AlestiaOnline.dmg"
rm -f "$dmg"
"$work/venv/bin/dmgbuild" -s "$here/dmg_settings.py" -D "folder=$folder" -D "background=$work/background.tiff" \
	"Alestia Online" "$dmg" >&2

if [ -n "${MACOS_SIGN_IDENTITY:-}" ]; then
	codesign --force --timestamp --sign "$MACOS_SIGN_IDENTITY" "$dmg" >&2
	if [ -n "${MACOS_NOTARY_PROFILE:-}" ]; then
		echo "Notarizing $dmg" >&2
		xcrun notarytool submit "$dmg" --keychain-profile "$MACOS_NOTARY_PROFILE" --wait >&2
		xcrun stapler staple "$dmg" >&2
	fi
fi

echo "$dmg"
