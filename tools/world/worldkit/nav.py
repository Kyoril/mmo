# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Walkability: navmesh builds (nav_builder) and path / on-mesh queries (nav_query).

Checks run on a scratch build in generated/world/nav; the real navmesh in data/editor/nav is only
rebuilt by `dress.py publish-nav`. Protected routes (tools/world/nav_routes.json) must stay walkable,
each dressed site must be reachable from the route network, and spawns near a site must stay on the mesh.
"""

from __future__ import annotations

import json
import math
import os
import subprocess
from dataclasses import dataclass
from pathlib import Path

from .lint import Violation
from .paths import REPO, client_root

ROUTES_PATH = REPO / "tools" / "world" / "nav_routes.json"
LONGER_FACTOR = 1.25
DETOUR_FACTOR = 3.0
SPAWN_OFF_MESH = 1.5
_NETWORK_STEP = 10.0


class NavError(RuntimeError):
    pass


def tool_exe(name: str, repo: Path = REPO) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    for config in ("Release", "Debug"):
        path = repo / "bin" / config / f"{name}{suffix}"
        if path.is_file():
            return path
    raise NavError(f"{name}{suffix} not found in bin/Release or bin/Debug: build it with MMO_BUILD_TOOLS=ON")


def scratch_nav_root(repo: Path = REPO) -> Path:
    return repo / "generated" / "world"


def build_nav(directory: str, out_root: Path, repo: Path = REPO, runner=subprocess.run) -> Path:
    """Runs nav_builder for a whole world; outputs land in <out_root>/nav/<World>/ and <out_root>/nav/<World>.map."""
    exe = tool_exe("nav_builder", repo)
    out_root.mkdir(parents=True, exist_ok=True)
    proc = runner([str(exe), "-d", str(client_root(repo)), "-w", directory, "-o", str(out_root)],
                  capture_output=True, text=True)
    if proc.returncode != 0:
        raise NavError(f"nav_builder failed ({proc.returncode}): {(proc.stdout + proc.stderr)[-1500:]}")
    return out_root / "nav"


class NavQuery:
    def __init__(self, nav_dir: Path, world: str, repo: Path = REPO, popen=subprocess.Popen, exe: Path | None = None):
        exe = exe or tool_exe("nav_query", repo)
        self._proc = popen([str(exe), "--nav", str(nav_dir), "--world", world], stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1)
        ready = self._read()
        if not ready.get("ready"):
            raise NavError(f"nav_query did not start for {world} in {nav_dir}")

    def _read(self) -> dict:
        line = self._proc.stdout.readline()
        if not line:
            raise NavError("nav_query exited (is the navmesh built?)")
        return json.loads(line)

    def _ask(self, request: dict) -> dict:
        self._proc.stdin.write(json.dumps(request) + "\n")
        self._proc.stdin.flush()
        return self._read()

    def path(self, a, b) -> float | None:
        answer = self._ask({"op": "path", "from": [float(v) for v in a], "to": [float(v) for v in b]})
        return float(answer["length"]) if answer.get("ok") else None

    def on_mesh(self, point, radius: float = 2.0) -> float | None:
        answer = self._ask({"op": "on_mesh", "at": [float(v) for v in point], "radius": radius})
        return float(answer["distance"]) if answer.get("ok") else None

    def close(self) -> None:
        self._proc.stdin.close()
        self._proc.wait(timeout=10)
        self._proc.stdout.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


@dataclass
class Route:
    id: str
    name: str
    points: list            # [[x, z], ...]
    baseline_length: float | None


def load_routes(path: Path = ROUTES_PATH) -> list[Route]:
    doc = json.loads(Path(path).read_text(encoding="utf-8"))
    return [Route(r["id"], r["name"], r["points"], r.get("baseline_length")) for r in doc["routes"]]


def save_routes(routes: list[Route], path: Path = ROUTES_PATH) -> None:
    doc = {"version": 1, "routes": [{"id": r.id, "name": r.name, "points": r.points,
                                     "baseline_length": None if r.baseline_length is None else round(r.baseline_length, 2)}
                                    for r in routes]}
    Path(path).write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")


def _point(query, x: float, z: float) -> tuple[float, float, float]:
    return (x, (query.height_at(x, z) or 0.0) + 0.5, z)


def route_length(nav, query, points) -> float | None:
    total = 0.0
    for (ax, az), (bx, bz) in zip(points, points[1:]):
        leg = nav.path(_point(query, ax, az), _point(query, bx, bz))
        if leg is None:
            return None
        total += leg
    return total


def _network(routes: list[Route]) -> list[tuple[float, float]]:
    out = []
    for route in routes:
        for (ax, az), (bx, bz) in zip(route.points, route.points[1:]):
            steps = max(1, int(math.hypot(bx - ax, bz - az) // _NETWORK_STEP))
            out += [(ax + (bx - ax) * k / steps, az + (bz - az) * k / steps) for k in range(steps + 1)]
    return out


def walkability_violations(nav, query, routes: list[Route], sites: list[dict], spawns) -> list[Violation]:
    found: list[Violation] = []
    walkable: list[Route] = []
    for route in routes:
        length = route_length(nav, query, route.points)
        start = route.points[0]
        if length is not None:
            walkable.append(route)
        if length is None:
            found.append(Violation("route_broken", "error", f"route:{route.id}", f"{route.name}: no path any more", *start))
        elif route.baseline_length and length > route.baseline_length * LONGER_FACTOR:
            found.append(Violation("route_longer", "warning", f"route:{route.id}",
                                   f"{route.name}: {length:.0f} m, was {route.baseline_length:.0f} m", *start))
    # A broken route is already reported above and would only anchor sites on the wrong side of the break.
    network = _network(walkable)
    for site in sites:
        sx, sz = site["x"], site["z"]
        if network:
            nx, nz = min(network, key=lambda p: math.hypot(p[0] - sx, p[1] - sz))
            straight = max(math.hypot(nx - sx, nz - sz), 1.0)
            length = nav.path(_point(query, nx, nz), _point(query, sx, sz))
            if length is None or length > straight * DETOUR_FACTOR:
                text = "no path" if length is None else f"path {length:.0f} m for {straight:.0f} m straight"
                found.append(Violation("site_unreachable", "error", f"site:{site['name']}",
                                       f"{site['name']}: not reachable from the route network ({text})", sx, sz))
        for record in spawns:
            if record.active and math.hypot(record.x - sx, record.z - sz) <= site["radius"]:
                distance = nav.on_mesh((record.x, record.y, record.z), 2.0)
                if distance is None or distance > SPAWN_OFF_MESH:
                    found.append(Violation("spawn_off_mesh", "error", record.key,
                                           f"{record.name or record.key} is off the navmesh near {site['name']}", record.x, record.z))
    return found
