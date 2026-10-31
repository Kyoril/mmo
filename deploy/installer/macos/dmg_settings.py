# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#
# dmgbuild layout of AlestiaOnline.dmg. Run through build_dmg.sh, which passes:
#   -D folder=<staged "Alestia Online" folder>   -D background=<HiDPI tiff>
#
# The disk image holds the game folder, not the bare app: the launcher installs the game
# next to itself, so the folder dragged to Applications becomes the install folder.
# Icon positions must match make_background.swift.

import os.path

folder = defines["folder"]  # noqa: F821 - provided by dmgbuild
folder_name = os.path.basename(folder)

format = "UDZO"
filesystem = "HFS+"

files = [folder]
symlinks = {"Applications": "/Applications"}

background = defines["background"]  # noqa: F821
window_rect = ((200, 120), (660, 420))
default_view = "icon-view"
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False

icon_size = 112
text_size = 14
icon_locations = {
	folder_name: (170, 205),
	"Applications": (490, 205),
}
