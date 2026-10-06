#!/bin/sh
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
# Compiles a miniature client tree with the real deploy/patch/source.txt layout and checks
# that the Linux update_compiler produces a list.txt naming the Windows binaries.
set -eu
work=$(mktemp -d)
mkdir -p "$work/deploy/patch"
for dir in Config Fonts Interface Models Sound Textures ClientDB Worlds 	Locales/Locale_deDE Locales/Locale_enUS Locales/Locale_frFR Locales/Locale_ruRU; do
	mkdir -p "$work/data/client/$dir"
	echo "smoke" > "$work/data/client/$dir/file.txt"
done
for bin in Launcher.exe fmod.dll mmo_client.exe mmo_error.exe; do
	echo "$bin" > "$work/deploy/patch/$bin"
done
cat > "$work/deploy/patch/source.txt" <<'EOF'
version = 0
root = (type = "fs", from = ".", to = "", entries =
{
	(type = "if", condition = "WINDOWS", value =
		(type = "fs", from = "Launcher.exe")
	)
	(type = "fs", from = "../../data/client", to = "Data", entries =
	{
		(type = "hpak2", from = "Config", to = "Misc.hpak", sub = "Config")
		(type = "hpak2", from = "Fonts", to = "Fonts.hpak", sub = "Fonts")
		(type = "hpak2", from = "Interface", to = "Interface.hpak", sub = "Interface")
		(type = "hpak2", from = "Models", to = "Models.hpak", sub = "Models")
		(type = "hpak2", from = "Sound", to = "Sound.hpak", sub = "Sound")
		(type = "hpak2", from = "Textures", to = "Textures.hpak", sub = "Textures")
		(type = "hpak2", from = "ClientDB", to = "ClientDB.hpak", sub = "ClientDB")
		(type = "hpak2", from = "Worlds", to = "Worlds.hpak", sub = "Worlds")
		(type = "fs", from = "Locales", to = "Locales", entries =
		{
			(type = "hpak2", from = "Locale_deDE", to = "Locale_deDE.hpak")
			(type = "hpak2", from = "Locale_enUS", to = "Locale_enUS.hpak")
			(type = "hpak2", from = "Locale_frFR", to = "Locale_frFR.hpak")
			(type = "hpak2", from = "Locale_ruRU", to = "Locale_ruRU.hpak")
		})
	})
	(type = "if", condition = "WINDOWS", value =
		(type = "fs", from = "fmod.dll")
	)
	(type = "if", condition = "WINDOWS", value =
		(type = "fs", from = "mmo_client.exe")
	)
	(type = "if", condition = "WINDOWS", value =
		(type = "fs", from = "mmo_error.exe")
	)
})
EOF
/app/update_compiler -s "$work/deploy/patch" -o "$work/out" -c zlib -j 2
for bin in Launcher.exe mmo_client.exe mmo_error.exe fmod.dll; do
	grep -q "$bin" "$work/out/list.txt" || { echo "list.txt lacks $bin"; exit 1; }
done
echo "update_compiler smoke test passed"
