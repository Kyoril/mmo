#!/usr/bin/env python3
"""Check that a terrain material's height blend is wired correctly.

The terrain material blends its four splat layers with the height based formula WoW
introduced in Legion, plus a `sharpness` factor:

    pct = coverage * (height * heightScale + heightOffset)
    pct = pct * (1 - clamp((max(pct) - pct) * sharpness, 0, 1))
    pct = pct / sum(pct)

The sharpness factor is what makes the feature safe to enable on an existing world: at
sharpness 0 every factor in the chain is exactly 1.0 and the blend collapses to the plain
`coverage / sum(coverage)` normalize the shader performed before height blending existed.
Without it, WoW's formula is NOT the identity even with the height term disabled - it
quietly re-contrasts every blend, turning (0.6, 0.4) into (0.652, 0.348).

This script evaluates the material's actual node graph on the CPU and checks that
property holds, so a bad rewire is caught without a shader compile, a GPU, or a
screenshot comparison. Run it after anyone edits the terrain material's graph.

    python tools/terrain_blend_check.py data/client/Models/Terrain/Oakenshire_BoarTerrain.hmat

The maths itself is pinned separately by src/tests/terrain_tests/test_terrain_layer_blend.cpp
against src/shared/terrain/terrain_layer_blend.h; this script only checks the wiring.

Known blind spots, so nobody reads a pass as more than it is:
  - MaskNode forwards its input and ignores which channel is selected, so a wrong channel
    mask is invisible here.
  - DivideNode returns 0.0 on a zero divisor rather than producing NaN like the shader, so
    division hazards are invisible here too.
  - Evaluation is in Python doubles. At neutral settings both sides evaluate the identical
    expression tree with no rounding, so exact equality is meaningful; it would not catch a
    float-precision-only divergence in a non-neutral configuration.
"""
import argparse
import json
import random
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
MATERIAL_TOOL = REPO_ROOT / ".claude" / "skills" / "mmo-material-editor" / "scripts" / "material_tool.py"
WEIGHT_VARIABLES = ["Layer 1 Weight", "Layer 2 Weight", "Layer 3 Weight", "Layer 4 Weight"]
SPLAT_PARAM = "Splatting"
SPLAT_CHANNELS = ["R", "G", "B", "A"]
WHITE = 1.0  # the neutral height texture the layers default to


