# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Builds the Water_Ocean material graph from code and writes it into the .hmat.

Why a generator instead of hand-editing the graph: the look needs many iterations, every
iteration has to survive a compile in mmo_edit, and a script keeps the whole graph reviewable
and reproducible in one place. Tuning values are exposed as named parameters, so per-zone
variants belong in .hmi instances, not in edits to this file.

Design rules the graph follows (each one fixes a defect of the Water_Base graph it replaces):

  * All view-dependent optics go to EMISSIVE, and Base Color is black. The forward shader lights
    Base Color (ambient * baseColor + sun * baseColor * NdotL); the refracted scene colour is
    already lit, so routing it through Base Color lights it a second time and greys the whole
    surface out. With a black base colour the engine still contributes its GGX sun glint from the
    Normal pin, which is exactly the part worth keeping.
  * Fresnel, the reflection vector and SSR all use a WORLD-space normal. The sampled normal is
    tangent space; the Fresnel and reflection expressions dot it against the world-space view
    vector, so feeding them the raw sample makes reflection strength follow compass direction.
  * Depth tint is Beer-Lambert extinction on the water column, blended toward a scatter colour that
    itself runs shallow -> deep. Water_Base had its shallow/deep Lerp alpha unconnected, so the deep
    colour never appeared.
  * Opacity only softens the shoreline. Past EdgeFadeDistance the surface is opaque, because the
    refracted scene is already composited into the emissive term.

Usage:
    python tools/water_gen/build_ocean_material.py               # the ocean look
    python tools/water_gen/build_ocean_material.py --mode ssr    # debug: SSR hits vs misses

After running, open the material in mmo_edit and save it: the material tool rewrites only the
graph chunk and cannot compile shaders.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MATERIAL_TOOL = os.path.join(REPO_ROOT, ".claude", "skills", "mmo-material-editor", "scripts", "material_tool.py")
DEFAULT_TARGET = os.path.join("data", "client", "Worlds", "Water_Ocean.hmat")

# Space enum from graphics/material_compiler.h.
SPACE_WORLD = 1
SPACE_TANGENT = 4

# Property defaults for values the catalog does not carry. Arithmetic nodes use "Value 2" as the
# constant when their B pin is unlinked, so anything not listed defaults to the neutral 1.0 for
# those and 0.0 elsewhere.
PROPERTY_DEFAULTS = {
    ("LerpNode", "Value B"): 1.0,
    ("ClampNode", "Max Default"): 1.0,
    ("SmoothStepNode", "Edge 1 Default"): 1.0,
    ("FresnelNode", "Exponent"): 5.0,
    ("FresnelNode", "Base Reflect Fraction"): 0.02,
    ("PowerNode", "Const Exponent"): 1.0,
}


def run_tool(*args):
    subprocess.run([sys.executable, MATERIAL_TOOL, "--source-root", REPO_ROOT, *args], check=True)


def default_property(type_name, prop):
    key = (type_name, prop["name"])
    if key in PROPERTY_DEFAULTS:
        return PROPERTY_DEFAULTS[key]

    kind = prop["type"]
    if kind == "float":
        return 1.0 if prop["name"] in ("Value 1", "Value 2") else 0.0
    if kind == "bool":
        return False
    if kind == "int":
        return 0
    if kind == "color":
        return [1.0, 1.0, 1.0, 1.0]
    return ""


class Out:
    """A reference to one output pin of a node."""

    def __init__(self, node, pin):
        self.node = node
        self.pin = pin


