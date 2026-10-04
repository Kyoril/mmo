# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Builds the GrassBlade.hmat graph JSON from Plant.hmat's exported graph.

The grass blade takes its colour from the ground it grows on: near the root it samples the same
texture, tint and macro variation as the terrain's grass layer at its world XZ, towards the tip it
fades into the blade texture. The terrain's painted vertex colour arrives as the per-instance tint
(see Foliage::PopulateChunk) and is overlaid exactly like the terrain does it. Blade height (0 at
the root, 1 at the tip) is the mesh's vertex alpha (see set_grass_vertex_heights.py).

Usage:
  python build_grass_blade_material.py <plant.json> <out.json>

Then write the graph with material_tool.py apply-json --allow-stale-shaders and compile it with
  mmo_edit --rebuild-material Models/FalwynPlains/Plants/GrassBlade.hmat
"""

import json
import sys

# Oakenshire_Boars_Enhanced.hmi's grass layer, so the blades match that ground.
GROUND_TEXTURE = "Textures/Tilesets/Collision_PoC/Grass_01.htex"
GROUND_SCALE = -4.5
GROUND_TINT = [0.7512713074684143, 0.932584285736084, 0.4435887932777405, 1.0]
MACRO_TEXTURE = "Textures/Foam_01_C.htex"
MACRO_SCALE = -96.0
MACRO_DARK = 0.62
MACRO_LIGHT = 1.08

TYPE_IDS = {
    "WorldPositionNode": 2025480940,
    "MaskNode": 3240430705,
    "ScalarParameterNode": 447849396,
    "VectorParameterNode": 3273156381,
    "TextureParameterNode": 2148760939,
    "DivideNode": 3999868,
    "MultiplyNode": 1778499981,
    "LerpNode": 2811013844,
    "PowerNode": 174352402,
    "VertexColorNode": 3526416866,
    "ConstFloatNode": 1360314258,
    "MaterialFunctionNode": 1738386996,
}

# Plant.hmat's unused colour gradient (never connected to the material) and its normal map.
REMOVED_NODES = {10, 12, 15, 19, 21, 23, 27, 31, 38, 45, 52, 93}

TEX_OUT = ("RGB", "R", "G", "B", "A", "RGBA")
VEC_OUT = ("RGB", "R", "G", "B", "A", "ARGB")


class Graph:
    def __init__(self, graph):
        self.graph = graph
        self.nodes = {n["id"]: n for n in graph["nodes"] if n["id"] not in REMOVED_NODES}
        self.next_id = graph["next_id"]

        removed_pins = set()
        for n in graph["nodes"]:
            if n["id"] in REMOVED_NODES:
                for p in n["inputs"] + n["outputs"]:
                    removed_pins.add(p["id"])
        for n in self.nodes.values():
            for p in n["inputs"] + n["outputs"]:
                if p.get("link") in removed_pins:
                    p["link"] = None

    def alloc(self):
        value = self.next_id
        self.next_id += 1
        return value

    def add(self, type_name, x, y, inputs=(), outputs=(), properties=(), display=None, extra=None):
        node = {
            "id": self.alloc(),
            "type_id": TYPE_IDS[type_name],
            "type_name": type_name,
            "display_name": display or type_name,
            "position": {"x": float(x), "y": float(y), "width": 140.0, "height": 80.0},
            "inputs": [],
            "outputs": [],
            "properties": [{"name": k, "type": t, "value": v} for k, t, v in properties],
        }
        for name in inputs:
            node["inputs"].append({"id": self.alloc(), "link": None, "name": name})
        for name in outputs:
            node["outputs"].append({"id": self.alloc(), "link": None, "name": name})
        if extra:
            node.update(extra)
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
        if out_pin.get("link") is None:
            out_pin["link"] = in_pin["id"]

    def finish(self):
        self.graph["nodes"] = list(self.nodes.values())
        self.graph["node_count"] = len(self.graph["nodes"])
        self.graph["next_id"] = self.next_id
        return self.graph


def scalar(g, name, value, x, y):
    return g.add("ScalarParameterNode", x, y, outputs=("m_Float",),
                 properties=(("Name", "string", name), ("Value", "float", value)), display="Scalar Parameter")


def mask(g, x, y, r, gr, b, a):
    return g.add("MaskNode", x, y, inputs=("m_input",), outputs=("m_output",),
                 properties=(("R", "bool", r), ("G", "bool", gr), ("B", "bool", b), ("A", "bool", a)), display="Mask")


def binary(g, type_name, x, y):
    return g.add(type_name, x, y, inputs=("A", "B"), outputs=("m_output",),
                 properties=(("Value 1", "float", 1.0), ("Value 2", "float", 1.0)), display=type_name.replace("Node", ""))


def lerp(g, x, y):
    return g.add("LerpNode", x, y, inputs=("A", "B", "Alpha"), outputs=("m_output",),
                 properties=(("Value A", "float", 0.0), ("Value B", "float", 1.0), ("Alpha", "float", 0.0)), display="Lerp")


def build(document):
    g = Graph(document["graph"])
    material = g.nodes[document["graph"]["root_node_id"]]
    diffuse = g.nodes[83]
    roughness = g.nodes[57]
    roughness["properties"][1]["value"] = 0.96

    # Ground colour, sampled exactly like the terrain's grass layer.
    wp = g.add("WorldPositionNode", -1700, -700, outputs=("\\0",), display="World Position")
    xz = mask(g, -1540, -700, True, False, True, False)
    g.link(wp, "\\0", xz, "m_input")

    ground_scale = scalar(g, "GroundScale", GROUND_SCALE, -1540, -600)
    ground_uv = binary(g, "DivideNode", -1380, -680)
    g.link(xz, "m_output", ground_uv, "A")
    g.link(ground_scale, "m_Float", ground_uv, "B")

    ground_tex = g.add("TextureParameterNode", -1220, -720, inputs=("UVs",), outputs=TEX_OUT,
                       properties=(("Name", "string", "GroundBaseColor"), ("Texture", "asset_path", GROUND_TEXTURE), ("Sampler Type", "int", 0)),
                       display="Texture Parameter")
    g.link(ground_uv, "m_output", ground_tex, "UVs")

    ground_tint = g.add("VectorParameterNode", -1220, -540, outputs=VEC_OUT,
                        properties=(("Name", "string", "GroundTint"), ("Value", "color", GROUND_TINT)), display="Vector Parameter")
    ground = binary(g, "MultiplyNode", -1040, -680)
    g.link(ground_tex, "RGB", ground, "A")
    g.link(ground_tint, "RGB", ground, "B")

    macro_scale = scalar(g, "MacroScale", MACRO_SCALE, -1540, -440)
    macro_uv = binary(g, "DivideNode", -1380, -440)
    g.link(xz, "m_output", macro_uv, "A")
    g.link(macro_scale, "m_Float", macro_uv, "B")
    macro_tex = g.add("TextureParameterNode", -1220, -420, inputs=("UVs",), outputs=TEX_OUT,
                      properties=(("Name", "string", "MacroVariation"), ("Texture", "asset_path", MACRO_TEXTURE), ("Sampler Type", "int", 0)),
                      display="Texture Parameter")
    g.link(macro_uv, "m_output", macro_tex, "UVs")
    macro_dark = scalar(g, "MacroDark", MACRO_DARK, -1220, -260)
    macro_light = scalar(g, "MacroLight", MACRO_LIGHT, -1220, -180)
    macro = lerp(g, -1040, -400)
    g.link(macro_dark, "m_Float", macro, "A")
    g.link(macro_light, "m_Float", macro, "B")
    g.link(macro_tex, "R", macro, "Alpha")

    ground_macro = binary(g, "MultiplyNode", -880, -600)
    g.link(ground, "m_output", ground_macro, "A")
    g.link(macro, "m_output", ground_macro, "B")

    # Blade colour.
    blade_tint = g.add("VectorParameterNode", -880, 160, outputs=VEC_OUT,
                       properties=(("Name", "string", "BladeTint"), ("Value", "color", [1.0, 1.0, 1.0, 1.0])), display="Vector Parameter")
    blade = binary(g, "MultiplyNode", -720, 120)
    g.link(diffuse, "RGB", blade, "A")
    g.link(blade_tint, "RGB", blade, "B")

    # Root-to-tip blend: vertex alpha is the blade height, vertex rgb the painted ground colour.
    vertex_color = g.add("VertexColorNode", -1040, -180, outputs=("m_coordinates",), display="Vertex Color")
    painted = mask(g, -880, -220, True, True, True, False)
    height = mask(g, -880, -140, False, False, False, True)
    g.link(vertex_color, "m_coordinates", painted, "m_input")
    g.link(vertex_color, "m_coordinates", height, "m_input")

    tip_power = scalar(g, "TipBlendPower", 1.5, -880, -60)
    tip_curve = g.add("PowerNode", -720, -120, inputs=("Base", "Exp"), outputs=("m_output",),
                      properties=(("Const Exponent", "float", 2.0),), display="Power")
    g.link(height, "m_output", tip_curve, "Base")
    g.link(tip_power, "m_Float", tip_curve, "Exp")
    tip_blend = scalar(g, "TipBlend", 0.45, -720, -20)
    tip_alpha = binary(g, "MultiplyNode", -560, -100)
    g.link(tip_curve, "m_output", tip_alpha, "A")
    g.link(tip_blend, "m_Float", tip_alpha, "B")

    color = lerp(g, -400, -300)
    g.link(ground_macro, "m_output", color, "A")
    g.link(blade, "m_output", color, "B")
    g.link(tip_alpha, "m_output", color, "Alpha")

    # Contact shading: slightly darker at the root so the tuft sits in the ground.
    root_shade = scalar(g, "RootShade", 0.8, -560, 40)
    one = g.add("ConstFloatNode", -560, 120, outputs=("m_Float",), properties=(("Value", "float", 1.0),), display="Const Float")
    shade = lerp(g, -400, 40)
    g.link(root_shade, "m_Float", shade, "A")
    g.link(one, "m_Float", shade, "B")
    g.link(height, "m_output", shade, "Alpha")

    shaded = binary(g, "MultiplyNode", -240, -200)
    g.link(color, "m_output", shaded, "A")
    g.link(shade, "m_output", shaded, "B")

    overlay = g.add("MaterialFunctionNode", -80, -200, inputs=("Base", "Blend"), outputs=("Result",),
                    properties=(("Material Function", "asset_path", "Models/Engine/Functions/Blend_Overlay.hmf"),),
                    display="Material Function", extra={"material_function": "Models/Engine/Functions/Blend_Overlay.hmf"})
    g.link(shaded, "m_output", overlay, "Base")
    g.link(painted, "m_output", overlay, "Blend")
    g.link(overlay, "Result", material, "Base Color")

    # Same specular as the terrain's grass layer; the engine default of 0.5 gives blades a sheen.
    specular = scalar(g, "Specular", 0.025, 0, 200)
    g.link(specular, "m_Float", material, "Specular")

    # Normal stays unconnected: the blade meshes' vertex normals point straight up, which lights
    # them like the ground beneath.
    g.pin(material, "Normal")["link"] = None

    document["graph"] = g.finish()
    document["name"] = "Models/FalwynPlains/Plants/GrassBlade.hmat"
    return document


def main():
    with open(sys.argv[1], encoding="utf-8") as f:
        document = json.load(f)
    build(document)
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        json.dump(document, f, indent=2)


if __name__ == "__main__":
    main()
