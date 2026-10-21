# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Generate the glue screen skin (login, realm list, character select/create) and import it as HTEX.

Look: dark wood panels with a thin bronze edge and corner ornaments (Alestia WindowPanel), steel
framed buttons and text fields (fg4 stone edges) around an inset fill. Buttons are a deep
steel-blue vertical gradient with a faint stone grain and a soft top gloss; fields are dark stone.

Why the panel and the steel frame are re-baked at twice their size: a BorderComponent draws its
corners with the same number of UI units as it cuts source pixels (border_component.cpp). The UI is
authored at 4K, so at 1080p the source textures' 22 px wood edge and 18 px steel band would render
at half the size of the approved mockup. Doubling the source keeps the corners crisp at the
approved scale (1 source px of the original = 1 screen px at 1080p).

Outputs (data/client/Interface/GlueUI/):
  GlueSkinPanel.htex          1024x1024 DXT1   9-slice, borderSize 64
  GlueSkinSteelFrame.htex      512x512  DXT5   9-slice, borderSize 36 (draw over a fill inset 14)
  GlueSkinButton<State>.htex   512x128  DXT1   button fill, stretched into the 14-unit inset
                                               (Normal, Hovered, Pushed, Disabled, Checked)
  GlueSkinFieldFill.htex       512x128  DXT1   text field fill, same inset

Usage:
  python tools/glue_skin/make_glue_skin.py [--keep-png DIR] [--preview OUT.png] [--no-import]

Requires texconv.exe through the item icon importer, like tools/sky_moon/make_moon.py.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageEnhance, ImageFont

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLIENT = os.path.join(REPO_ROOT, "data", "client")
OUT_DIR = os.path.join(CLIENT, "Interface", "GlueUI")
IMPORTER = os.path.join(REPO_ROOT, ".agents", "skills", "mmo-item-designer", "scripts", "import_item_icon.py")
HTEX_TOOL = os.path.join(REPO_ROOT, ".agents", "skills", "mmo-material-editor", "scripts", "htex_tool.py")

SOURCES = {
	"panel": "Interface/GameUI/Alestia/WindowPanel.htex",
	"steel": "Interface/GameUI/fg4_borders_stone_edges_result.htex",
	"stone": "Interface/GameUI/fg4_borders_stone_bg_01_result.htex",
}

FILL_SIZE = (512, 128)
BUTTON_TOP = (78, 102, 132)     # #4E6684
BUTTON_BOTTOM = (26, 34, 50)    # #1A2232
STONE_OPACITY = 0.24
GLOSS_ALPHA = 55

# XML contract (GlueTemplates.xml); the preview renders with the same numbers.
PANEL_BORDER = 64
STEEL_BORDER = 36
FILL_INSET = 14
FIELD_TEXT_OFFSET = (34, 36, 34, 36)  # GlueTextField_ApplySkin (GlueParent.lua), fields are 128 tall


def decode_sources(work: str) -> dict[str, Image.Image]:
	images = {}
	for key, rel in SOURCES.items():
		out = os.path.join(work, key + ".png")
		subprocess.run([sys.executable, HTEX_TOOL, "preview", os.path.join(CLIENT, rel), "--output", out],
			check=True, stdout=subprocess.DEVNULL)
		images[key] = Image.open(out).convert("RGBA")
	return images


def double(img: Image.Image) -> Image.Image:
	return img.resize((img.width * 2, img.height * 2), Image.LANCZOS)


def gradient(w: int, h: int, top, bottom) -> Image.Image:
	g = Image.new("RGBA", (w, h))
	d = ImageDraw.Draw(g)
	for y in range(h):
		t = y / max(1, h - 1)
		d.line([(0, y), (w, y)], fill=tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,))
	return g