class GraphBuilder:
    def __init__(self, catalog, root):
        self.catalog = {entry["type_name"]: entry for entry in catalog}
        self.root = root
        self.nodes = [root]
        ids = [root["id"]] + [p["id"] for p in root["inputs"] + root["outputs"]]
        self.next_id = max(ids) + 1
        self.column = 0
        self.row = 0

    def _take_id(self):
        value = self.next_id
        self.next_id += 1
        return value

    def next_column(self):
        self.column += 1
        self.row = 0

    def add(self, type_name, props=None, ins=None):
        spec = self.catalog[type_name]
        props = props or {}

        node = {
            "id": self._take_id(),
            "type_id": spec["type_id"],
            "type_name": type_name,
            "display_name": spec["display_name"],
            "position": {"x": -2400.0 + self.column * 260.0, "y": -600.0 + self.row * 140.0,
                         "width": 200.0, "height": 110.0},
            "inputs": [{"id": self._take_id(), "link": None, "name": i["name"]} for i in spec["inputs"]],
            "outputs": [{"id": self._take_id(), "link": None, "name": o["name"]} for o in spec["outputs"]],
            "properties": [],
        }
        self.row += 1

        for prop in spec["properties"]:
            value = props[prop["name"]] if prop["name"] in props else default_property(type_name, prop)
            node["properties"].append({"name": prop["name"], "type": prop["type"], "value": value})

        unknown = set(props) - {p["name"] for p in spec["properties"]}
        if unknown:
            raise ValueError("%s has no properties %s" % (type_name, sorted(unknown)))

        self.nodes.append(node)

        for input_name, source in (ins or {}).items():
            self.link(source, node, input_name)

        return node

    def out(self, node, pin=None):
        if pin is None:
            return Out(node, node["outputs"][0])
        for candidate in node["outputs"]:
            if candidate["name"] == pin:
                return Out(node, candidate)
        raise ValueError("%s has no output %r" % (node["type_name"], pin))

    def link(self, source, target, input_name):
        if isinstance(source, dict):
            source = self.out(source)
        for candidate in target["inputs"]:
            if candidate["name"] == input_name:
                candidate["link"] = source.pin["id"]
                # Outputs fan out; the field only records one consumer.
                if source.pin["link"] is None:
                    source.pin["link"] = candidate["id"]
                return
        raise ValueError("%s has no input %r" % (target["type_name"], input_name))

    # --- small helpers so the graph below reads as maths ------------------------------------

    def scalar(self, name, value):
        return self.add("ScalarParameterNode", {"Name": name, "Value": float(value)})

    def vector(self, name, rgba):
        return self.out(self.add("VectorParameterNode", {"Name": name, "Value": list(rgba)}), "RGB")

    def const(self, value):
        return self.add("ConstFloatNode", {"Value": float(value)})

    def const_rgb(self, rgba):
        return self.out(self.add("ConstVectorNode", {"Value": list(rgba)}), "RGB")

    def mul(self, a, b=None, constant=None):
        props = {} if constant is None else {"Value 2": float(constant)}
        ins = {"A": a}
        if b is not None:
            ins["B"] = b
        return self.add("MultiplyNode", props, ins)

    def addn(self, a, b=None, constant=None):
        props = {} if constant is None else {"Value 2": float(constant)}
        ins = {"A": a}
        if b is not None:
            ins["B"] = b
        return self.add("AddNode", props, ins)

    def sub(self, a, b=None, constant=None):
        props = {} if constant is None else {"Value 2": float(constant)}
        ins = {"A": a}
        if b is not None:
            ins["B"] = b
        return self.add("SubtractNode", props, ins)

    def div(self, a, b=None, constant=None):
        props = {} if constant is None else {"Value 2": float(constant)}
        ins = {"A": a}
        if b is not None:
            ins["B"] = b
        return self.add("DivideNode", props, ins)

    def sat(self, x):
        return self.add("SaturateNode", ins={"m_input": x})

    def lerp(self, a, b, alpha):
        return self.add("LerpNode", ins={"A": a, "B": b, "Alpha": alpha})

    def mask(self, x, r=False, g=False, b=False, a=False):
        return self.add("MaskNode", {"R": r, "G": g, "B": b, "A": a}, {"m_input": x})

    def panner(self, uvs, speed_x, speed_y):
        return self.add("PannerNode", {"Speed X": float(speed_x), "Speed Y": float(speed_y)}, {"UVs": uvs})


