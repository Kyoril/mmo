# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Builds the Water_Ocean (and the flowing Water_River) material graph from code and writes it into the .hmat.

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
    python tools/water_gen/build_ocean_material.py --variant river
        # Water_River.hmat: no swell; normals and foam streaks travel along the page's flow UVs
        # (u runs downstream, see terrain_io WaterFlowChunk) at the speed in vertex colour green.

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
RIVER_TARGET = os.path.join("data", "client", "Worlds", "Water_River.hmat")

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


# Optical swell displaced in the vertex shader (MaterialNode "Vertex Waves"). Gentle on purpose:
# swimming and collision do not see it, so it must never look like it should move the player.
# Wave shape is compiled into the shader; changing it needs a material rebuild, not an .hmi.
ROOT_WAVE_PROPERTIES = {
    "Vertex Waves": True,
    "Wave Height": 0.18,
    "Wave Length": 22.0,
    "Wave Speed": 1.0,
    "Wave Direction": 35.0,
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


# Per-variant starting values of the exposed parameters (tuning belongs in .hmi instances).
RIVER_DEFAULTS = {
    # Murky, sediment-laden river: the colour takes over within a metre or two, little sky mirror.
    "NormalStrength": 0.7, "SwellStrength": 0.5, "NormalTilingA": 2.2, "NormalTilingB": 5.5, "SwellTiling": 0.8,
    "FlowCycle": 0.35, "FlowDistanceA": 0.6, "FlowDistanceB": 1.4, "FlowDistanceC": 0.3,
    "ShallowColor": (0.06, 0.075, 0.045, 1.0), "DeepColor": (0.012, 0.026, 0.02, 1.0),
    "AbsorptionRGB": (0.9, 0.55, 0.45, 1.0), "DeepDistance": 2.0, "CausticsStrength": 0.15,
    "ReflectionStrength": 0.2, "ReflectionDistortion": 0.65, "NormalFarStrength": 0.85, "NormalFadeDistance": 300.0, "GlitterStrength": 1.2, "GlitterPower": 350.0,
    "StreakIntensity": 0.06, "StreakThreshold": 0.78, "FoamDistance": 0.6, "ShoreFoamDepth": 0.25, "ShoreFoamFade": 0.6,
}


def build_graph(b, mode, variant="ocean"):
    """Wires the ocean. Remember the compiler's typing rule: Add, Subtract and Divide take the
    type of their FIRST operand, so the wider operand always goes into A."""

    river = variant == "river"
    if river:
        plain_scalar, plain_vector = b.scalar, b.vector
        b.scalar = lambda name, value: plain_scalar(name, RIVER_DEFAULTS.get(name, value))
        b.vector = lambda name, rgba: plain_vector(name, RIVER_DEFAULTS.get(name, rgba))

    # --- Normals (tangent space) ------------------------------------------------------------
    uv0 = b.add("TextureCoordNode", {"UV Coordinate Index": 0})

    def normal_layer(tiling_name, tiling, speed, strength):
        uvs = b.panner(b.mul(uv0, b.scalar(tiling_name, tiling)), *speed)
        sample = b.add("TextureParameterNode",
                       {"Name": "Normal", "Texture": "Models/Engine/WaterNormal.htex", "Sampler Type": 1},
                       {"UVs": uvs})
        return b.mul(b.mask(b.out(sample, "RGBA"), r=True, g=True), strength)

    # River flow: the page carries flow-aligned UVs (u downstream) and the local speed in vertex colour
    # green. Two copies of each layer slide downstream on phases half a cycle apart and cross-fade, so
    # differing speeds never shear the pattern apart over time (the classic flow-map scheme).
    flow_speed = None
    if river:
        flow_speed = b.mask(b.out(b.add("VertexColorNode"), "m_coordinates"), g=True)
        flow_t = b.mul(b.out(b.add("TimeNode"), "Time"), b.scalar("FlowCycle", 0.25))
        phase_a = b.add("FracNode", ins={"m_inputPin": flow_t})
        phase_b = b.add("FracNode", ins={"m_inputPin": b.addn(flow_t, constant=0.5)})
        # 1 where phase A restarts: there only B is shown.
        phase_blend = b.add("AbsNode", ins={"m_input": b.sub(b.mul(phase_a, constant=2.0), constant=1.0)})

        def flow_sample(uvs, distance, sample_fn):
            def at(phase):
                offset = b.mul(b.mul(phase, flow_speed), distance)
                return sample_fn(b.sub(uvs, b.add("AppendNode", ins={"A": offset, "B": b.const(0.0)})))
            return b.lerp(at(phase_a), at(phase_b), phase_blend)

        # Away from the current (bays, lakes, the basin: low speed) the flow-aligned UVs stretch and
        # swirl, so there the ripples cross-fade to the ocean's world-space panning instead.
        world_uv = b.mul(b.mask(b.add("WorldPositionNode"), r=True, b=True), constant=1.0 / 16.0)
        still_t = b.sat(b.mul(b.sub(flow_speed, b.scalar("StillWaterSpeed", 0.12)), constant=5.0))
        view_depth = b.add("PixelDepthNode")

        def normal_layer(tiling_name, tiling, distance, strength, still_speed, fade_name, fade_distance, fade_floor):
            def sample(uvs):
                node = b.add("TextureParameterNode",
                             {"Name": "Normal", "Texture": "Models/Engine/WaterNormal.htex", "Sampler Type": 1},
                             {"UVs": uvs})
                return b.mask(b.out(node, "RGBA"), r=True, g=True)
            tiling_value = b.scalar(tiling_name, tiling)
            flowing = flow_sample(b.mul(uv0, tiling_value), distance, sample)
            still = sample(b.panner(b.mul(world_uv, tiling_value), *still_speed))
            # Per-layer detail fade: fine layers calm down first, so the distance shows the broad
            # pattern instead of shrinking every layer into noise.
            fade = b.lerp(b.const(1.0), b.const(fade_floor), b.sat(b.div(view_depth, b.scalar(fade_name, fade_distance))))
            return b.mul(b.mul(b.lerp(still, flowing, still_t), strength), fade)

    b.next_column()
    normal_strength = b.scalar("NormalStrength", 0.55)
    swell_strength = b.scalar("SwellStrength", 0.35)
    if river:
        layer_a = normal_layer("NormalTilingA", 1.0, b.scalar("FlowDistanceA", 0.4), normal_strength,
                               (0.020, 0.012), "DetailFadeA", 120.0, 0.35)
        layer_b = normal_layer("NormalTilingB", 2.7, b.scalar("FlowDistanceB", 1.0), normal_strength,
                               (-0.017, 0.025), "DetailFadeB", 45.0, 0.0)
        layer_c = normal_layer("SwellTiling", 0.3, b.scalar("FlowDistanceC", 0.15), swell_strength,
                               (0.006, -0.004), "DetailFadeC", 400.0, 0.8)
    else:
        layer_a = normal_layer("NormalTilingA", 1.0, (0.020, 0.012), normal_strength)
        layer_b = normal_layer("NormalTilingB", 2.7, (-0.017, 0.025), normal_strength)
        layer_c = normal_layer("SwellTiling", 0.3, (0.006, -0.004), swell_strength)

    b.next_column()
    # Ripples fade toward a calmer surface with distance. Full-strength normals at grazing angles
    # alias into sparkle and make every view-dependent term (glint, SSR, Fresnel) flicker.
    view_distance = b.add("PixelDepthNode")
    distance_t = b.sat(b.div(view_distance, b.scalar("NormalFadeDistance", 90.0)))
    distance_factor = b.lerp(b.const(1.0), b.scalar("NormalFarStrength", 0.2), distance_t)
    slope = b.mul(b.addn(b.addn(layer_a, layer_b), layer_c), distance_factor)
    tangent_normal = b.add("NormalizeNode", ins={"m_input": b.add("AppendNode", ins={"A": slope, "B": b.const(1.0)})})
    world_normal = b.add("NormalizeNode", ins={"m_input": b.add(
        "TransformVectorNode", {"Source Space": SPACE_TANGENT, "Target Space": SPACE_WORLD}, {"Vector": tangent_normal})})

    # Reflections use a much flatter normal than lighting. With the full ripple normal, a small
    # tilt swings the reflected ray between hitting the scene and missing into the sky, so
    # neighbouring pixels flip between the two and the reflection shatters into hard fragments.
    # The flat base is the vertex normal, which the root's Vertex Waves tilt with the swell, so
    # reflections roll with the waves instead of sitting on a mirror-flat plane.
    wave_normal = b.add("VertexNormalNode")
    reflection_normal = b.add("NormalizeNode", ins={"m_input": b.lerp(
        wave_normal, world_normal, b.scalar("ReflectionDistortion", 0.3))})

    # --- Water column ----------------------------------------------------------------------
    b.next_column()
    scene_depth = b.add("SceneDepthNode")
    pixel_depth = b.add("PixelDepthNode")
    # Where the opaque pass wrote nothing (sky seen past the water's edge, holes in the ground)
    # scene depth reads 0. Treat that as very deep water rather than as shoreline, or every such
    # pixel gets full shore foam.
    has_scene = b.sat(b.mul(scene_depth, constant=1000.0))
    scene_depth_fixed = b.lerp(b.const(10000.0), scene_depth, has_scene)
    thickness = b.add("MaxNode", {"Value 2": 0.0}, {"A": b.sub(scene_depth_fixed, pixel_depth)})

    # --- Screen-space reflection --------------------------------------------------------------
    b.next_column()
    ssr = b.add("ScreenSpaceReflectionNode", ins={
        "Normal": reflection_normal,
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
        refracted_scene = b.out(b.add("SceneColorNode", ins={"UV Offset (px)": offset}), "Scene Color")

        # Caustics on the ground under the water. G-buffer depth is radial, so the floor point
        # seen through this pixel is exactly camera + direction * scene depth. Two panned copies of
        # the caustic web combined with Min give the slowly shifting, never repeating network of
        # light lines seen on the sand in shallow water.
        sun_color = b.add("GlobalVectorParameterNode", {"Name": "SunColor"})
        sun_dir = b.add("GlobalVectorParameterNode", {"Name": "SunDirection"})
        camera_pos = b.add("CameraPositionNode")
        view_dir = b.add("NormalizeNode", ins={"m_input": b.sub(b.add("WorldPositionNode"), camera_pos)})
        floor_pos = b.addn(camera_pos, b.mul(view_dir, scene_depth))
        floor_uv = b.mul(b.mask(floor_pos, r=True, b=True), b.scalar("CausticsTiling", 0.22))

        def caustic_layer(uvs):
            return b.out(b.add("TextureParameterNode",
                               {"Name": "Caustics", "Texture": "Textures/Caustics_01.htex", "Sampler Type": 0},
                               {"UVs": uvs}), "R")

        caustic = b.add("MinNode", ins={
            "A": caustic_layer(b.panner(floor_uv, 0.021, 0.013)),
            "B": caustic_layer(b.panner(b.mul(floor_uv, constant=1.37), -0.016, 0.012)),
        })
        caustic_depth = b.add("OneMinusNode", ins={"m_input": b.sat(b.div(thickness, b.scalar("CausticsDepth", 4.0)))})
        sun_strength = b.mul(b.out(sun_color, "A"), b.sat(b.out(sun_dir, "G")))
        caustic_light = b.mul(b.mul(b.mul(caustic, b.scalar("CausticsStrength", 1.4)), caustic_depth),
                              b.mul(sun_strength, has_scene))
        refracted = b.addn(refracted_scene, b.mul(b.mul(refracted_scene, b.out(sun_color, "RGB")), caustic_light))

        absorption = b.vector("AbsorptionRGB", (0.24, 0.08, 0.065, 1.0))
        exponent = b.mul(b.mul(absorption, thickness), constant=-1.0)
        euler = b.const_rgb((2.718282, 2.718282, 2.718282, 1.0))
        extinction = b.add("PowerNode", ins={"Base": euler, "Exp": exponent})

        b.next_column()
        deep_t = b.sat(b.div(thickness, b.scalar("DeepDistance", 7.0)))
        scatter = b.lerp(b.vector("ShallowColor", (0.055, 0.17, 0.165, 1.0)),
                         b.vector("DeepColor", (0.012, 0.05, 0.06, 1.0)), deep_t)
        body = b.addn(b.mul(refracted, extinction),
                      b.mul(scatter, b.add("OneMinusNode", ins={"m_input": extinction})))

        # --- Reflection -----------------------------------------------------------------------
        b.next_column()
        reflection_vector = b.add("ReflectionVectorNode", ins={"Normal": reflection_normal})
        sky_t = b.add("PowerNode", ins={"Base": b.sat(b.mask(reflection_vector, g=True)), "Exp": b.const(0.6)})
        horizon = b.out(b.add("GlobalVectorParameterNode", {"Name": "SkyHorizonColor"}), "RGB")
        zenith = b.out(b.add("GlobalVectorParameterNode", {"Name": "SkyZenithColor"}), "RGB")
        sky = b.lerp(horizon, zenith, sky_t)
        reflection = b.mul(b.lerp(sky, ssr_color, ssr_mask), b.scalar("ReflectionStrength", 0.5))
        fresnel = b.add("FresnelNode", {"Exponent": 5.0, "Base Reflect Fraction": 0.02}, {"Normal": reflection_normal})
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
        # Shore foam only where the terrain itself is shallow under the surface. The screen-space
        # thickness above is just as small against a swimmer's body as against a beach, so on its
        # own it wrapped every character in the water in shoreline foam. Each water vertex carries
        # the water depth over the terrain in vertex colour red, encoded over 0..8 units: keep this
        # constant equal to terrain::water_mesh::ShoreDepthEncodeRange.
        shore_depth_encode_range = 8.0
        shore_depth = b.mul(b.mask(b.add("VertexColorNode"), r=True), constant=shore_depth_encode_range)
        shore_mask = b.add("OneMinusNode", ins={"m_input": b.sat(b.div(
            b.sub(shore_depth, b.scalar("ShoreFoamDepth", 0.75)), b.scalar("ShoreFoamFade", 1.5)))})
        shore_foam = b.mul(b.sat(b.mul(b.sub(b.addn(foam_tex, band), constant=1.0), b.scalar("FoamSharpness", 4.0))),
                           shore_mask)
        if river:
            # Rivers have no surf: only a faint scum line where the water meets the bank.
            shore_foam = b.mul(shore_foam, b.scalar("ShoreFoamIntensity", 0.2))

        if river:
            # Foam streaks drifting with the current: the foam texture stretched along the flow.
            def foam_sample(uvs):
                return b.out(b.add("TextureParameterNode",
                                   {"Name": "Foam", "Texture": "Textures/Foam_01_C.htex", "Sampler Type": 0},
                                   {"UVs": uvs}), "R")
            streak_tiling = b.add("AppendNode", ins={"A": b.scalar("StreakTilingAlong", 0.35),
                                                     "B": b.scalar("StreakTilingAcross", 2.2)})
            streak_tex = flow_sample(b.mul(uv0, streak_tiling), b.scalar("StreakFlowDistance", 0.25), foam_sample)
            caps = b.mul(b.mul(b.mul(b.sat(b.mul(b.sub(streak_tex, b.scalar("StreakThreshold", 0.72)), constant=4.0)),
                                     b.scalar("StreakIntensity", 0.22)), flow_speed),
                         b.add("OneMinusNode", ins={"m_input": distance_t}))
        else:
            cap_uv = b.panner(b.mul(uv0, b.scalar("WhitecapTiling", 0.25)), 0.004, 0.006)
            cap_tex = b.out(b.add("TextureParameterNode",
                                  {"Name": "Foam", "Texture": "Textures/Foam_01_C.htex", "Sampler Type": 0},
                                  {"UVs": cap_uv}), "R")
            # Whitecaps fade with distance too: perspective squashes them into white streaks.
            caps = b.mul(b.mul(b.sat(b.mul(b.sub(cap_tex, b.scalar("WhitecapThreshold", 0.86)), constant=6.0)),
                               b.scalar("WhitecapIntensity", 0.18)),
                         b.add("OneMinusNode", ins={"m_input": distance_t}))
        foam_mask = b.sat(b.addn(shore_foam, caps))

        b.next_column()
        sun_light = b.addn(b.mul(b.mul(b.out(sun_color, "RGB"), b.out(sun_color, "A")),
                                 b.sat(b.out(sun_dir, "G"))), constant=0.45)
        foam_lit = b.mul(b.vector("FoamColor", (0.92, 0.97, 1.0, 1.0)), sun_light)

        # Sun glitter: a very tight highlight along the FULL ripple normal, so it breaks up into
        # the separate sparkles of a sunlit sea instead of the engine's one smooth GGX lobe.
        glitter_dir = b.add("ReflectionVectorNode", ins={"Normal": world_normal})
        glitter_dot = b.sat(b.add("DotNode", ins={"A": glitter_dir, "B": b.out(sun_dir, "RGB")}))
        glitter = b.mul(b.mul(b.out(sun_color, "RGB"), b.mul(b.out(sun_color, "A"), b.scalar("GlitterStrength", 5.0))),
                        b.add("PowerNode", ins={"Base": glitter_dot, "Exp": b.scalar("GlitterPower", 900.0)}))
        above_water = b.addn(b.lerp(water, foam_lit, foam_mask),
                             b.mul(glitter, b.add("OneMinusNode", ins={"m_input": foam_mask})))

        # --- Underside --------------------------------------------------------------------------
        # Seen from below, none of the above applies. There is no sky to reflect - and the Fresnel
        # term saturates to full reflection there, which drew an opaque sky-coloured lid over the
        # whole world above the water. There is no foam, and no water column behind the surface to
        # tint: the underwater post-process already fogs the path through the water. So the
        # underside shows the refracted world above, darkening toward the water colour only where
        # the view meets the surface at a grazing angle.
        b.next_column()
        # From the camera to this surface pixel. Its Y is positive exactly when looking up at the
        # surface from below, with no dependence on how the shader's V is oriented.
        to_surface = b.add("NormalizeNode", ins={"m_input": b.sub(b.add("WorldPositionNode"), b.add("CameraPositionNode"))})
        view_up = b.mask(to_surface, g=True)
        below = b.sat(b.mul(view_up, constant=50.0))
        grazing = b.add("OneMinusNode", ins={"m_input": b.sat(b.div(view_up, b.scalar("UndersideClearAngle", 0.35)))})
        underside = b.lerp(refracted_scene, b.vector("UndersideTint", (0.04, 0.22, 0.28, 1.0)),
                           b.mul(grazing, b.scalar("UndersideReflection", 0.55)))
        final = b.lerp(above_water, underside, below)

        root_inputs["Emissive Color"] = final
        opacity = b.sat(b.div(thickness, b.scalar("EdgeFadeDistance", 0.5)))
        if river:
            # Fade out over the real water depth at the bank (vertex colour red), so the edge is
            # soft from every view angle instead of a hard rim where the screen depth jumps.
            opacity = b.mul(opacity, b.sat(b.div(shore_depth, b.scalar("EdgeFadeDepth", 0.9))))
        root_inputs["Opacity"] = opacity
        root_inputs["Roughness"] = b.scalar("Roughness", 0.1)

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
    parser.add_argument("--variant", choices=("ocean", "river"), default="ocean")
    parser.add_argument("--target", default=None)
    args = parser.parse_args()
    if args.target is None:
        args.target = RIVER_TARGET if args.variant == "river" else DEFAULT_TARGET

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

        # Materials saved before a root property existed store fewer of them; add the rest.
        root_spec = next(entry for entry in catalog if entry["type_name"] == "MaterialNode")
        stored = {prop["name"] for prop in root["properties"]}
        for prop in root_spec["properties"]:
            if prop["name"] not in stored:
                root["properties"].append({"name": prop["name"], "type": prop["type"],
                                           "value": default_property("MaterialNode", prop)})

        for prop in root["properties"]:
            if prop["name"] in ROOT_WAVE_PROPERTIES:
                prop["value"] = ROOT_WAVE_PROPERTIES[prop["name"]]
            if args.variant == "river" and prop["name"] == "Vertex Waves":
                prop["value"] = False   # rivers do not swell

        for prop in root["properties"]:
            if prop["name"] in ("Lit", "Translucent", "Is Two Sided", "Receives Shadows"):
                prop["value"] = True
            elif prop["name"] in ("Casts Shadows", "Depth Write", "User Interface", "Masked"):
                prop["value"] = False
            elif prop["name"] == "Depth Test":
                prop["value"] = True

        builder = GraphBuilder(catalog, root)
        build_graph(builder, args.mode, args.variant)

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
