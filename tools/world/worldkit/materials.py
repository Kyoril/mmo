# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Base-colour texture of a mesh's material, for the offline renderer.

A submesh names a material instance (.hmi) or material (.hmat). Its base colour is the first texture
parameter whose name says so (BaseColor, Diffuse, Albedo, ...), searched in the instance's overrides
first and then up its PRNT chain, falling back to the first directly sampled texture (TEXT). Parsing
reuses the material skill's tools (material_tool.parse_material, htex_tool.read_htex/decode_mip).
Anything unresolvable yields None; the renderer then shades the mesh plainly.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np

from .paths import REPO, material_scripts

_NAME_PRIORITY = ("basecolor", "diffuse", "albedo", "albedo01", "texture", "color")
_NAME_HINTS = ("basecolor", "albedo", "diffuse", "color")
_MAX_PARENTS = 8


def _tools():
    folder = str(material_scripts(REPO))
    if folder not in sys.path:
        sys.path.insert(0, folder)
    import htex_tool      # noqa: E402  (tracked skill script)
    import material_tool  # noqa: E402
    return material_tool, htex_tool


def _norm(path) -> str | None:
    return str(path).replace("\\", "/") if path else None


def resolve_base_texture(material: str, client: Path, parse=None, exists=None) -> str | None:
    if parse is None:
        material_tool, _ = _tools()
        parse = material_tool.parse_material
        errors = (material_tool.FormatError, ValueError, OSError, struct.error)
    else:
        errors = (ValueError, OSError, KeyError)
    exists = exists or (lambda path: path.is_file())
    params: list[dict] = []
    direct: list[str] = []
    current, seen = _norm(material), set()
    while current and current not in seen and len(seen) < _MAX_PARENTS:
        seen.add(current)
        path = Path(client) / current
        if not exists(path):
            break
        try:
            info, _, _ = parse(path, None)
        except errors:
            break
        params += info.get("texture_parameters") or []
        direct += [t for t in (info.get("textures") or []) if t]
        current = _norm(info.get("parent"))
    by_name: dict[str, str] = {}
    for param in params:
        by_name.setdefault(str(param.get("name", "")).lower(), _norm(param.get("texture")))
    for name in _NAME_PRIORITY:
        if by_name.get(name):
            return by_name[name]
    for name, texture in by_name.items():
        if texture and any(hint in name for hint in _NAME_HINTS):
            return texture
    return _norm(direct[0]) if direct else None


def load_texture(texture: str, client: Path, max_size: int = 256) -> np.ndarray | None:
    path = Path(client) / texture
    if not path.is_file():
        return None
    _, htex_tool = _tools()
    try:
        info = htex_tool.read_htex(path)
        mips = sorted(info["mips"], key=lambda m: m["index"])
        if not mips:
            return None
        pick = next((m for m in mips if max(m["width"], m["height"]) <= max_size), mips[-1])
        image = htex_tool.decode_mip(info, pick["index"])
    except (ValueError, OSError, struct.error, KeyError):
        return None
    return np.asarray(image.convert("RGBA"), dtype=np.uint8)


class TextureCache:
    """Material -> decoded base texture, resolved once per process."""

    def __init__(self, client: Path, max_size: int = 256):
        self._client = Path(client)
        self._max_size = max_size
        self._by_material: dict[str, np.ndarray | None] = {}
        self._by_texture: dict[str, np.ndarray | None] = {}

    def texture_name(self, material: str) -> str | None:
        return resolve_base_texture(material, self._client)

    def for_material(self, material: str) -> np.ndarray | None:
        if material not in self._by_material:
            name = self.texture_name(material)
            if name and name not in self._by_texture:
                self._by_texture[name] = load_texture(name, self._client, self._max_size)
            self._by_material[material] = self._by_texture.get(name) if name else None
        return self._by_material[material]