def build_graph(b, mode):
    """Wires the ocean. Remember the compiler's typing rule: Add, Subtract and Divide take the
    type of their FIRST operand, so the wider operand always goes into A."""

    # --- Normals (tangent space) ------------------------------------------------------------
    uv0 = b.add("TextureCoordNode", {"UV Coordinate Index": 0})

    def normal_layer(tiling_name, tiling, speed, strength):
        uvs = b.panner(b.mul(uv0, b.scalar(tiling_name, tiling)), *speed)
        sample = b.add("TextureParameterNode",
                       {"Name": "Normal", "Texture": "Models/Engine/WaterNormal.htex", "Sampler Type": 1},
                       {"UVs": uvs})
        return b.mul(b.mask(b.out(sample, "RGBA"), r=True, g=True), strength)

    b.next_column()
    normal_strength = b.scalar("NormalStrength", 0.55)
    swell_strength = b.scalar("SwellStrength", 0.35)
    layer_a = normal_layer("NormalTilingA", 1.0, (0.020, 0.012), normal_strength)
    layer_b = normal_layer("NormalTilingB", 2.7, (-0.017, 0.025), normal_strength)
    layer_c = normal_layer("SwellTiling", 0.3, (0.006, -0.004), swell_strength)

    b.next_column()
    slope = b.addn(b.addn(layer_a, layer_b), layer_c)
    tangent_normal = b.add("NormalizeNode", ins={"m_input": b.add("AppendNode", ins={"A": slope, "B": b.const(1.0)})})
    world_normal = b.add("NormalizeNode", ins={"m_input": b.add(
        "TransformVectorNode", {"Source Space": SPACE_TANGENT, "Target Space": SPACE_WORLD}, {"Vector": tangent_normal})})

    # --- Water column ----------------------------------------------------------------------
    b.next_column()
    scene_depth = b.add("SceneDepthNode")
    pixel_depth = b.add("PixelDepthNode")
    thickness = b.add("MaxNode", {"Value 2": 0.0}, {"A": b.sub(scene_depth, pixel_depth)})

    # --- Screen-space reflection --------------------------------------------------------------
    b.next_column()
    ssr = b.add("ScreenSpaceReflectionNode", ins={
        "Normal": world_normal,
        "Max Distance": b.scalar("SSRMaxDistance", 200.0),
        "Steps": b.scalar("SSRSteps", 32.0),
    })
    ssr_color = b.out(ssr, "Color")
    ssr_mask = b.out(ssr, "Hit Mask")

    root_inputs = {}

    if mode == "ssr":
        # Debug: purple where the ray missed, the reflected scene where it hit. Nothing else.
        miss = b.const_rgb((0.35, 0.0, 0.35, 1.0))
        root_inputs["Emissive Color"] = b.lerp(miss, ssr_color, ssr_mask)
        root_inputs["Opacity"] = b.const(1.0)
        root_inputs["Roughness"] = b.const(1.0)
    else:
        # --- Refraction and depth tint --------------------------------------------------------
        b.next_column()
        refraction_strength = b.scalar("RefractionStrength", 30.0)
        shallow_fade = b.sat(b.div(thickness, constant=2.0))
        offset = b.mul(b.mul(b.mask(tangent_normal, r=True, g=True), refraction_strength), shallow_fade)
        refracted = b.out(b.add("SceneColorNode", ins={"UV Offset (px)": offset}), "Scene Color")

        absorption = b.vector("AbsorptionRGB", (0.30, 0.075, 0.05, 1.0))
        exponent = b.mul(b.mul(absorption, thickness), constant=-1.0)
        euler = b.const_rgb((2.718282, 2.718282, 2.718282, 1.0))
        extinction = b.add("PowerNode", ins={"Base": euler, "Exp": exponent})

        b.next_column()
        deep_t = b.sat(b.div(thickness, b.scalar("DeepDistance", 12.0)))
        scatter = b.lerp(b.vector("ShallowColor", (0.10, 0.62, 0.58, 1.0)),
                         b.vector("DeepColor", (0.01, 0.10, 0.20, 1.0)), deep_t)
        body = b.addn(b.mul(refracted, extinction),
                      b.mul(scatter, b.add("OneMinusNode", ins={"m_input": extinction})))

        # --- Reflection -----------------------------------------------------------------------
        b.next_column()
        reflection_vector = b.add("ReflectionVectorNode", ins={"Normal": world_normal})
        sky_t = b.add("PowerNode", ins={"Base": b.sat(b.mask(reflection_vector, g=True)), "Exp": b.const(0.6)})
        horizon = b.out(b.add("GlobalVectorParameterNode", {"Name": "SkyHorizonColor"}), "RGB")
        zenith = b.out(b.add("GlobalVectorParameterNode", {"Name": "SkyZenithColor"}), "RGB")
        sky = b.lerp(horizon, zenith, sky_t)
        reflection = b.mul(b.lerp(sky, ssr_color, ssr_mask), b.scalar("ReflectionStrength", 0.9))
        fresnel = b.add("FresnelNode", {"Exponent": 5.0, "Base Reflect Fraction": 0.02}, {"Normal": world_normal})
        water = b.lerp(body, reflection, fresnel)

        # --- Foam -----------------------------------------------------------------------------
        b.next_column()
        shore = b.add("OneMinusNode", ins={"m_input": b.sat(b.div(thickness, b.scalar("FoamDistance", 2.5)))})
        time = b.add("TimeNode")
        swash_arg = b.addn(b.mul(time, b.scalar("SwashSpeed", 0.8)), b.mul(thickness, b.scalar("SwashFrequency", 2.0)))
        swash = b.addn(b.mul(b.add("SineNode", ins={"m_inputPin": swash_arg}), constant=0.5), constant=0.5)
        band = b.sat(b.mul(shore, b.addn(b.mul(swash, constant=0.45), constant=0.55)))

        b.next_column()
        foam_uv = b.panner(b.mul(uv0, b.scalar("FoamTiling", 3.0)), 0.010, -0.008)
        foam_tex = b.out(b.add("TextureParameterNode",
                               {"Name": "Foam", "Texture": "Textures/Foam_01_C.htex", "Sampler Type": 0},
                               {"UVs": foam_uv}), "R")
        shore_foam = b.sat(b.mul(b.sub(b.addn(foam_tex, band), constant=1.0), b.scalar("FoamSharpness", 4.0)))

        cap_uv = b.panner(b.mul(uv0, b.scalar("WhitecapTiling", 0.4)), 0.004, 0.006)
        cap_tex = b.out(b.add("TextureParameterNode",
                              {"Name": "Foam", "Texture": "Textures/Foam_01_C.htex", "Sampler Type": 0},
                              {"UVs": cap_uv}), "R")
        caps = b.mul(b.sat(b.mul(b.sub(cap_tex, b.scalar("WhitecapThreshold", 0.8)), constant=6.0)),
                     b.scalar("WhitecapIntensity", 0.35))
        foam_mask = b.sat(b.addn(shore_foam, caps))

        b.next_column()
        sun_color = b.add("GlobalVectorParameterNode", {"Name": "SunColor"})
        sun_dir = b.add("GlobalVectorParameterNode", {"Name": "SunDirection"})
        sun_light = b.addn(b.mul(b.mul(b.out(sun_color, "RGB"), b.out(sun_color, "A")),
                                 b.sat(b.out(sun_dir, "G"))), constant=0.45)
        foam_lit = b.mul(b.vector("FoamColor", (0.92, 0.97, 1.0, 1.0)), sun_light)
        final = b.lerp(water, foam_lit, foam_mask)

        root_inputs["Emissive Color"] = final
        root_inputs["Opacity"] = b.sat(b.div(thickness, b.scalar("EdgeFadeDistance", 0.5)))
        root_inputs["Roughness"] = b.scalar("Roughness", 0.05)

    b.next_column()
    root_inputs["Base Color"] = b.const_rgb((0.0, 0.0, 0.0, 1.0))
    root_inputs["Metallic"] = b.scalar("Metallic", 0.0)
    root_inputs["Specular"] = b.scalar("Specular", 0.5)
    root_inputs["Normal"] = tangent_normal

    for candidate in b.root["inputs"]:
        candidate["link"] = None
    for name, source in root_inputs.items():
        b.link(source, b.root, name)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mode", choices=("full", "ssr"), default="full")
    parser.add_argument("--target", default=DEFAULT_TARGET)
    args = parser.parse_args()

    target = os.path.join(REPO_ROOT, args.target)

    with tempfile.TemporaryDirectory() as work:
        exported = os.path.join(work, "material.json")
        catalog_path = os.path.join(work, "catalog.json")
        run_tool("export-json", target, "--output", exported)
        run_tool("node-catalog", "--output", catalog_path)

        document = json.load(open(exported, encoding="utf-8"))
        catalog = json.load(open(catalog_path, encoding="utf-8"))
        catalog = catalog["nodes"] if isinstance(catalog, dict) else catalog

        graph = document["graph"]
        root = next(n for n in graph["nodes"] if n["id"] == graph["root_node_id"])
        for prop in root["properties"]:
            if prop["name"] in ("Lit", "Translucent", "Is Two Sided", "Receives Shadows"):
                prop["value"] = True
            elif prop["name"] in ("Casts Shadows", "Depth Write", "User Interface", "Masked"):
                prop["value"] = False
            elif prop["name"] == "Depth Test":
                prop["value"] = True

        builder = GraphBuilder(catalog, root)
        build_graph(builder, args.mode)

        graph["nodes"] = builder.nodes
        graph["node_count"] = len(builder.nodes)
        graph["next_id"] = builder.next_id

        json.dump(document, open(exported, "w", encoding="utf-8"), indent=1)
        run_tool("validate-json", exported)
        run_tool("apply-json", exported, "--output", target, "--overwrite", "--allow-stale-shaders")

    print("Wrote %s graph (%d nodes) to %s - open and save it in mmo_edit to compile."
          % (args.mode, len(builder.nodes), args.target))


if __name__ == "__main__":
    main()
