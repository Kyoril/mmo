# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Maps (terrain material, splat layer index) to a terrain kind such as road or grass.

The table is hand-authored in terrain_kinds.json after looking at layer previews
(`python -m worldkit layers`), because which layer a material uses for paths is an art decision
that no file states.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np

KINDS = ("grass", "dirt", "path", "road", "rock", "sand", "mud", "forest_floor", "snow", "unknown")
ROAD_KINDS = frozenset({"path", "road"})
DEFAULT_KINDS_PATH = Path(__file__).with_name("terrain_kinds.json")


@dataclass
class TerrainKinds:
    table: dict[str, list[str]]

    def kind(self, material: str, layer: int) -> str:
        layers = self.table.get(material)
        return layers[layer] if layers else "unknown"

    def road_mask(self, snapshot) -> np.ndarray:
        """(Z, X) bool: cells whose dominant layer is a path or road."""
        mask = np.zeros(snapshot.layer.shape, bool)
        for index in [-1, *range(len(snapshot.materials))]:
            layers = self.table.get(snapshot.material_name(index))
            if not layers:
                continue
            in_material = snapshot.material == index
            for layer_index, kind in enumerate(layers):
                if kind in ROAD_KINDS:
                    mask |= in_material & (snapshot.layer == layer_index)
        return mask


def load_kinds(path: Path = DEFAULT_KINDS_PATH) -> TerrainKinds:
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    table = data.get("materials", {})
    for material, layers in table.items():
        if len(layers) != 4 or any(kind not in KINDS for kind in layers):
            raise ValueError(f"{path}: '{material}' must list 4 kinds from {KINDS}, got {layers}")
    return TerrainKinds(table)
