# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Adds the moon to the Sky.hmat graph JSON and gates the sun disc.

The sky dome draws one disc at the light's position (LightDirection). SkyComponent moves the light
along the sun's arc by day and the moon's arc by night and sets:
  SunDiscVisibility  1 while the sun disc should show, fading at the horizon
  MoonVisibility     the same for the moon
  MoonRight/MoonUp   tangent frame around the moon direction, divided by its angular radius
  MoonColor          the environment's moon colour, tints the halo
The moon disc samples Textures/T_Sky_Moon.htex (tools/sky_moon/make_moon.py; rgb premultiplied).

Usage:
  python build_sky_material.py <sky.json> <out.json>

Then write the graph with material_tool.py apply-json --allow-stale-shaders and compile it with
  mmo_edit --rebuild-material Models/Sky.hmat
"""

import json
import sys

TYPE_IDS = {
	"AddNode": 3862753360,
	"MultiplyNode": 1778499981,
	"DotNode": 1811812588,
	"AppendNode": 769987721,
	"OneMinusNode": 3707627949,
	"SaturateNode": 891710880,
	"PowerNode": 174352402,
	"TextureNode": 3062158288,
	"ScalarParameterNode": 447849396,
	"VectorParameterNode": 3273156381,
}

TEX_OUT = ("RGB", "R", "G", "B", "A", "RGBA")
VEC_OUT = ("RGB", "R", "G", "B", "A", "ARGB")

# Existing Sky.hmat nodes.
CAMERA_VECTOR_RGB = 280   # Mask of the camera vector
DOT_VIEW_LIGHT = 239      # Dot(camera vector, normalized LightDirection)
SUN_DISC = 272            # SunColor * SunBrightness * disc mask
SKY_PLUS_SUN = 276        # Add(sky, sun disc), feeds the OverallColor multiply


class Graph:
	def __init__(self, graph):
		self.graph = graph
		self.nodes = {n["id"]: n for n in graph["nodes"]}
		self.next_id = graph["next_id"]

	def alloc(self):
		value = self.next_id
		self.next_id += 1
		return value

	def add(self, type_name, x, y, inputs=(), outputs=(), properties=(), display=None):
		node = {
			"id": self.alloc(),
			"type_id": TYPE_IDS[type_name],
			"type_name": type_name,
			"display_name": display or type_name.replace("Node", ""),
			"position": {"x": float(x), "y": float(y), "width": 140.0, "height": 80.0},
			"inputs": [],
			"outputs": [],
			"properties": [{"name": k, "type": t, "value": v} for k, t, v in properties],
		}
		for name in inputs:
			node["inputs"].append({"id": self.alloc(), "link": None, "name": name})
		for name in outputs:
			node["outputs"].append({"id": self.alloc(), "link": None, "name": name})
		self.nodes[node["id"]] = node
		return node

	@staticmethod
	def pin(node, name):
		for p in node["inputs"] + node["outputs"]:
			if p["name"] == name:
				return p
		raise KeyError(f"{node['type_name']} has no pin {name}")

	def link(self, src, src_pin, dst, dst_pin):
		out_pin = self.pin(src, src_pin)
		in_pin = self.pin(dst, dst_pin)
		in_pin["link"] = out_pin["id"]
		out_pin["link"] = in_pin["id"]

	def finish(self):
		self.graph["nodes"] = list(self.nodes.values())
		self.graph["node_count"] = len(self.graph["nodes"])
		self.graph["next_id"] = self.next_id
		return self.graph


def scalar(g, name, value, x, y):
	return g.add("ScalarParameterNode", x, y, outputs=("m_Float",),
				 properties=(("Name", "string", name), ("Value", "float", value)), display="Scalar Parameter")


def vector(g, name, value, x, y):
	return g.add("VectorParameterNode", x, y, outputs=VEC_OUT,
				 properties=(("Name", "string", name), ("Value", "color", value)), display="Vector Parameter")


def binary(g, type_name, x, y, value2=1.0):
	# Dot and Append have no constant fallbacks for their inputs.
	properties = () if type_name in ("DotNode", "AppendNode") else (("Value 1", "float", 1.0), ("Value 2", "float", value2))
	return g.add(type_name, x, y, inputs=("A", "B"), outputs=("m_output",), properties=properties)


def unary(g, type_name, x, y, in_pin="m_input", out_pin="m_output"):
	return g.add(type_name, x, y, inputs=(in_pin,), outputs=(out_pin,))


def build(document):
	g = Graph(document["graph"])
	if any(n.get("properties") and n["properties"][0].get("value") == "MoonVisibility" for n in g.nodes.values()):
		raise SystemExit("Sky.hmat already has the moon")

	view = g.nodes[CAMERA_VECTOR_RGB]
	view_dot_light = g.nodes[DOT_VIEW_LIGHT]
	sun = g.nodes[SUN_DISC]
	sky_plus_sun = g.nodes[SKY_PLUS_SUN]
	x0, y0 = -600.0, 1200.0

	# Sun disc only while the sun is up.
	sun_visibility = scalar(g, "SunDiscVisibility", 1.0, 400, 960)
	sun_gated = binary(g, "MultiplyNode", 700, 900)
	g.link(sun, "m_output", sun_gated, "A")
	g.link(sun_visibility, "m_Float", sun_gated, "B")
	g.link(sun_gated, "m_output", sky_plus_sun, "B")

	# Moon texture coordinates: the camera vector in the moon's tangent frame, [-1, 1] across the disc.
	right = vector(g, "MoonRight", [28.57, 0.0, 0.0, 0.0], x0 - 300, y0)
	up = vector(g, "MoonUp", [0.0, 28.57, 0.0, 0.0], x0 - 300, y0 + 200)
	ox = binary(g, "DotNode", x0, y0)
	oy = binary(g, "DotNode", x0, y0 + 120)
	g.link(view, "m_output", ox, "A")
	g.link(right, "RGB", ox, "B")
	g.link(view, "m_output", oy, "A")
	g.link(up, "RGB", oy, "B")
	offset = binary(g, "AppendNode", x0 + 160, y0 + 60)
	g.link(ox, "m_output", offset, "A")
	g.link(oy, "m_output", offset, "B")
	half = binary(g, "MultiplyNode", x0 + 320, y0 + 60, 0.5)
	g.link(offset, "m_output", half, "A")
	uv = binary(g, "AddNode", x0 + 480, y0 + 60, 0.5)
	g.link(half, "m_output", uv, "A")
	moon_tex = g.add("TextureNode", x0 + 640, y0, inputs=("UVs",), outputs=TEX_OUT,
					 properties=(("Texture", "asset_path", "Textures/T_Sky_Moon.htex"), ("Sampler Type", "int", 0)),
					 display="Texture")
	g.link(uv, "m_output", moon_tex, "UVs")

	# The texture wraps, so keep only the disc around the moon direction, never its tiled copies
	# or the antipode.
	# Squared distance via Dot: the Length node types its result like its input (float2 here).
	dist = binary(g, "DotNode", x0 + 320, y0 + 220)
	g.link(offset, "m_output", dist, "A")
	g.link(offset, "m_output", dist, "B")
	inside = unary(g, "OneMinusNode", x0 + 480, y0 + 220)
	g.link(dist, "m_output", inside, "m_input")
	inside_sharp = binary(g, "MultiplyNode", x0 + 640, y0 + 220, 15.0)
	g.link(inside, "m_output", inside_sharp, "A")
	circle = unary(g, "SaturateNode", x0 + 800, y0 + 220)
	g.link(inside_sharp, "m_output", circle, "m_input")
	front_sharp = binary(g, "MultiplyNode", x0 + 640, y0 + 320, 50.0)
	g.link(view_dot_light, "m_output", front_sharp, "A")
	front = unary(g, "SaturateNode", x0 + 800, y0 + 320)
	g.link(front_sharp, "m_output", front, "m_input")
	mask = binary(g, "MultiplyNode", x0 + 960, y0 + 260)
	g.link(circle, "m_output", mask, "A")
	g.link(front, "m_output", mask, "B")

	brightness = scalar(g, "MoonBrightness", 1.6, x0 + 800, y0 - 100)
	disc_bright = binary(g, "MultiplyNode", x0 + 960, y0)
	g.link(moon_tex, "RGB", disc_bright, "A")
	g.link(brightness, "m_Float", disc_bright, "B")
	disc = binary(g, "MultiplyNode", x0 + 1120, y0 + 60)
	g.link(disc_bright, "m_output", disc, "A")
	g.link(mask, "m_output", disc, "B")

	# Soft halo in the moon's colour.
	facing = unary(g, "SaturateNode", x0 + 640, y0 + 440)
	g.link(view_dot_light, "m_output", facing, "m_input")
	glow_exponent = scalar(g, "MoonGlowExponent", 400.0, x0 + 640, y0 + 540)
	glow_curve = g.add("PowerNode", x0 + 800, y0 + 460, inputs=("Base", "Exp"), outputs=("m_output",),
					   properties=(("Const Exponent", "float", 400.0),))
	g.link(facing, "m_output", glow_curve, "Base")
	g.link(glow_exponent, "m_Float", glow_curve, "Exp")
	glow_strength = scalar(g, "MoonGlow", 0.12, x0 + 800, y0 + 580)
	glow_scaled = binary(g, "MultiplyNode", x0 + 960, y0 + 480)
	g.link(glow_curve, "m_output", glow_scaled, "A")
	g.link(glow_strength, "m_Float", glow_scaled, "B")
	moon_color = vector(g, "MoonColor", [0.55, 0.65, 0.9, 1.0], x0 + 960, y0 + 600)
	glow = binary(g, "MultiplyNode", x0 + 1120, y0 + 500)
	g.link(glow_scaled, "m_output", glow, "A")
	g.link(moon_color, "RGB", glow, "B")

	moon_sum = binary(g, "AddNode", x0 + 1280, y0 + 200)
	g.link(disc, "m_output", moon_sum, "A")
	g.link(glow, "m_output", moon_sum, "B")
	moon_visibility = scalar(g, "MoonVisibility", 0.0, x0 + 1280, y0 + 340)
	moon = binary(g, "MultiplyNode", x0 + 1440, y0 + 220)
	g.link(moon_sum, "m_output", moon, "A")
	g.link(moon_visibility, "m_Float", moon, "B")

	# sky + sun + moon replaces sky + sun in front of the OverallColor multiply.
	consumer_pin = next(p for n in g.nodes.values() for p in n["inputs"] if p.get("link") == g.pin(sky_plus_sun, "m_output")["id"])
	consumer = next(n for n in g.nodes.values() if consumer_pin in n["inputs"])
	total = binary(g, "AddNode", 0, 600)
	g.link(sky_plus_sun, "m_output", total, "A")
	g.link(moon, "m_output", total, "B")
	g.link(total, "m_output", consumer, consumer_pin["name"])

	document["graph"] = g.finish()
	return document


def main():
	with open(sys.argv[1], encoding="utf-8") as f:
		document = json.load(f)
	with open(sys.argv[2], "w", encoding="utf-8") as f:
		json.dump(build(document), f, indent=2)


if __name__ == "__main__":
	main()
