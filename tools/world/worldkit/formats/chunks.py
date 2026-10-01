# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Chunked binary container reading shared by all world formats.

A chunk is a 4-byte magic, a little-endian uint32 payload size, then the payload. Magics are
compared as the bytes found on disk (the C++ multi-char literals appear byte-reversed).
"""

from __future__ import annotations

import struct

import numpy as np


class FormatError(ValueError):
    """Raised when a world file does not match the layout this parser knows. Never guessed around."""


def iter_chunks(data: bytes, source: str) -> list[tuple[bytes, memoryview]]:
    """Splits a file into (magic, payload) pairs, validating every size against the file length."""
    chunks: list[tuple[bytes, memoryview]] = []
    view = memoryview(data)
    offset = 0
    while offset < len(data):
        if offset + 8 > len(data):
            raise FormatError(f"{source}: truncated chunk header at byte {offset}")
        magic = bytes(view[offset:offset + 4])
        size = struct.unpack_from("<I", data, offset + 4)[0]
        start = offset + 8
        end = start + size
        if end > len(data):
            raise FormatError(f"{source}: chunk {magic!r} at byte {offset} claims {size} bytes but only {len(data) - start} remain")
        chunks.append((magic, view[start:end]))
        offset = end
    return chunks


class Cursor:
    """Sequential little-endian reader over one chunk payload."""

    def __init__(self, payload: memoryview, source: str, magic: bytes):
        self._data = payload
        self._pos = 0
        self._source = source
        self._magic = magic

    def _take(self, count: int) -> memoryview:
        if self._pos + count > len(self._data):
            raise FormatError(f"{self._source}: chunk {self._magic!r} ends early (need {count} bytes at offset {self._pos}, size {len(self._data)})")
        out = self._data[self._pos:self._pos + count]
        self._pos += count
        return out

    def u8(self) -> int:
        return self._take(1)[0]

    def u16(self) -> int:
        return struct.unpack("<H", self._take(2))[0]

    def u32(self) -> int:
        return struct.unpack("<I", self._take(4))[0]

    def u64(self) -> int:
        return struct.unpack("<Q", self._take(8))[0]

    def f32(self) -> float:
        return struct.unpack("<f", self._take(4))[0]

    def floats(self, count: int) -> np.ndarray:
        return np.frombuffer(self._take(count * 4), dtype="<f4").astype(np.float32)

    def str8(self) -> str:
        return bytes(self._take(self.u8())).decode("utf-8")

    def str16(self) -> str:
        return bytes(self._take(self.u16())).decode("utf-8")

    def raw(self, count: int) -> memoryview:
        """The next `count` bytes as-is (vertex and index blocks)."""
        return self._take(count)

    def strz32(self) -> str:
        """u32 length, that many bytes, then one NUL not counted in the length (world-model strings)."""
        text = bytes(self._take(self.u32())).decode("utf-8")
        if self.u8() != 0:
            raise FormatError(f"{self._source}: chunk {self._magic!r} string is not NUL-terminated")
        return text

    def remaining(self) -> int:
        return len(self._data) - self._pos

    def done(self) -> bool:
        return self._pos == len(self._data)


def chunk_bytes(magic: bytes, payload: bytes) -> bytes:
    """One chunk as the engine's ChunkWriter writes it: 4 magic bytes, u32 payload size, payload."""
    return magic + struct.pack("<I", len(payload)) + payload


def str8_bytes(text: str) -> bytes:
    raw = text.encode("utf-8")
    return struct.pack("<B", len(raw)) + raw


def str16_bytes(text: str) -> bytes:
    raw = text.encode("utf-8")
    return struct.pack("<H", len(raw)) + raw
