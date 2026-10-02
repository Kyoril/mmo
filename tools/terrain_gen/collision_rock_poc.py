# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Reproducible single-page rock study. Generates sources; does not alter live pages.

Run from the repository root with Python + numpy + Pillow. Import with terrain_tool
using the printed command. Page 34,32 is deliberately outside the existing test area.
"""
from pathlib import Path
import json
import subprocess
import sys
import numpy as np
from PIL import Image

from terrain_lib import fbm

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "generated/terrain/collision-rock-poc"
SIZE = 533.3333129882812
MATERIAL = "Models/Terrain/Collision_Rock_PoC.hmi"


def smooth(a, b, value):
	t = np.clip((value - a) / (b - a), 0, 1)
	return t * t * (3 - 2 * t)


def make_height():
	z, x = np.mgrid[0:129, 0:129].astype(np.float32) * (SIZE / 128)
	noise = fbm(x.shape, 34, octaves=3, seed=821)
	# A low hillside built from broken ledges, each 5-10 m high. The broad
	# terrain carries the slope; embedded meshes supply breaks below grid size.
	u = x-263
	v = z-237
	footprint = 1-smooth(.72,1.14, np.sqrt((u/153)**2+(v/117)**2))
	h = (3.5 + 2*noise)*footprint
	for center, halfwidth, front, rise, phase in [
		(242,135,323,6.5,.3),
		(267,111,283,8.0,1.4),
		(243,81,243,9.0,2.6),
		(279,45,207,6.0,.9),
	]:
		local = x-center
		edge = front + 9*np.sin(local/31+phase)+4*np.sin(local/13-phase)
		span = 1-smooth(halfwidth-15,halfwidth+8,np.abs(local+7*np.sin(z/24)))
		ledge = 1-smooth(edge-6,edge+4,z)
		# Independent ends and rear falloff interrupt each grass shelf.
		ledge *= smooth(116,157,z) * span
		ramp = np.exp(-((x-(center-35+phase*14)-.3*(z-front))/15)**2)
		h += rise*ledge*(1-.75*ramp)
	# Small shoulders, oblique gullies and slanted exposed ribs.
	for cx,cz,rx,rz,rise in [(155,280,26,41,5),(337,253,28,37,6),(206,209,24,29,4),(377,300,20,27,5)]:
		r=np.sqrt(((x-cx)/rx)**2+((z-cz)/rz)**2)
		h += rise*np.maximum(1-r,0)
	for cx,depth in [(194,8.5),(310,9.0)]:
		h -= depth*np.exp(-((x-cx-.24*(z-260))/13)**2)*smooth(185,230,z)*(1-smooth(315,352,z))
	h += footprint*(.65*np.sin(x/9+z/17)+1.2*noise)
	h=np.maximum(h,0)
	# Exactly flat boundary collar preserves the neighbouring flat test terrain.
	border = np.minimum.reduce([x, z, SIZE - x, SIZE - z])
	h *= smooth(27, 65, border)
	h[border <= 27] = 0
	return h.astype(np.float32)


def main():
	OUT.mkdir(parents=True, exist_ok=True)
	h = make_height()
	# Fixed range includes exactly representable zero at the whole boundary.
	meta = {"world": "Collision", "pageRect": {"x0": 34, "z0": 32, "x1": 34, "z1": 32},
		"minY": 0.0, "maxY": 140.0, "material": MATERIAL}
	Image.fromarray(np.rint(h / 140 * 65535).astype(np.uint16)).save(OUT / "height.png")
	(OUT / "height.json").write_text(json.dumps(meta, indent=2) + "\n")
	# Weights sampled at native splat resolution. The compiled parent has an old
	# slope function whose bias parameter is unused. Force its base to triplanar
	# cliff and author the slope blend here: channels are rock/grass/scree/earth.
	high = np.asarray(Image.fromarray(h).resize((1009, 1009), Image.Resampling.BILINEAR))
	dz, dx = np.gradient(high, SIZE / 1008)
	slope = np.degrees(np.arctan(np.hypot(dx, dz)))
	z, x = np.mgrid[0:1009, 0:1009].astype(np.float32) * (SIZE / 1008)
	noise = fbm(high.shape, 110, octaves=3, seed=351)
	scree = smooth(3, 17, high) * (1 - smooth(27, 45, high))
	scree *= smooth(8, 24, slope) * (1 - smooth(39, 53, slope))
	scree *= np.clip(0.6 + noise * 1.4, 0, 1) * 0.85
	trail_z = 391 + 17 * np.sin(x / 80)
	earth = (1 - smooth(3, 8, np.abs(z - trail_z))) * 0.85
	earth *= 0 * smooth(45, 90, x) * (1 - smooth(443, 488, x)) * (1 - smooth(4, 10, high))
	rock = smooth(19 + 9 * noise, 37 + 9 * noise, slope)
	# Exposed ribs cross the upper grass instead of making uninterrupted green caps.
	ribs = smooth(.40, .72, np.sin(x/17 + z/41) * .5 + noise)
	rock = np.maximum(rock, ribs * smooth(9, 25, high) * .85)
	rock = np.maximum(rock, 0.35 * smooth(24, 31, high) * np.clip(0.5 + noise, 0, 1))
	grass = (1 - rock) * (1 - scree) * (1 - earth)
	weights = np.stack([rock * (1 - earth), grass, scree * (1 - rock), earth], axis=2)
	weights /= weights.sum(axis=2, keepdims=True)
	packed = np.floor(weights * 255).astype(np.uint8)
	packed[:, :, 0] += (255 - packed.sum(axis=2)).astype(np.uint8)
	Image.fromarray(packed).save(OUT / "splat.png")
	# Visible diagnostic (not an alpha-composited display of a weight texture).
	colors = np.array([[130, 133, 128], [99, 125, 61], [146, 144, 130], [137, 104, 65]])
	Image.fromarray(np.uint8(weights @ colors)).save(OUT / "splat-preview.png")
	# New instance only; the compiled parent is deliberately unchanged.
	material_tool = ROOT / ".agents/skills/mmo-material-editor/scripts/material_tool.py"
	subprocess.run([sys.executable, str(material_tool), "export-json",
		str(ROOT / "data/client/Models/Terrain/Oakenshire_BoarTerrain.hmi"),
		"--output", str(OUT / "instance-source.json")], check=True, cwd=ROOT)
	doc = json.loads((OUT / "instance-source.json").read_text())
	doc["name"] = MATERIAL
	params = {"scale_textures_Grass": 8.0, "scale_textures_Cliff": 14.0,
		"cliff_blend_contrast": 0.3, "bias_blend": 10.0, "sharp_blend": 1.0,
		"cliff_blend_intensity": 1.0, "cliff_normal_intensity": 1.0,
		"layer2_textures_scaling": 8.0, "layer3_textures_scaling": 12.0,
		"layer4_textures_scaling": 12.0, "MacroDark": 1.0, "MacroLight": 1.0,
		"height_blend_sharpness": 0.0, "cliff_edge_breakup_strength": 0.0}
	for surface in ("Grass", "Cliff", "DryDirt", "WetMud", "EdgeGrass"):
		params["Specular_" + surface] = 0.12
		params["Roughness_" + surface] = 0.95
	inherited = {p["name"]: p["value"] for p in doc["scalar_parameters"]}
	inherited.update(params)
	params = inherited
	doc["vector_parameters"] = [{"name": p["name"], "value": [1.0, 1.0, 1.0, 1.0]}
		for p in doc.get("vector_parameters", [])]
	doc["scalar_parameters"] = [{"name": k, "value": v} for k, v in params.items()]
	textures = {"Grass_BaseColor": "Textures/Tilesets/Collision_PoC/Grass_Olive.htex",
		"Grass_Normal": "Textures/Tilesets/Collision_PoC/Flat_Normal.htex",
		"Cliff_BaseColor": "Textures/Tilesets/Collision_PoC/Cliff_Granite.htex",
		"Cliff_Normal": "Textures/Stones/T_stone_normal.htex",
		"Splatting": "Textures/Tilesets/PH_Splatting.htex",
		"layer2_baseColor": "Textures/Tilesets/Collision_PoC/Grass_Olive.htex",
		"layer2_normal": "Textures/Tilesets/Collision_PoC/Flat_Normal.htex",
		"layer3_baseColor": "Textures/Tilesets/Collision_PoC/Cliff_Granite.htex",
		"layer3_normal": "Textures/Tilesets/Collision_PoC/Flat_Normal.htex",
		"layer4_baseColor": "Textures/Tilesets/T_ground_BaseColor.htex",
		"layer4_normal": "Textures/Tilesets/T_ground_Normal.htex"}
	inherited_textures = {p["name"]: p["texture"] for p in doc["texture_parameters"]}
	inherited_textures.update(textures)
	doc["texture_parameters"] = [{"name": k, "texture": v} for k, v in inherited_textures.items()]
	(OUT / "instance.json").write_text(json.dumps(doc, indent=2) + "\n")
	print(f"Page 34,32; maximum height {h.max():.2f}; {h.shape}; boundary zero")
	print("terrain_tool import --data data/client --heightmap generated/terrain/collision-rock-poc/height.png --meta generated/terrain/collision-rock-poc/height.json --splat generated/terrain/collision-rock-poc/splat.png")


if __name__ == "__main__":
	main()
