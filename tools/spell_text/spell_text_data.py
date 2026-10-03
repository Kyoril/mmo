# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""
Spell data loading and a placeholder parser matching the client's spell text formatter
(src/shared/game_client/spell_text_formatter.cpp).
"""

import re
import subprocess
import tempfile
from pathlib import Path

from google.protobuf import descriptor_pb2, descriptor_pool, message_factory

ROOT = Path(__file__).resolve().parents[2]

# '$', an optional referenced spell id, a token letter and an optional effect index digit.
# "$$" is a literal dollar sign.
PLACEHOLDER = re.compile(r"\$(?:(?P<literal>\$)|(?P<ref>\d*)(?P<token>[A-Za-z]?)(?P<index>\d?))")
INDEXED_TOKENS = set("smotiSMOTI")
KNOWN_TOKENS = INDEXED_TOKENS | set("dD")


def find_protoc():
    for config in ("Release", "RelWithDebInfo", "Debug"):
        candidate = ROOT / f"build/_deps/protobuf-build/{config}/protoc.exe"
        if candidate.is_file():
            return candidate
    raise FileNotFoundError("protoc.exe not found under build/_deps/protobuf-build")


def editor_path():
    return ROOT / "data/editor/data/spells.data"


def client_path():
    return ROOT / "data/client/ClientDB/spells.data"


def load_spells(path=None):
    schema_dir = ROOT / "src/shared/proto_data"
    with tempfile.TemporaryDirectory(prefix="spell_text_") as tmp:
        desc = Path(tmp) / "d.pb"
        subprocess.run([str(find_protoc()), f"-I{schema_dir}", f"--descriptor_set_out={desc}",
                        "--include_imports", "spells.proto"], cwd=schema_dir, check=True)
        file_set = descriptor_pb2.FileDescriptorSet.FromString(desc.read_bytes())
    pool = descriptor_pool.DescriptorPool()
    for entry in file_set.file:
        pool.Add(entry)
    spells_type = message_factory.GetMessageClass(pool.FindMessageTypeByName("mmo.proto.Spells"))
    return spells_type.FromString((path or editor_path()).read_bytes())


def spell_texts(spell):
    """Yields (label, text) for every description and aura text of a spell, in all locales."""
    yield "description", spell.description
    yield "auratext", spell.auratext
    for entry in spell.description_loc:
        yield f"description[{entry.locale}]", entry.value
    for entry in spell.auratext_loc:
        yield f"auratext[{entry.locale}]", entry.value


def placeholder_problems(text, spell, spells_by_id):
    """Returns a list of human readable problems with the placeholders in one text."""
    problems = []
    index = 0
    for match in PLACEHOLDER.finditer(text):
        if match["literal"]:
            continue

        token = match["token"]
        if not token:
            problems.append(f"{match[0]!r} has no token letter")
            continue
        if token not in KNOWN_TOKENS:
            problems.append(f"{match[0]!r} uses unknown token {token!r}")
            continue

        source = spell
        if match["ref"]:
            source = spells_by_id.get(int(match["ref"]))
            if source is None:
                problems.append(f"{match[0]!r} references unknown spell {match['ref']}")
                continue

        if token in INDEXED_TOKENS:
            if match["index"]:
                index = int(match["index"])
            if index >= len(source.effects):
                problems.append(f"{match[0]!r} reads effect {index} of spell {source.id}, "
                                f"which has {len(source.effects)}")
    return problems
