# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Dump a ``.hpar`` particle system as readable text, or as a ready-to-edit Python recipe.

    python tools/particle_gen/inspect_hpar.py data/client/Particles/ResetTalents.hpar
    python tools/particle_gen/inspect_hpar.py Sparkles.hpar --as-recipe > recipes/my_effect.py

``--as-recipe`` is the fastest way to start a new effect from one that already looks good:
it prints a script that rebuilds the file exactly, which you then retune.
"""

from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import hpar  # noqa: E402


def _fmt(v):
    if isinstance(v, float):
        return "%.4g" % v
    if isinstance(v, (tuple, list)):
        return "(" + ", ".join("%.4g" % c for c in v) + ")"
    return repr(v)


def dump_text(system: hpar.ParticleSystem) -> str:
    lines = ["%d emitter(s)" % len(system.emitters)]
    for index, e in enumerate(system.emitters):
        lines.append("")
        lines.append("--- emitter %d: %s%s" % (index, e.name, "" if e.enabled else "  [DISABLED]"))
        lines.append("  space          %s   loop=%s duration=%.3gs delay=%.3gs warmup=%.3gs" % (
            hpar.SIM_NAMES.get(e.simulation_space, e.simulation_space), e.loop,
            e.duration, e.start_delay, e.warmup_time))
        lines.append("  emission       rate=%.4g/s max=%d bursts=%s" % (
            e.spawn_rate, e.max_particles,
            [(round(b.time, 4), b.count) for b in e.bursts] or "none"))
        lines.append("  shape          %s extents=%s" % (
            hpar.SHAPE_NAMES.get(e.shape, e.shape), _fmt(e.shape_extents)))
        lines.append("  lifetime       %.3g .. %.3g s" % (e.min_lifetime, e.max_lifetime))
        lines.append("  velocity       %s .. %s  startSpeed=%.3g..%.3g" % (
            _fmt(e.min_velocity), _fmt(e.max_velocity), e.min_start_speed, e.max_start_speed))
        lines.append("  size           %.3g .. %.3g" % (e.min_start_size, e.max_start_size))
        lines.append("  rotation       %.3g..%.3g rad  angVel=%.3g..%.3g rad/s" % (
            e.min_start_rotation, e.max_start_rotation, e.min_angular_velocity, e.max_angular_velocity))
        lines.append("  forces         gravity=%s drag=%.3g orbital=%.3g radial=%.3g" % (
            _fmt(e.gravity), e.drag, e.orbital_speed, e.radial_acceleration))
        if e.attractor_strength or e.noise_amplitude:
            lines.append("                 attractor=%s@%.3g noise=%.3g@%.3g" % (
                _fmt(e.attractor_position), e.attractor_strength, e.noise_amplitude, e.noise_frequency))
        lines.append("  render         %s lengthScale=%.3g material='%s'%s" % (
            hpar.RENDER_NAMES.get(e.render_mode, e.render_mode), e.length_scale, e.material_name,
            (" mesh='%s'" % e.mesh_name) if e.mesh_name else ""))
        if e.sprite_sheet_columns > 1 or e.sprite_sheet_rows > 1:
            lines.append("  sprite sheet   %dx%d %s fps=%.3g" % (
                e.sprite_sheet_columns, e.sprite_sheet_rows,
                hpar.SPRITE_NAMES.get(e.sprite_animation, e.sprite_animation), e.sprite_animation_fps))
        lines.append("  sizeOverLife   " + " ".join("%.3g:%.3g" % (k.time, k.value) for k in e.size_over_life))
        lines.append("  colorOverLife  " + "  ".join(
            "%.3g:(%.2f,%.2f,%.2f,a%.2f)" % (k.time, k.color[0], k.color[1], k.color[2], k.color[3])
            for k in e.color_over_lifetime))
    return "\n".join(lines)


def dump_recipe(system: hpar.ParticleSystem, source: str) -> str:
    out = [
        "# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.",
        '"""Recipe generated from %s by tools/particle_gen/inspect_hpar.py."""' % source,
        "",
        "import os",
        "import sys",
        "",
        'sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))',
        "",
        "import hpar",
        "from hpar import Burst, ColorKey, Emitter, FloatKey, ParticleSystem",
        "",
        "",
        "def build():",
        "    return ParticleSystem(emitters=[",
    ]
    for e in system.emitters:
        out.append("        Emitter(")
        out.append("            name=%r," % e.name)
        out.append("            enabled=%r, simulation_space=%d, loop=%r," % (e.enabled, e.simulation_space, e.loop))
        out.append("            duration=%r, start_delay=%r, warmup_time=%r, inherit_velocity=%r," % (
            e.duration, e.start_delay, e.warmup_time, e.inherit_velocity))
        out.append("            spawn_rate=%r, max_particles=%d," % (e.spawn_rate, e.max_particles))
        out.append("            bursts=[%s]," % ", ".join("Burst(%r, %d)" % (b.time, b.count) for b in e.bursts))
        out.append("            shape=%d, shape_extents=%r," % (e.shape, tuple(e.shape_extents)))
        out.append("            min_lifetime=%r, max_lifetime=%r," % (e.min_lifetime, e.max_lifetime))
        out.append("            min_velocity=%r, max_velocity=%r," % (tuple(e.min_velocity), tuple(e.max_velocity)))
        out.append("            min_start_speed=%r, max_start_speed=%r," % (e.min_start_speed, e.max_start_speed))
        out.append("            min_start_size=%r, max_start_size=%r," % (e.min_start_size, e.max_start_size))
        out.append("            min_start_rotation=%r, max_start_rotation=%r," % (e.min_start_rotation, e.max_start_rotation))
        out.append("            min_angular_velocity=%r, max_angular_velocity=%r," % (e.min_angular_velocity, e.max_angular_velocity))
        out.append("            gravity=%r, drag=%r, orbital_speed=%r, radial_acceleration=%r," % (
            tuple(e.gravity), e.drag, e.orbital_speed, e.radial_acceleration))
        out.append("            attractor_position=%r, attractor_strength=%r," % (tuple(e.attractor_position), e.attractor_strength))
        out.append("            noise_amplitude=%r, noise_frequency=%r," % (e.noise_amplitude, e.noise_frequency))
        out.append("            sprite_sheet_columns=%d, sprite_sheet_rows=%d, sprite_animation=%d, sprite_animation_fps=%r," % (
            e.sprite_sheet_columns, e.sprite_sheet_rows, e.sprite_animation, e.sprite_animation_fps))
        out.append("            render_mode=%d, length_scale=%r," % (e.render_mode, e.length_scale))
        out.append("            material_name=%r, mesh_name=%r," % (e.material_name, e.mesh_name))
        out.append("            size_over_life=[%s]," % ", ".join(
            "FloatKey(%r, %r, %r, %r, %d)" % (k.time, k.value, k.in_tangent, k.out_tangent, k.tangent_mode)
            for k in e.size_over_life))
        out.append("            color_over_lifetime=[%s]," % ", ".join(
            "ColorKey(%r, %r, %r, %r, %d)" % (k.time, tuple(k.color), tuple(k.in_tangent), tuple(k.out_tangent), k.tangent_mode)
            for k in e.color_over_lifetime))
        out.append("        ),")
    out += [
        "    ])",
        "",
        "",
        'if __name__ == "__main__":',
        '    out = sys.argv[1] if len(sys.argv) > 1 else "out.hpar"',
        "    hpar.save(build(), out)",
        '    print("wrote %s" % out)',
        "",
    ]
    return "\n".join(out)


_MATERIAL_TYPES = ("Opaque", "Unlit", "Masked", "Translucent", "UserInterface")


def _material_type(material_name, data_root):
    """Read a .hmat/.hmi ATTR chunk and return (type_name, depth_write) or (None, None)."""
    import struct

    path = os.path.join(data_root, (material_name or "").replace("/", os.sep))
    try:
        data = open(path, "rb").read()
    except OSError:
        return None, None
    pos = 0
    while pos + 8 <= len(data):
        magic = data[pos:pos + 4]
        size = struct.unpack("<I", data[pos + 4:pos + 8])[0]
        if magic == b"ATTR" and size in (4, 6):
            payload = data[pos + 8:pos + 8 + size]
            type_id = payload[3]
            name = _MATERIAL_TYPES[type_id] if type_id < len(_MATERIAL_TYPES) else "?"
            return name, (bool(payload[4]) if size == 6 else None)
        pos += 8 + size
    return None, None


def check(path: str, one_shot: bool, data_root: str):
    """Report the mistakes that are invisible in a preview. Returns a list of problems."""
    import struct

    data = open(path, "rb").read()
    problems = []

    # Chunk table must consume the file exactly -- the engine seeks to each declared chunk
    # end, so a size that is off by even one byte silently misreads everything after it.
    pos = 0
    while pos + 8 <= len(data):
        pos += 8 + struct.unpack("<I", data[pos + 4:pos + 8])[0]
    if pos != len(data):
        problems.append("chunk table ends at %d but the file is %d bytes" % (pos, len(data)))

    system = hpar.loads(data)
    if hpar.dumps(system) != data:
        # Not a defect on its own: legacy v1.0 files upgrade to v2.0 on write, and v2.0 files
        # predating the trailing mesh_name field gain its 2 empty bytes. Worth reporting only
        # so a genuine layout mismatch is not mistaken for one of those.
        print("note: does not round-trip byte-identically (expected for legacy v1.0 files and "
              "for v2.0 files predating the mesh_name field)")

    for e in system.emitters:
        demand = e.spawn_rate * e.max_lifetime + sum(b.count for b in e.bursts)
        if demand > e.max_particles:
            problems.append("%s: demand %.0f exceeds max_particles %d -- the emitter will stop "
                            "spawning partway through" % (e.name, demand, e.max_particles))
        if e.color_over_lifetime and e.color_over_lifetime[-1].color[3] != 0.0:
            problems.append("%s: colour curve ends at alpha %.2f, so particles pop out instead "
                            "of fading" % (e.name, e.color_over_lifetime[-1].color[3]))
        if one_shot and e.loop:
            problems.append("%s: loops, so the system never reports finished and a one-shot "
                            "effect would leak" % e.name)
        if not e.material_name and e.render_mode != hpar.RENDER_MESH:
            problems.append("%s: no material set" % e.name)
        else:
            type_name, depth_write = _material_type(e.material_name, data_root)
            if type_name is None:
                problems.append("%s: material '%s' not found under %s"
                                % (e.name, e.material_name, data_root))
            elif type_name not in ("Translucent", "UserInterface"):
                problems.append("%s: material '%s' is typed %s, so the engine renders it with "
                                "OPAQUE blending -- particles will be hard occluding quads"
                                % (e.name, e.material_name, type_name))
            elif depth_write:
                problems.append("%s: material '%s' has depth-write on; translucent particles "
                                "will cull each other" % (e.name, e.material_name))

    if one_shot:
        ends = [e.start_delay + e.duration + e.max_lifetime for e in system.emitters]
        print("effect ends at %.2fs (longest emitter: %s)" %
              (max(ends), system.emitters[ends.index(max(ends))].name))

    return problems


def main():
    ap = argparse.ArgumentParser(description="Dump a .hpar particle system.")
    ap.add_argument("input")
    ap.add_argument("--as-recipe", action="store_true",
                    help="print an editable Python recipe that rebuilds the file")
    ap.add_argument("--check", action="store_true",
                    help="validate structure, particle budgets and fade-out instead of dumping")
    ap.add_argument("--one-shot", action="store_true",
                    help="with --check, also require every emitter to be non-looping")
    ap.add_argument("--data-root", default=os.path.join(
                        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                        "data", "client"),
                    help="client data root used to resolve materials")
    args = ap.parse_args()

    if args.check:
        problems = check(args.input, args.one_shot, args.data_root)
        for problem in problems:
            print("PROBLEM: " + problem)
        print("%d problem(s)" % len(problems))
        sys.exit(1 if problems else 0)

    system = hpar.load(args.input)
    print(dump_recipe(system, args.input) if args.as_recipe else dump_text(system))


if __name__ == "__main__":
    main()
