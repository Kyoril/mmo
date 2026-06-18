#!/usr/bin/env python3
"""Inspect, export, validate, and safely rewrite MMO HMAT/HMI material files."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


MATERIAL_TYPES = ["Opaque", "Unlit", "Masked", "Translucent", "UserInterface"]
VERTEX_SHADER_TYPES = ["Default", "SkinnedLow", "SkinnedMedium", "SkinnedHigh", "UI", "Instanced"]
PIXEL_SHADER_TYPES = ["Forward", "GBuffer", "ShadowMap", "UI"]


class FormatError(ValueError):
    pass


@dataclass
class Cursor:
    data: bytes
    offset: int = 0

    def take(self, size: int) -> bytes:
        end = self.offset + size
        if end > len(self.data):
            raise FormatError(f"Read past end at {self.offset}: need {size}, have {len(self.data) - self.offset}")
        value = self.data[self.offset:end]
        self.offset = end
        return value

    def unpack(self, fmt: str) -> tuple:
        size = struct.calcsize(fmt)
        return struct.unpack(fmt, self.take(size))

    def u8(self) -> int:
        return self.unpack("<B")[0]

    def u16(self) -> int:
        return self.unpack("<H")[0]

    def u32(self) -> int:
        return self.unpack("<I")[0]

    def i32(self) -> int:
        return self.unpack("<i")[0]

    def f32(self) -> float:
        return self.unpack("<f")[0]

    def string(self, length_fmt: str) -> str:
        length = {"u8": self.u8, "u16": self.u16, "u32": self.u32}[length_fmt]()
        return self.take(length).decode("utf-8")


def pack_string(value: str, length_fmt: str) -> bytes:
    encoded = value.encode("utf-8")
    limits = {"u8": 0xFF, "u16": 0xFFFF, "u32": 0xFFFFFFFF}
    if len(encoded) > limits[length_fmt]:
        raise FormatError(f"String too long for {length_fmt}: {value!r}")
    prefix = {"u8": "<B", "u16": "<H", "u32": "<I"}[length_fmt]
    return struct.pack(prefix, len(encoded)) + encoded


def make_chunk(magic: str, payload: bytes) -> bytes:
    if len(magic) != 4:
        raise ValueError("Chunk magic must be four characters")
    return magic.encode("ascii") + struct.pack("<I", len(payload)) + payload


def read_chunks(data: bytes) -> list[dict]:
    cursor = Cursor(data)
    chunks = []
    while cursor.offset < len(data):
        start = cursor.offset
        if len(data) - start < 8:
            raise FormatError(f"Trailing {len(data) - start} bytes at offset {start}")
        magic_bytes = cursor.take(4)
        try:
            magic = magic_bytes.decode("ascii")
        except UnicodeDecodeError as error:
            raise FormatError(f"Invalid chunk magic at {start}: {magic_bytes!r}") from error
        size = cursor.u32()
        payload = cursor.take(size)
        chunks.append(
            {
                "magic": magic,
                "size": size,
                "offset": start,
                "payload": payload,
                "raw": data[start:cursor.offset],
            }
        )
    return chunks


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_attributes(payload: bytes) -> dict:
    if len(payload) not in (4, 6):
        raise FormatError(f"ATTR size must be 4 or 6, got {len(payload)}")
    values = list(payload)
    result = {
        "two_sided": bool(values[0]),
        "casts_shadows": bool(values[1]),
        "receives_shadows": bool(values[2]),
        "material_type_id": values[3],
        "material_type": MATERIAL_TYPES[values[3]] if values[3] < len(MATERIAL_TYPES) else f"Unknown ({values[3]})",
    }
    if len(values) == 6:
        result["depth_write"] = bool(values[4])
        result["depth_test"] = bool(values[5])
    return result


def encode_attributes(attributes: dict) -> bytes:
    material_type = attributes.get("material_type_id")
    if material_type is None:
        name = attributes.get("material_type", "Opaque")
        if name not in MATERIAL_TYPES:
            raise FormatError(f"Unknown material type {name!r}")
        material_type = MATERIAL_TYPES.index(name)
    return bytes(
        [
            int(bool(attributes.get("two_sided", False))),
            int(bool(attributes.get("casts_shadows", True))),
            int(bool(attributes.get("receives_shadows", True))),
            int(material_type),
            int(bool(attributes.get("depth_write", True))),
            int(bool(attributes.get("depth_test", True))),
        ]
    )


def parse_scalar_params(payload: bytes) -> list[dict]:
    cursor = Cursor(payload)
    count = cursor.u16()
    values = [{"name": cursor.string("u8"), "value": cursor.f32()} for _ in range(count)]
    ensure_consumed(cursor, "SPAR")
    return values


def encode_scalar_params(values: list[dict]) -> bytes:
    result = bytearray(struct.pack("<H", len(values)))
    for item in values:
        result += pack_string(item["name"], "u8")
        result += struct.pack("<f", float(item["value"]))
    return bytes(result)


def parse_vector_params(payload: bytes) -> list[dict]:
    cursor = Cursor(payload)
    count = cursor.u16()
    values = []
    for _ in range(count):
        values.append({"name": cursor.string("u8"), "value": list(cursor.unpack("<4f"))})
    ensure_consumed(cursor, "VPAR")
    return values


def encode_vector_params(values: list[dict]) -> bytes:
    result = bytearray(struct.pack("<H", len(values)))
    for item in values:
        vector = item["value"]
        if len(vector) != 4:
            raise FormatError(f"Vector parameter {item['name']!r} must have four components")
        result += pack_string(item["name"], "u8")
        result += struct.pack("<4f", *map(float, vector))
    return bytes(result)


def parse_texture_params(payload: bytes) -> list[dict]:
    cursor = Cursor(payload)
    count = cursor.u8()
    values = [{"name": cursor.string("u8"), "texture": cursor.string("u16")} for _ in range(count)]
    ensure_consumed(cursor, "TPAR")
    return values


def encode_texture_params(values: list[dict]) -> bytes:
    if len(values) > 255:
        raise FormatError("At most 255 texture parameters are supported")
    result = bytearray(struct.pack("<B", len(values)))
    for item in values:
        result += pack_string(item["name"], "u8")
        result += pack_string(item["texture"], "u16")
    return bytes(result)


def parse_textures(payload: bytes) -> list[str]:
    cursor = Cursor(payload)
    count = cursor.u8()
    values = [cursor.string("u8") for _ in range(count)]
    ensure_consumed(cursor, "TEXT")
    return values


def encode_textures(values: list[str]) -> bytes:
    if len(values) > 255:
        raise FormatError("At most 255 textures are supported")
    return struct.pack("<B", len(values)) + b"".join(pack_string(value, "u8") for value in values)


def parse_shader_summary(payload: bytes, version: int, stage: str) -> dict:
    cursor = Cursor(payload)
    type_names = VERTEX_SHADER_TYPES if stage == "vertex" else PIXEL_SHADER_TYPES
    entries: list[dict] = []
    if version >= 0x0500:
        platform_count = cursor.u8()
        for _ in range(platform_count):
            platform = cursor.string("u8")
            shader_count = cursor.u8()
            for _ in range(shader_count):
                type_id = cursor.u8()
                size = cursor.u32()
                bytecode = cursor.take(size)
                entries.append(
                    {
                        "platform": platform,
                        "type_id": type_id,
                        "type": type_names[type_id] if type_id < len(type_names) else f"Unknown ({type_id})",
                        "bytecode_size": size,
                        "bytecode_magic": bytecode[:4].decode("ascii", errors="replace"),
                        "sha256": sha256(bytecode),
                    }
                )
    else:
        shader_count = cursor.u8()
        for _ in range(shader_count):
            profile = cursor.string("u8")
            if stage == "vertex" and version >= 0x0300:
                type_id = cursor.u8()
            elif stage == "pixel" and version >= 0x0400:
                type_id = cursor.u8()
            else:
                type_id = 0
            size = cursor.u32()
            bytecode = cursor.take(size)
            entries.append(
                {
                    "platform": profile,
                    "type_id": type_id,
                    "type": type_names[type_id] if type_id < len(type_names) else f"Unknown ({type_id})",
                    "bytecode_size": size,
                    "bytecode_magic": bytecode[:4].decode("ascii", errors="replace"),
                    "sha256": sha256(bytecode),
                }
            )
    ensure_consumed(cursor, "shader")
    return {"stage": stage, "entries": entries}


def ensure_consumed(cursor: Cursor, label: str) -> None:
    if cursor.offset != len(cursor.data):
        raise FormatError(f"{label} parser left {len(cursor.data) - cursor.offset} unread bytes")


def fnv1a(value: str) -> int:
    result = 2166136261
    for byte in value.encode("utf-8"):
        result ^= byte
        result = (result * 16777619) & 0xFFFFFFFF
    return result


def class_blocks(source: str) -> list[tuple[str, str]]:
    blocks = []
    pattern = re.compile(r"class\s+(\w+)\s+final\s*:\s*public\s+GraphNode\s*\{")
    for match in pattern.finditer(source):
        depth = 1
        index = match.end()
        while index < len(source) and depth:
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
            index += 1
        blocks.append((match.group(1), source[match.start():index]))
    return blocks


def parse_quoted_label(text: str, fallback: str) -> str:
    match = re.search(r'"([^"]*)"', text)
    return match.group(1) if match else fallback


def build_node_catalog(source_path: Path) -> dict[int, dict]:
    source = source_path.read_text(encoding="utf-8")
    catalog: dict[int, dict] = {}
    for class_name, block in class_blocks(source):
        node_match = re.search(r'MAT_NODE\(\s*\w+\s*,\s*"([^"]+)"\s*\)', block)
        if not node_match:
            continue

        pin_labels: dict[str, str] = {}
        for match in re.finditer(
            r"MaterialPin\s+(\w+)\s*(?:=)?\s*\{\s*this(?:\s*,\s*([^}]+))?\s*\}",
            block,
        ):
            pin_labels[match.group(1)] = parse_quoted_label(match.group(2) or "", match.group(1))

        arrays: dict[str, list[str]] = {}
        for match in re.finditer(r"Pin\s*\*\s*(\w+)\s*\[\s*\d+\s*\]\s*=\s*\{([^}]*)\}", block, re.S):
            arrays[match.group(1)] = re.findall(r"&\s*(\w+)", match.group(2))

        input_array = None
        output_array = None
        input_match = re.search(r"GetInputPins\(\)\s+override\s*\{\s*return\s+(\w+)\s*;", block)
        output_match = re.search(r"GetOutputPins\(\)\s+override\s*\{\s*return\s+(\w+)\s*;", block)
        if input_match:
            input_array = input_match.group(1)
        if output_match:
            output_array = output_match.group(1)
        if class_name == "MaterialNode":
            input_array = "m_surfaceInputPins"
        elif class_name == "MaterialFunctionOutputNode":
            pin_labels["m_inputPin"] = "Result"
            arrays["m_inputPins"] = ["m_inputPin"]
            input_array = "m_inputPins"

        property_defs: dict[str, dict] = {}
        type_map = {
            "Bool": "bool",
            "Float": "float",
            "Color": "color",
            "Int": "int",
            "String": "string",
            "AssetPath": "asset_path",
        }
        for match in re.finditer(
            r"(Bool|Float|Color|Int|String|AssetPath)Property\s+(\w+)\s*"
            r"(?:=\s*(?:\w+Property)?\s*)?\{\s*\"([^\"]+)\"",
            block,
        ):
            property_defs[match.group(2)] = {"name": match.group(3), "type": type_map[match.group(1)]}
        for match in re.finditer(
            r"EnumProperty<[^>]+>\s+(\w+)\s*(?:=)?\s*\{\s*\"([^\"]+)\"",
            block,
        ):
            property_defs[match.group(1)] = {"name": match.group(2), "type": "int"}
        for match in re.finditer(
            r"(Bool|Float|Color|Int|String|AssetPath)Property\s+(\w+)\s*\[\s*\d+\s*\]\s*=\s*\{(.*?)\};",
            block,
            re.S,
        ):
            base_name = match.group(2)
            labels = re.findall(r'\w+Property\(\s*"([^"]+)"', match.group(3))
            for index, label in enumerate(labels):
                property_defs[f"{base_name}[{index}]"] = {"name": label, "type": type_map[match.group(1)]}

        property_arrays: dict[str, list[str]] = {}
        for match in re.finditer(
            r"PropertyBase\s*\*\s*(\w+)\s*\[\s*\d+\s*\]\s*=\s*\{(.*?)\};",
            block,
            re.S,
        ):
            property_arrays[match.group(1)] = [
                re.sub(r"\s+", "", name)
                for name in re.findall(r"&\s*([\w]+\s*(?:\[\s*\d+\s*\])?)", match.group(2))
            ]
        properties_name = None
        properties_match = re.search(r"GetProperties\(\)\s+override\s*\{\s*return\s+(\w+)\s*;", block)
        if properties_match:
            properties_name = properties_match.group(1)
        if class_name == "MaterialNode":
            properties_name = "m_surfaceProperties"

        inputs = [
            {"name": pin_labels.get(member, member), "member": member}
            for member in arrays.get(input_array or "", [])
        ]
        outputs = [
            {"name": pin_labels.get(member, member), "member": member}
            for member in arrays.get(output_array or "", [])
        ]
        properties = [
            {**property_defs.get(member, {"name": member, "type": "unknown"}), "member": member}
            for member in property_arrays.get(properties_name or "", [])
        ]

        node_id = fnv1a(class_name)
        catalog[node_id] = {
            "type_id": node_id,
            "type_name": class_name,
            "display_name": node_match.group(1),
            "inputs": inputs,
            "outputs": outputs,
            "properties": properties,
            "dynamic": class_name in ("MaterialFunctionNode", "MaterialFunctionOutputNode"),
        }
    return catalog


def read_property(cursor: Cursor, prop: dict) -> Any:
    prop_type = prop["type"]
    if prop_type == "bool":
        return bool(cursor.u8())
    if prop_type == "float":
        return cursor.f32()
    if prop_type == "color":
        return list(cursor.unpack("<4f"))
    if prop_type == "int":
        return cursor.i32()
    if prop_type == "string":
        return cursor.string("u32")
    if prop_type == "asset_path":
        return cursor.string("u16")
    raise FormatError(f"Unsupported property type {prop_type!r} for {prop['name']}")


def write_property(prop: dict, value: Any) -> bytes:
    prop_type = prop["type"]
    if prop_type == "bool":
        return struct.pack("<B", int(bool(value)))
    if prop_type == "float":
        return struct.pack("<f", float(value))
    if prop_type == "color":
        if len(value) != 4:
            raise FormatError(f"Color property {prop['name']!r} needs four components")
        return struct.pack("<4f", *map(float, value))
    if prop_type == "int":
        return struct.pack("<i", int(value))
    if prop_type == "string":
        return pack_string(str(value), "u32")
    if prop_type == "asset_path":
        return pack_string(str(value), "u16")
    raise FormatError(f"Unsupported property type {prop_type!r}")


def read_material_function_signature(asset_root: Path, asset_path: str) -> tuple[list[dict], list[dict]]:
    path = asset_root / Path(asset_path)
    if not path.exists():
        raise FormatError(f"Referenced material function does not exist: {path}")
    chunks = read_chunks(path.read_bytes())
    inputs: list[dict] = []
    outputs: list[dict] = []
    for chunk in chunks:
        if chunk["magic"] not in ("INPS", "OUTP"):
            continue
        cursor = Cursor(chunk["payload"])
        count = cursor.u32()
        values = []
        for _ in range(count):
            values.append({"name": cursor.string("u8"), "parameter_type": cursor.u8()})
        ensure_consumed(cursor, chunk["magic"])
        if chunk["magic"] == "INPS":
            inputs = values
        else:
            outputs = values
    return inputs, outputs


def parse_graph(payload: bytes, catalog: dict[int, dict], asset_root: Path) -> dict:
    cursor = Cursor(payload)
    node_count, next_id, root_id = cursor.unpack("<III")
    nodes = []
    for _ in range(node_count):
        type_id = cursor.u32()
        node_type = catalog.get(type_id)
        if not node_type:
            raise FormatError(f"Unknown graph node type 0x{type_id:08x} at offset {cursor.offset - 4}")
        function_path = None
        active_type = node_type
        if node_type["type_name"] == "MaterialFunctionNode":
            function_path = cursor.string("u16")
            function_inputs, function_outputs = read_material_function_signature(asset_root, function_path)
            active_type = {
                **node_type,
                "inputs": [{"name": item["name"]} for item in function_inputs],
                "outputs": [{"name": item["name"]} for item in function_outputs],
            }
        node_id = cursor.u32()
        position = list(cursor.unpack("<4f"))
        input_count = cursor.u8()
        if input_count > len(active_type["inputs"]):
            raise FormatError(
                f"{node_type['type_name']} stores {input_count} inputs but catalog has {len(active_type['inputs'])}"
            )
        inputs = []
        for index in range(input_count):
            pin_id, link_id = cursor.unpack("<II")
            inputs.append(
                {
                    "id": pin_id,
                    "link": link_id or None,
                    "name": active_type["inputs"][index]["name"],
                }
            )
        output_count = cursor.u8()
        if output_count > len(active_type["outputs"]):
            raise FormatError(
                f"{node_type['type_name']} stores {output_count} outputs but catalog has {len(active_type['outputs'])}"
            )
        outputs = []
        for index in range(output_count):
            pin_id, link_id = cursor.unpack("<II")
            outputs.append(
                {
                    "id": pin_id,
                    "link": link_id or None,
                    "name": active_type["outputs"][index]["name"],
                }
            )
        property_count = cursor.u8()
        if property_count > len(node_type["properties"]):
            raise FormatError(
                f"{node_type['type_name']} stores {property_count} properties but catalog has {len(node_type['properties'])}"
            )
        properties = []
        for index in range(property_count):
            prop = node_type["properties"][index]
            properties.append(
                {"name": prop["name"], "type": prop["type"], "value": read_property(cursor, prop)}
            )
        nodes.append(
            {
                "id": node_id,
                "type_id": type_id,
                "type_name": node_type["type_name"],
                "display_name": node_type["display_name"],
                "position": {"x": position[0], "y": position[1], "width": position[2], "height": position[3]},
                "inputs": inputs,
                "outputs": outputs,
                "properties": properties,
                **({"material_function": function_path} if function_path is not None else {}),
            }
        )
    ensure_consumed(cursor, "GRPH")
    return {
        "node_count": node_count,
        "next_id": next_id,
        "root_node_id": None if root_id == 0xFFFFFFFF else root_id,
        "nodes": nodes,
    }


def validate_graph(graph: dict, catalog: dict[int, dict]) -> None:
    nodes = graph.get("nodes", [])
    node_ids = [int(node["id"]) for node in nodes]
    if len(set(node_ids)) != len(node_ids):
        raise FormatError("Graph node IDs are not unique")
    pin_ids: list[int] = []
    links: dict[int, int] = {}
    pin_directions: dict[int, str] = {}
    for node in nodes:
        type_id = int(node.get("type_id", fnv1a(node["type_name"])))
        node_type = catalog.get(type_id)
        if not node_type:
            raise FormatError(f"Unknown node type {node.get('type_name')} / 0x{type_id:08x}")
        for direction in ("inputs", "outputs"):
            expected = node_type[direction]
            actual = node.get(direction, [])
            if not node_type["dynamic"] and len(actual) > len(expected):
                raise FormatError(f"{node_type['type_name']} has too many {direction}")
            for pin in actual:
                pin_id = int(pin["id"])
                pin_ids.append(pin_id)
                pin_directions[pin_id] = direction
                if pin.get("link") is not None:
                    links[pin_id] = int(pin["link"])
        properties = node.get("properties", [])
        if len(properties) > len(node_type["properties"]):
            raise FormatError(f"{node_type['type_name']} has too many properties")
        for index, prop in enumerate(properties):
            expected = node_type["properties"][index]
            if prop.get("type") != expected["type"]:
                raise FormatError(
                    f"{node_type['type_name']} property {index} type is {prop.get('type')}, expected {expected['type']}"
                )
    if len(set(pin_ids)) != len(pin_ids):
        raise FormatError("Graph pin IDs are not unique")
    pin_set = set(pin_ids)
    for source, target in links.items():
        if target not in pin_set:
            raise FormatError(f"Pin {source} links to missing pin {target}")
        if pin_directions[source] == pin_directions[target]:
            raise FormatError(f"Link {source}->{target} connects two {pin_directions[source]} pins")
    root = graph.get("root_node_id")
    if root is not None and int(root) not in set(node_ids):
        raise FormatError(f"Root node {root} does not exist")
    all_ids = node_ids + pin_ids
    if all_ids and int(graph["next_id"]) <= max(all_ids):
        raise FormatError(
            f"next_id {graph['next_id']} must be greater than every node and pin ID ({max(all_ids)})"
        )


def encode_graph(graph: dict, catalog: dict[int, dict]) -> bytes:
    validate_graph(graph, catalog)
    nodes = graph["nodes"]
    root_id = 0xFFFFFFFF if graph.get("root_node_id") is None else int(graph["root_node_id"])
    result = bytearray(struct.pack("<III", len(nodes), int(graph["next_id"]), root_id))
    for node in nodes:
        type_id = int(node.get("type_id", fnv1a(node["type_name"])))
        node_type = catalog[type_id]
        position = node.get("position", {})
        result += struct.pack("<I", type_id)
        if node_type["type_name"] == "MaterialFunctionNode":
            function_path = node.get("material_function")
            if not function_path:
                raise FormatError("MaterialFunctionNode requires material_function")
            result += pack_string(function_path, "u16")
        result += struct.pack(
            "<I4f",
            int(node["id"]),
            float(position.get("x", 0)),
            float(position.get("y", 0)),
            float(position.get("width", 0)),
            float(position.get("height", 0)),
        )
        inputs = node.get("inputs", [])
        outputs = node.get("outputs", [])
        properties = node.get("properties", [])
        result += struct.pack("<B", len(inputs))
        for pin in inputs:
            result += struct.pack("<II", int(pin["id"]), int(pin.get("link") or 0))
        result += struct.pack("<B", len(outputs))
        for pin in outputs:
            result += struct.pack("<II", int(pin["id"]), int(pin.get("link") or 0))
        result += struct.pack("<B", len(properties))
        for index, prop in enumerate(properties):
            result += write_property(node_type["properties"][index], prop["value"])
    return bytes(result)


def parse_material(path: Path, source_path: Path | None) -> tuple[dict, list[dict], bytes]:
    data = path.read_bytes()
    chunks = read_chunks(data)
    if not chunks:
        raise FormatError("Asset contains no chunks")
    if chunks[0]["magic"] in ("HMAT", "HMIT"):
        kind = "hmat" if chunks[0]["magic"] == "HMAT" else "hmi"
        version_cursor = Cursor(chunks[0]["payload"])
        version = version_cursor.u32()
    elif path.suffix.lower() == ".hmf":
        kind = "hmf"
        version = None
    else:
        raise FormatError("Expected HMAT, HMIT, or HMF chunks")
    result: dict[str, Any] = {
        "schema": f"mmo-{kind}-json-v1",
        "kind": kind,
        "source": str(path),
        "source_sha256": sha256(data),
        "chunks": [{"magic": item["magic"], "size": item["size"], "offset": item["offset"]} for item in chunks],
    }
    if version is not None:
        result["version"] = version
        result["version_hex"] = f"0x{version:04x}"
    content_chunks = chunks if kind == "hmf" else chunks[1:]
    for chunk in content_chunks:
        magic, payload = chunk["magic"], chunk["payload"]
        if magic == "NAME":
            result["name"] = Cursor(payload).string("u8")
        elif magic == "PRNT":
            result["parent"] = Cursor(payload).string("u8")
        elif magic == "ATTR":
            result["attributes"] = parse_attributes(payload)
        elif magic == "TEXT":
            result["textures"] = parse_textures(payload)
        elif magic == "SPAR":
            result["scalar_parameters"] = parse_scalar_params(payload)
        elif magic == "VPAR":
            result["vector_parameters"] = parse_vector_params(payload)
        elif magic == "TPAR":
            result["texture_parameters"] = parse_texture_params(payload)
        elif magic == "VRTX":
            assert version is not None
            result["vertex_shaders"] = parse_shader_summary(payload, version, "vertex")
        elif magic == "PIXL":
            assert version is not None
            result["pixel_shaders"] = parse_shader_summary(payload, version, "pixel")
        elif magic in ("INPS", "OUTP"):
            cursor = Cursor(payload)
            count = cursor.u32()
            values = [
                {"name": cursor.string("u8"), "parameter_type": cursor.u8()}
                for _ in range(count)
            ]
            ensure_consumed(cursor, magic)
            result["inputs" if magic == "INPS" else "outputs"] = values
        elif magic == "GRPH":
            if source_path is None:
                result["graph"] = {"error": "Pass --source-root to decode the graph node catalog"}
            else:
                catalog = build_node_catalog(source_path / "src/mmo_edit/editors/material_editor/material_node.h")
                try:
                    result["graph"] = parse_graph(
                        payload,
                        catalog,
                        source_path / "data/client",
                    )
                except (FormatError, UnicodeError) as error:
                    result["graph"] = {"error": str(error), "size": len(payload)}
    return result, chunks, data


def rewrite_chunks(chunks: list[dict], replacements: dict[str, bytes | None]) -> bytes:
    output = bytearray()
    handled = set()
    for chunk in chunks:
        magic = chunk["magic"]
        if magic not in replacements:
            output += chunk["raw"]
            continue
        handled.add(magic)
        payload = replacements[magic]
        if payload is not None:
            output += make_chunk(magic, payload)
    for magic, payload in replacements.items():
        if magic not in handled and payload is not None:
            output += make_chunk(magic, payload)
    return bytes(output)


def apply_hmi(document: dict, chunks: list[dict]) -> bytes:
    replacements: dict[str, bytes | None] = {
        "NAME": pack_string(document["name"], "u8"),
        "PRNT": pack_string(document["parent"], "u8"),
        "ATTR": encode_attributes(document.get("attributes", {})),
        "SPAR": encode_scalar_params(document.get("scalar_parameters", []))
        if document.get("scalar_parameters")
        else None,
        "VPAR": encode_vector_params(document.get("vector_parameters", []))
        if document.get("vector_parameters")
        else None,
        "TPAR": encode_texture_params(document.get("texture_parameters", []))
        if document.get("texture_parameters")
        else None,
    }
    return rewrite_chunks(chunks, replacements)


def apply_hmat_graph(document: dict, chunks: list[dict], catalog: dict[int, dict]) -> bytes:
    graph = document.get("graph")
    if not graph or graph.get("error"):
        raise FormatError("JSON has no decodable graph")
    return rewrite_chunks(chunks, {"GRPH": encode_graph(graph, catalog)})


def write_checked(output: Path, data: bytes, overwrite: bool) -> None:
    if output.exists() and not overwrite:
        raise FormatError(f"{output} exists; pass --overwrite to replace it")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)


def print_summary(document: dict) -> None:
    version = f" {document['version_hex']}" if "version_hex" in document else ""
    print(f"{document['source']}: {document['kind'].upper()}{version}")
    if document["kind"] != "hmf":
        print(f"  name: {document.get('name', '<missing>')}")
    if "parent" in document:
        print(f"  parent: {document['parent']}")
    attributes = document.get("attributes", {})
    if attributes:
        print(
            f"  type: {attributes.get('material_type')}; two-sided={attributes.get('two_sided')}; "
            f"depth-write={attributes.get('depth_write', 'legacy')}; depth-test={attributes.get('depth_test', 'legacy')}"
        )
    for key in ("textures", "scalar_parameters", "vector_parameters", "texture_parameters"):
        if key in document:
            print(f"  {key.replace('_', ' ')}: {len(document[key])}")
    for key in ("inputs", "outputs"):
        if key in document:
            print(f"  function {key}: {len(document[key])}")
    graph = document.get("graph")
    if graph:
        if graph.get("error"):
            print(f"  graph: unavailable ({graph['error']})")
        else:
            print(f"  graph: {graph['node_count']} nodes, root={graph['root_node_id']}")
    for key in ("vertex_shaders", "pixel_shaders"):
        if key in document:
            print(f"  {key.replace('_', ' ')}: {len(document[key]['entries'])}")


def resolve_source_header(source_root: Path) -> Path:
    header = source_root / "src/mmo_edit/editors/material_editor/material_node.h"
    if not header.exists():
        raise FormatError(f"Could not find live node registry source at {header}")
    return header


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path.cwd(), help="MMO repository root")
    subparsers = parser.add_subparsers(dest="command", required=True)

    inspect_parser = subparsers.add_parser("inspect")
    inspect_parser.add_argument("input", type=Path)
    inspect_parser.add_argument("--json", action="store_true")

    export_parser = subparsers.add_parser("export-json")
    export_parser.add_argument("input", type=Path)
    export_parser.add_argument("--output", "-o", required=True, type=Path)

    catalog_parser = subparsers.add_parser("node-catalog")
    catalog_parser.add_argument("--output", "-o", type=Path)

    validate_parser = subparsers.add_parser("validate-json")
    validate_parser.add_argument("input", type=Path)

    apply_parser = subparsers.add_parser("apply-json")
    apply_parser.add_argument("input", type=Path, help="Exported JSON document")
    apply_parser.add_argument("--source", type=Path, help="Original HMAT/HMI; defaults to JSON source")
    apply_parser.add_argument("--output", "-o", required=True, type=Path)
    apply_parser.add_argument("--overwrite", action="store_true")
    apply_parser.add_argument("--force", action="store_true", help="Ignore source SHA-256 mismatch")
    apply_parser.add_argument(
        "--allow-stale-shaders",
        action="store_true",
        help="Required for HMAT graph writes; editor recompilation is still required",
    )

    args = parser.parse_args()
    try:
        source_header = resolve_source_header(args.source_root)
        catalog = build_node_catalog(source_header)

        if args.command == "node-catalog":
            output = {
                "schema": "mmo-material-node-catalog-v1",
                "source": str(source_header),
                "node_count": len(catalog),
                "nodes": sorted(catalog.values(), key=lambda item: item["display_name"]),
            }
            text = json.dumps(output, indent=2)
            if args.output:
                args.output.parent.mkdir(parents=True, exist_ok=True)
                args.output.write_text(text + "\n", encoding="utf-8")
                print(f"Wrote {args.output}")
            else:
                print(text)
            return 0

        if args.command in ("inspect", "export-json"):
            document, _, _ = parse_material(args.input, args.source_root)
            if args.command == "inspect":
                if args.json:
                    print(json.dumps(document, indent=2))
                else:
                    print_summary(document)
            else:
                args.output.parent.mkdir(parents=True, exist_ok=True)
                args.output.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
                print(f"Wrote {args.output}")
            return 0

        document = json.loads(args.input.read_text(encoding="utf-8"))
        if document.get("schema") not in ("mmo-hmat-json-v1", "mmo-hmi-json-v1", "mmo-hmf-json-v1"):
            raise FormatError(f"Unsupported JSON schema {document.get('schema')!r}")

        if document["kind"] == "hmat":
            graph = document.get("graph")
            if not graph or graph.get("error"):
                raise FormatError("HMAT JSON does not contain a writable graph")
            validate_graph(graph, catalog)
        elif document["kind"] == "hmi":
            if not document.get("name") or not document.get("parent"):
                raise FormatError("HMI JSON requires non-empty name and parent")
            encode_attributes(document.get("attributes", {}))
            encode_scalar_params(document.get("scalar_parameters", []))
            encode_vector_params(document.get("vector_parameters", []))
            encode_texture_params(document.get("texture_parameters", []))
        else:
            graph = document.get("graph")
            if not graph or graph.get("error"):
                raise FormatError("HMF JSON does not contain a decodable graph")
            validate_graph(graph, catalog)

        if args.command == "validate-json":
            print(f"{args.input}: valid {document['schema']}")
            return 0
        if document["kind"] == "hmf":
            raise FormatError("HMF JSON is currently inspect/export-only; edit material functions in mmo_edit")

        source = args.source or Path(document["source"])
        parsed, chunks, source_data = parse_material(source, args.source_root)
        if parsed["kind"] != document["kind"]:
            raise FormatError(f"JSON kind {document['kind']} does not match source kind {parsed['kind']}")
        if not args.force and sha256(source_data) != document.get("source_sha256"):
            raise FormatError("Source SHA-256 differs from export; re-export or pass --force")
        if document["kind"] == "hmi":
            output_data = apply_hmi(document, chunks)
        else:
            if not args.allow_stale_shaders:
                raise FormatError(
                    "HMAT graph writes preserve compiled shaders, which become stale; "
                    "pass --allow-stale-shaders and then open/save the material in mmo_edit"
                )
            output_data = apply_hmat_graph(document, chunks, catalog)
        write_checked(args.output, output_data, args.overwrite)
        print(f"Wrote {args.output}")
        if document["kind"] == "hmat":
            print("WARNING: Open and save this HMAT in mmo_edit to recompile shaders and parameter tables.")
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError, struct.error) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