def export_graph(material: Path) -> dict:
    if not MATERIAL_TOOL.exists():
        sys.exit("material_tool.py not found at {}".format(MATERIAL_TOOL))

    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "material.json"
        result = subprocess.run(
            [sys.executable, str(MATERIAL_TOOL), "export-json", str(material), "--output", str(out)],
            capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit("export failed:\n{}{}".format(result.stdout, result.stderr))
        return json.loads(out.read_text())


class Graph:
    def __init__(self, doc):
        self.nodes = {n["id"]: n for n in doc["graph"]["nodes"]}
        self.producer = {p["id"]: n for n in self.nodes.values() for p in n.get("outputs", [])}
        self.inputs = {p["id"]: p for n in self.nodes.values() for p in n.get("inputs", [])}

        self.splat_pins = {}
        for n in self.nodes.values():
            if n["type_name"] == "TextureParameterNode" and self.prop(n, "Name") == SPLAT_PARAM:
                for p in n["outputs"]:
                    if p["name"] in SPLAT_CHANNELS:
                        self.splat_pins[p["id"]] = SPLAT_CHANNELS.index(p["name"])
        if len(self.splat_pins) != 4:
            sys.exit("could not find the four {} channel pins".format(SPLAT_PARAM))

        self.weight_sources = []
        for name in WEIGHT_VARIABLES:
            node = next((n for n in self.nodes.values()
                         if n["type_name"] == "NamedVariableSetNode" and self.prop(n, "Name") == name), None)
            if node is None:
                sys.exit("material has no '{}' variable".format(name))
            link = node["inputs"][0].get("link")
            if link is None:
                sys.exit("'{}' is not connected".format(name))
            self.weight_sources.append(link)

    @staticmethod
    def prop(node, name, default=None):
        for p in node.get("properties", []):
            if p["name"] == name:
                return p["value"]
        return default

    def evaluate(self, pin_id, coverage, params, heights):
        if pin_id in self.splat_pins:
            return coverage[self.splat_pins[pin_id]]

        node = self.producer.get(pin_id)
        if node is None:
            sys.exit("no node produces pin {}".format(pin_id))

        kind = node["type_name"]
        if kind == "ScalarParameterNode":
            return params.get(self.prop(node, "Name"), self.prop(node, "Value", 0.0))
        if kind == "TextureParameterNode":
            return heights.get(self.prop(node, "Name"), WHITE)

        def operand(pin_name, fallback):
            pin = next(x for x in node["inputs"] if x["name"] == pin_name)
            if pin.get("link") is None:
                return self.prop(node, fallback, 0.0)
            return self.evaluate(pin["link"], coverage, params, heights)

        if kind in ("AddNode", "SubtractNode", "MultiplyNode", "DivideNode", "MaxNode", "MinNode"):
            a = operand("A", "Value 1")
            b = operand("B", "Value 2")
            if kind == "AddNode":
                return a + b
            if kind == "SubtractNode":
                return a - b
            if kind == "MultiplyNode":
                return a * b
            if kind == "MaxNode":
                return max(a, b)
            if kind == "MinNode":
                return min(a, b)
            return a / b if b != 0.0 else 0.0
        if kind == "SaturateNode":
            return min(1.0, max(0.0, operand("m_input", "Value")))
        if kind == "OneMinusNode":
            return 1.0 - operand("m_input", "Value")
        if kind == "MaskNode":
            return operand("m_input", "Value")

        sys.exit("unhandled node type {} (#{}) in the blend path".format(kind, node["id"]))

    def blend(self, coverage, params, heights=None):
        heights = heights or {}
        return [self.evaluate(src, coverage, params, heights) for src in self.weight_sources]


def scalar_names(graph, needle):
    return [graph.prop(n, "Name") for n in graph.nodes.values()
            if n["type_name"] == "ScalarParameterNode" and needle in (graph.prop(n, "Name") or "").lower()]


def plain_normalize(coverage):
    total = 0.0
    for value in coverage:
        total += value
    if total <= 0.0:
        return list(coverage)
    return [value / total for value in coverage]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("material", type=Path, help="Path to the terrain .hmat")
    parser.add_argument("--cases", type=int, default=4000, help="Random coverage vectors to test")
    args = parser.parse_args()

    graph = Graph(export_graph(args.material))

    neutral = {}
    for name in scalar_names(graph, "heightscale"):
        neutral[name] = 0.0
    for name in scalar_names(graph, "heightoffset"):
        neutral[name] = 1.0
    sharpness_params = scalar_names(graph, "sharpness")
    for name in sharpness_params:
        neutral[name] = 0.0

    if not sharpness_params:
        sys.exit("FAIL: the material declares no sharpness parameter, so the blend cannot be\n"
                 "      made neutral and enabling it would change every existing world.")

    print("{}: {} nodes".format(args.material, len(graph.nodes)))
    print("  height scale params : {}".format(sorted(scalar_names(graph, 'heightscale'))))
    print("  height offset params: {}".format(sorted(scalar_names(graph, 'heightoffset'))))
    print("  sharpness param     : {}".format(sharpness_params))

    cases = [[0.6, 0.4, 0.0, 0.0], [0.75, 0.25, 0.0, 0.0], [0.25, 0.25, 0.25, 0.25],
             [1.0, 0.0, 0.0, 0.0], [0.37, 0.11, 0.29, 0.83]]
    rng = random.Random(20260904)
    cases += [[rng.random() for _ in range(4)] for _ in range(args.cases)]

    failures = 0
    for coverage in cases:
        if sum(coverage) <= 0.0:
            continue
        got = graph.blend(coverage, neutral)
        want = plain_normalize(coverage)
        if got != want:
            failures += 1
            if failures <= 3:
                print("  MISMATCH coverage={} got={} want={}".format(coverage, got, want))

    if failures:
        print("\nFAIL: {} of {} coverage vectors differ from the plain normalize at neutral\n"
              "      settings. Enabling this material would change existing terrain."
              .format(failures, len(cases)))
        return 1

    print("\nOK: {} coverage vectors reproduce the plain coverage normalize exactly at\n"
          "    neutral settings, so the blend is safe to ship disabled.".format(len(cases)))

    sharp = dict(neutral)
    for name in sharpness_params:
        sharp[name] = 1.0
    got = graph.blend([0.6, 0.4, 0.0, 0.0], sharp)
    print("    sharpness 1 turns (0.6, 0.4) into ({:.4f}, {:.4f}); Legion gives (0.6522, 0.3478)"
          .format(got[0], got[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