def stone_grain(stone: Image.Image, w: int, h: int) -> Image.Image:
	"""Stone at the mockup's density: a fill of w x h texels covers roughly w/2 x h/2 screen px at 1080p."""
	crop = stone.crop((64, 160, 64 + w // 2, 160 + h // 2))
	return crop.resize((w, h), Image.LANCZOS)


def gloss(w: int, h: int, strength: float) -> Image.Image:
	hl = Image.new("RGBA", (w, h), (0, 0, 0, 0))
	d = ImageDraw.Draw(hl)
	half = h / 2
	for y in range(int(half)):
		d.line([(0, y), (w, y)], fill=(255, 255, 255, int(GLOSS_ALPHA * strength * (1 - y / half))))
	return hl


def button_fill(stone: Image.Image, brightness: float = 1.0, gloss_strength: float = 1.0,
		saturation: float = 1.0) -> Image.Image:
	w, h = FILL_SIZE
	fill = gradient(w, h, BUTTON_TOP, BUTTON_BOTTOM)
	grain = stone_grain(stone, w, h)
	grain.putalpha(int(255 * STONE_OPACITY))
	fill.alpha_composite(grain)
	if brightness != 1.0:
		fill = ImageEnhance.Brightness(fill).enhance(brightness)
	if saturation != 1.0:
		fill = ImageEnhance.Color(fill).enhance(saturation)
	fill.alpha_composite(gloss(w, h, gloss_strength))
	return fill


def checked_fill(stone: Image.Image) -> Image.Image:
	"""Selected choice: brighter, fuller gloss, and a soft ivory glow rising from the bottom edge."""
	fill = button_fill(stone, brightness=1.35, gloss_strength=1.3)
	w, h = fill.size
	glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
	d = ImageDraw.Draw(glow)
	for y in range(h // 2, h):
		t = (y - h / 2) / (h / 2)
		d.line([(0, y), (w, y)], fill=(243, 238, 226, int(70 * t * t)))
	fill.alpha_composite(glow)
	return fill


def field_fill(stone: Image.Image) -> Image.Image:
	w, h = FILL_SIZE
	return ImageEnhance.Brightness(stone_grain(stone, w, h)).enhance(0.25)


def build(images: dict[str, Image.Image]) -> dict[str, Image.Image]:
	stone = images["stone"]
	out = {
		"GlueSkinPanel": double(images["panel"]).convert("RGB").convert("RGBA"),
		"GlueSkinSteelFrame": double(images["steel"]),
		"GlueSkinButtonNormal": button_fill(stone),
		"GlueSkinButtonHovered": button_fill(stone, brightness=1.25),
		"GlueSkinButtonPushed": button_fill(stone, brightness=0.72, gloss_strength=0.35),
		"GlueSkinButtonDisabled": button_fill(stone, brightness=0.5, gloss_strength=0.4, saturation=0.25),
		"GlueSkinButtonChecked": checked_fill(stone),
		"GlueSkinFieldFill": field_fill(stone),
	}
	for name, img in out.items():
		if name != "GlueSkinSteelFrame":
			img.putalpha(255)
	return out


def import_all(textures: dict[str, Image.Image], png_dir: str) -> None:
	for name, img in textures.items():
		png = os.path.join(png_dir, name + ".png")
		img.save(png)
		fmt = "dxt5" if name == "GlueSkinSteelFrame" else "dxt1"
		subprocess.run([sys.executable, IMPORTER, png, os.path.join(OUT_DIR, name + ".htex"), "--format", fmt],
			check=True)


# ---------------------------------------------------------------- offline preview (engine emulation)

def nine(tex: Image.Image, border: int, w: int, h: int, scale: float) -> Image.Image:
	"""BorderComponent: cut `border` source px, draw them `border * scale` screen px wide."""
	tw, th = tex.size
	b = int(round(border * scale))
	out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
	xs = [(0, border, 0, b), (border, tw - border, b, w - b), (tw - border, tw, w - b, w)]
	ys = [(0, border, 0, b), (border, th - border, b, h - b), (th - border, th, h - b, h)]
	for sx0, sx1, dx0, dx1 in xs:
		for sy0, sy1, dy0, dy1 in ys:
			if dx1 <= dx0 or dy1 <= dy0:
				continue
			out.alpha_composite(tex.crop((sx0, sy0, sx1, sy1)).resize((dx1 - dx0, dy1 - dy0), Image.LANCZOS), (dx0, dy0))
	return out


def framed(fill: Image.Image, steel: Image.Image, w: int, h: int, scale: float, frame_tint=None) -> Image.Image:
	out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
	i = int(round(FILL_INSET * scale))
	out.alpha_composite(fill.resize((w - 2 * i, h - 2 * i), Image.LANCZOS), (i, i))
	frame = nine(steel, STEEL_BORDER, w, h, scale)
	if frame_tint is not None:
		r, g, b, a = frame.split()
		rgb = Image.merge("RGB", (r, g, b))
		rgb = ImageEnhance.Brightness(rgb).enhance(frame_tint)
		frame = Image.merge("RGBA", (*rgb.split(), a))
	out.alpha_composite(frame)
	return out


def preview(textures: dict[str, Image.Image], out_path: str, scale: float = 0.5) -> None:
	font_path = os.path.join(CLIENT, "Fonts", "EBGaramond-Regular.ttf")
	# Point sizes from GlueTemplates.xml at 96 dpi, in UI units, scaled to the preview resolution.
	header = ImageFont.truetype(font_path, int(round(41 * 96 / 72 * scale)))
	small = ImageFont.truetype(font_path, int(round(32 * 96 / 72 * scale)))
	ivory = (243, 238, 226, 255)
	steel = textures["GlueSkinSteelFrame"]
	panel = textures["GlueSkinPanel"]

	def px(v):
		return int(round(v * scale))

	def text(img, box, s, font, color):
		d = ImageDraw.Draw(img)
		l, t, r, b = d.textbbox((0, 0), s, font=font)
		x = box[0] + (box[2] - box[0] - (r - l)) / 2 - l
		y = box[1] + (box[3] - box[1] - (b - t)) / 2 - t
		d.text((x + 1, y + 1), s, font=font, fill=(0, 0, 0, 200))
		d.text((x, y), s, font=font, fill=color)

	def button(img, x, y, w_ui, h_ui, label, state="Normal"):
		w, h = px(w_ui), px(h_ui)
		tint = 0.6 if state == "Disabled" else None
		img.alpha_composite(framed(textures["GlueSkinButton" + state], steel, w, h, scale, tint), (x, y))
		color = {"Disabled": (138, 134, 126, 255), "Hovered": (255, 255, 255, 255),
			"Pushed": (216, 210, 198, 255)}.get(state, ivory)
		text(img, (x, y, x + w, y + h), label, small, color)

	def field(img, x, y, w_ui, h_ui, value="", caret=False):
		"""TextFieldRenderer: text area = frame minus FIELD_TEXT_OFFSET, text centred, caret one area tall."""
		w, h = px(w_ui), px(h_ui)
		img.alpha_composite(framed(textures["GlueSkinFieldFill"], steel, w, h, scale), (x, y))
		left, top, right, bottom = (px(v) for v in FIELD_TEXT_OFFSET)
		area = (x + left, y + top, x + w - right, y + h - bottom)
		d = ImageDraw.Draw(img)
		# EB Garamond's ascender-to-descender box, laid out like Font::DrawText from the area top.
		asc, desc = small.getmetrics()
		baseline = area[1] + (area[3] - area[1] - (asc + desc)) / 2 + asc
		if value:
			d.text((area[0], baseline), value, font=small, fill=(255, 255, 255, 255), anchor="ls")
		if caret:
			cx = area[0] + (d.textlength(value, font=small) if value else 0) + 1
			d.rectangle((cx, area[1], cx + 1, area[3] - 1), fill=(255, 255, 255, 255))

	bg_path = os.path.join(REPO_ROOT, ".superpowers", "sdd", "2026-10-21-plaza-character-select", "shots", "v3_boot_login.png")
	canvas = Image.new("RGBA", (1500, 1000), (24, 26, 30, 255))
	if os.path.isfile(bg_path):
		bg = Image.open(bg_path).convert("RGBA").crop((560, 320, 1360, 820))
		canvas.alpha_composite(bg, (0, 0))
	d = ImageDraw.Draw(canvas)

	# Login panel as AccountLogin.xml lays it out (panel-local UI units): LoginFormBackdrop 740x740,
	# name label 72-132, name field 140-268, password label 284-344, password field 356-484,
	# LoginButton 540-668. Placed over the old panel in the screenshot (centre + 16 UI).
	pw, ph = px(740), px(740)
	ox, oy = (800 - pw) // 2, 51
	canvas.alpha_composite(nine(panel, PANEL_BORDER, pw, ph, scale), (ox, oy))
	fx = ox + (pw - px(600)) // 2
	text(canvas, (ox, oy + px(72), ox + pw, oy + px(132)), "Account Name", header, ivory)
	field(canvas, fx, oy + px(140), 600, 128, "kyoril", caret=True)
	text(canvas, (ox, oy + px(284), ox + pw, oy + px(344)), "Account Password", header, ivory)
	field(canvas, fx, oy + px(356), 600, 128, "********")
	button(canvas, fx, oy + px(540), 600, 128, "Login")

	# Primary button in all four states (440x128) plus the checked choice state.
	x0, y0 = 820, 30
	d.text((x0, y0), "GlueButtonPrimary states (440x128 UI, 1080p)", font=small, fill=ivory)
	for i, state in enumerate(("Normal", "Hovered", "Pushed", "Disabled")):
		yy = y0 + 40 + i * 80
		button(canvas, x0, yy, 440, 128, "Enter World", state)
		d.text((x0 + px(440) + 16, yy + 20), state, font=small, fill=ivory)
	yy = y0 + 40 + 4 * 80
	button(canvas, x0, yy, 396, 104, "Human", "Checked")
	button(canvas, x0 + px(396) + 16, yy, 396, 104, "Orc", "Normal")
	d.text((x0 + px(396) * 2 + 32, yy + 14), "Checked", font=small, fill=ivory)
	button(canvas, x0, yy + 72, 80, 80, "<")
	field(canvas, x0 + px(80) + 16, yy + 72, 600, 128, "Aldric", caret=True)

	# Glue dialog: 1600x490 with label and one button (GlueDialog.xml), drawn at 1080p.
	dw, dh = px(1600), px(490)
	dx, dy = 10, 560
	canvas.alpha_composite(nine(panel, PANEL_BORDER, dw, dh, scale), (dx, dy))
	text(canvas, (dx, dy + px(60), dx + dw, dy + px(60) + 30), "Connecting...", header, ivory)
	button(canvas, dx + (dw - px(440)) // 2, dy + dh - px(40) - px(128), 440, 128, "Cancel")

	# 2x zoom of a text field (text + caret) and a button so padding and the frame/fill seam can be judged.
	zoom_src = Image.new("RGBA", (px(600) + 20, 2 * px(128) + 30), (40, 30, 22, 255))
	field(zoom_src, 10, 10, 600, 128, "Aldric", caret=True)
	zoom_src.alpha_composite(framed(textures["GlueSkinButtonNormal"], steel, px(600), px(128), scale), (10, 20 + px(128)))
	text(zoom_src, (10, 20 + px(128), 10 + px(600), 20 + 2 * px(128)), "Login", small, ivory)
	zoom = zoom_src.resize((zoom_src.width * 2, zoom_src.height * 2), Image.NEAREST)
	canvas.alpha_composite(zoom, (836, 560))

	canvas.convert("RGB").save(out_path)


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("--keep-png", default=None, help="Directory to keep the generated PNGs in")
	parser.add_argument("--preview", default=None, help="Write an offline 1080p preview to this PNG")
	parser.add_argument("--no-import", action="store_true", help="Skip writing HTEX files")
	args = parser.parse_args()

	with tempfile.TemporaryDirectory(prefix="glue_skin_") as work:
		textures = build(decode_sources(work))
		png_dir = args.keep_png or work
		os.makedirs(png_dir, exist_ok=True)
		if args.no_import:
			for name, img in textures.items():
				img.save(os.path.join(png_dir, name + ".png"))
		else:
			import_all(textures, png_dir)
		if args.preview:
			preview(textures, args.preview)
			print("preview: " + args.preview)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
