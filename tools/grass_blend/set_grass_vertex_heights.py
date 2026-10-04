# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Prepares grass blade meshes for GrassBlade.hmat.

For every submesh of the given .hmsh files (mesh format 0.3+, own vertex data):
  * the material is set to Models/FalwynPlains/Plants/GrassBlade.hmat,
  * every vertex colour becomes white with alpha = blade height (0 at the lowest vertex of the
    mesh, 1 at the highest).

The foliage system multiplies the vertex colour by the per-instance tint (the painted terrain
colour, alpha 1), so the material reads the ground colour from rgb and the height from alpha.
Everything else in the file is copied byte for byte.

Usage:
  python set_grass_vertex_heights.py <mesh.hmsh> [<mesh.hmsh> ...]
"""

import struct
import sys

MATERIAL = "Models/FalwynPlains/Plants/GrassBlade.hmat"
VERTEX_SIZE = 64  # position 3f, color u32, uvw 3f, normal 3f, binormal 3f, tangent 3f
COLOR_OFFSET = 12


def read_chunks(data):
    chunks = []
    offset = 0
    while offset + 8 <= len(data):
        magic = data[offset:offset + 4]
        size = struct.unpack_from("<I", data, offset + 4)[0]
        chunks.append((magic, data[offset + 8:offset + 8 + size]))
        offset += 8 + size
    if offset != len(data):
        raise ValueError("trailing bytes after the last chunk")
    return chunks


def parse_submesh(payload):
    """Returns (name bytes, material, offset of the vertex block, vertex count)."""
    offset = 0
    name_length = payload[offset]
    name = payload[offset + 1:offset + 1 + name_length]
    offset += 1 + name_length
    material_length = struct.unpack_from("<H", payload, offset)[0]
    material = payload[offset + 2:offset + 2 + material_length].decode("utf-8")
    offset += 2 + material_length
    shared_vertices = payload[offset]
    if shared_vertices:
        raise ValueError("submesh uses shared vertices; not supported")
    offset += 2  # useSharedVertices, visibleByDefault (mesh format 0.3.1+)
    vertex_count = struct.unpack_from("<I", payload, offset)[0]
    offset += 4
    return name, material, offset, vertex_count


def rewrite(path):
    data = open(path, "rb").read()
    chunks = read_chunks(data)

    submeshes = []
    for magic, payload in chunks:
        if magic == b"SUBM":
            submeshes.append(parse_submesh(payload))

    heights = []
    for (magic, payload), sub in zip([c for c in chunks if c[0] == b"SUBM"], submeshes):
        _, _, vertex_offset, vertex_count = sub
        for i in range(vertex_count):
            heights.append(struct.unpack_from("<f", payload, vertex_offset + i * VERTEX_SIZE + 4)[0])
    low, high = min(heights), max(heights)
    span = max(high - low, 1e-6)

    out = bytearray()
    for magic, payload in chunks:
        if magic == b"SUBM":
            name, material, vertex_offset, vertex_count = parse_submesh(payload)
            material_bytes = MATERIAL.encode("utf-8")
            header = bytes([len(name)]) + name + struct.pack("<H", len(material_bytes)) + material_bytes
            old_header_size = 1 + len(name) + 2 + len(material.encode("utf-8"))
            body = bytearray(payload[old_header_size:])
            body_vertex_offset = vertex_offset - old_header_size
            for i in range(vertex_count):
                base = body_vertex_offset + i * VERTEX_SIZE
                y = struct.unpack_from("<f", body, base + 4)[0]
                alpha = round(min(max((y - low) / span, 0.0), 1.0) * 255.0)
                struct.pack_into("<I", body, base + COLOR_OFFSET, (alpha << 24) | 0x00FFFFFF)
            payload = header + bytes(body)
        out += magic + struct.pack("<I", len(payload)) + payload

    open(path, "wb").write(bytes(out))
    print(f"{path}: {len(heights)} vertices, height {low:.3f}..{high:.3f}, material {MATERIAL}")


def main():
    for path in sys.argv[1:]:
        rewrite(path)


if __name__ == "__main__":
    main()
