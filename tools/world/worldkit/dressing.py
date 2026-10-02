# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Dressing passes: draft/manifest documents, the world fingerprint, the editor guard, apply and undo.

A pass is one template placed on one atlas place. Its document lives in
generated/world/passes/<id>/draft.json; once applied, a tracked copy goes to data/world/passes/<id>.json.
Apply writes one .wobj per prop and appends trees to the page .hfol files, recording every unique id
and hash; undo removes exactly those, keeping anything the user changed since (unless forced).
"""

from __future__ import annotations

import hashlib
import json
import os
import random
import subprocess
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

from .constants import entity_page_index
from .formats.chunks import FormatError
from .foliage import page_foliage_path
from .formats.hfol import FoliageInstance, append_instances, instance_hash, load_hfol, parse_hfol, remove_instances, write_hfol
from .formats.wobj import entity_file, new_unique_id, wobj_bytes
from .paths import REPO, client_root, entities_dir, foliage_dir, manifests_dir, passes_dir, terrain_dir
from .templates import item_rotation

DRAFT_VERSION = 1
APPLIED = ("applied", "applied-unchecked", "partially-undone")
# "applying" is the intent record written before the first world write: a pass a hard kill left in it is undone like an
# applied one (items not written yet are simply reported as already gone), but never walkability-checked.
UNDOABLE = APPLIED + ("applying",)
APPLY_KEYS = ("unique_id", "file", "written_hash")


class PassError(RuntimeError):
    pass


def new_pass_id(poi_id: str, repo: Path = REPO, today: str | None = None) -> str:
    stamp = today or datetime.now().strftime("%Y%m%d")
    prefix = f"{stamp}-{poi_id}-"
    used = [p.name for p in passes_dir(repo).glob(prefix + "*")] + [p.stem for p in manifests_dir(repo).glob(prefix + "*.json")]
    numbers = [int(name[len(prefix):]) for name in used if name[len(prefix):].isdigit()]
    return f"{prefix}{max(numbers, default=0) + 1}"


def new_draft(pass_id, map_id, world, poi_id, template, seed, anchor, keep_away, entry, items, gaps, notes, fingerprint) -> dict:
    return {"version": DRAFT_VERSION, "pass_id": pass_id, "map": map_id, "world": world, "poi": poi_id,
            "template": template, "seed": seed, "anchor": list(anchor), "keep_away": list(keep_away), "entry": list(entry),
            "created": datetime.now().isoformat(timespec="seconds"), "world_fingerprint": fingerprint, "items": items,
            "gaps": gaps, "notes": list(notes), "checks": {"placement": [], "walkability": []}, "status": "planned"}


def _round(value):
    if isinstance(value, float):
        return round(value, 2)
    if isinstance(value, list):
        return [_round(v) for v in value]
    if isinstance(value, dict):
        return {k: (v if k in ("written_hash", "world_fingerprint") else _round(v)) for k, v in value.items()}
    return value


def _paths(pass_id: str, repo: Path) -> tuple[Path, Path]:
    return passes_dir(repo) / pass_id / "draft.json", manifests_dir(repo) / f"{pass_id}.json"


def write_atomic(path: Path, data: bytes) -> None:
    """Writes through a temporary file next to `path` and swaps it in, so a crash, a kill or a full disk leaves either
    the old or the new file, never a truncated one. The temporary name ends in .tmp, which no world reader picks up."""
    tmp = path.with_name(path.name + ".tmp")
    try:
        with open(tmp, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(tmp, path)
    except BaseException:
        tmp.unlink(missing_ok=True)
        raise


def save_doc(doc: dict, repo: Path = REPO) -> None:
    data = (json.dumps(_round(doc), indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    work, tracked = _paths(doc["pass_id"], repo)
    work.parent.mkdir(parents=True, exist_ok=True)
    write_atomic(work, data)
    if doc["status"] != "planned":
        tracked.parent.mkdir(parents=True, exist_ok=True)
        write_atomic(tracked, data)


def items_hash(items: list[dict]) -> str:
    """Hash of the items as a check saw them (rounded like a saved draft, apply's own keys left out), so apply can tell
    whether the draft was edited after its last check."""
    canonical = [{k: v for k, v in item.items() if k not in APPLY_KEYS} for item in _round(items)]
    return hashlib.sha1(json.dumps(canonical, sort_keys=True, ensure_ascii=False).encode("utf-8")).hexdigest()


def load_doc(pass_id: str, repo: Path = REPO) -> dict:
    for path in _paths(pass_id, repo):
        if path.is_file():
            return json.loads(path.read_text(encoding="utf-8"))
    raise PassError(f"no pass {pass_id!r} in {passes_dir(repo)} or {manifests_dir(repo)}")


def list_docs(repo: Path = REPO) -> list[dict]:
    ids = {p.parent.name for p in passes_dir(repo).glob("*/draft.json")} | {p.stem for p in manifests_dir(repo).glob("*.json")}
    return [load_doc(pass_id, repo) for pass_id in sorted(ids)]


def world_fingerprint(directory: str, repo: Path = REPO) -> str:
    digest = hashlib.sha1(b"world-v1")
    sources = sorted(terrain_dir(directory, repo).glob("*.tile")) + sorted(entities_dir(directory, repo).glob("*/*.wobj"))
    sources += sorted(foliage_dir(directory, repo).glob("*.hfol"))
    for path in sources:
        stat = path.stat()
        digest.update(f"{path.relative_to(repo).as_posix()}|{stat.st_size}|{stat.st_mtime_ns}\n".encode())
    return digest.hexdigest()


def _default_probe() -> bool:
    """Whether mmo_edit is running. Output is read as bytes (a localized tasklist is not valid UTF-8) and any
    failure to find out raises: the guard fails closed, never reads "could not tell" as "not running"."""
    if os.name == "nt":
        cmd, exe = ["tasklist", "/FI", "IMAGENAME eq mmo_edit.exe", "/NH"], b"mmo_edit.exe"
    else:
        cmd, exe = ["pgrep", "-x", "mmo_edit"], None
    try:
        result = subprocess.run(cmd, capture_output=True)
    except (OSError, subprocess.SubprocessError) as exc:
        raise PassError(f"could not determine whether mmo_edit is running ({exc})") from exc
    if exe is not None:
        if result.returncode != 0:
            raise PassError(f"could not determine whether mmo_edit is running (tasklist exited {result.returncode})")
        return exe in (result.stdout or b"").lower()
    if result.returncode > 1:  # pgrep: 0 = found, 1 = none found, anything else = it failed
        raise PassError(f"could not determine whether mmo_edit is running (pgrep exited {result.returncode})")
    return result.returncode == 0


def editor_running(probe=None) -> bool:
    return bool((probe or _default_probe)())


def _guard_editor(probe) -> None:
    if editor_running(probe):
        raise PassError("mmo_edit is running: close it first (it keeps pages in memory and would overwrite foliage on save, "
                        "and it only discovers new prop files at startup)")


def _taken_ids(directory: str, repo: Path) -> set[int]:
    taken = {int(p.stem) for p in entities_dir(directory, repo).glob("*/*.wobj") if p.stem.isdigit()}
    for path in foliage_dir(directory, repo).glob("*.hfol"):
        taken |= {i.unique_id for i in load_hfol(path).instances}
    return taken


def _make_dirs(directory: Path, made: list[Path]) -> None:
    """mkdir -p, remembering which directories were newly created (child first) so a rollback can remove them."""
    missing = []
    probe = directory
    while not probe.exists():
        missing.append(probe)
        probe = probe.parent
    directory.mkdir(parents=True, exist_ok=True)
    made.extend(missing)


def _roll_back(written: list[Path], originals: dict[Path, bytes | None], made_dirs: list[Path]) -> list[str]:
    """Undoes a failed apply step by step; one failing step never skips the rest. Returns the steps that failed."""
    failures: list[str] = []
    for path in written:
        try:
            path.unlink(missing_ok=True)
        except OSError as exc:
            failures.append(f"could not remove {path}: {exc}")
    for path, original in originals.items():
        try:
            if original is None:
                path.unlink(missing_ok=True)
            elif not path.is_file() or path.read_bytes() != original:
                write_atomic(path, original)
        except OSError as exc:
            failures.append(f"could not restore {path}: {exc}")
    for directory in made_dirs:
        try:
            directory.rmdir()
        except OSError:
            pass  # not empty or already gone: not ours to force
    return failures


def _instance(item: dict, unique_id: int) -> FoliageInstance:
    scale = float(item["scale"])
    return FoliageInstance(unique_id, item["asset"], tuple(item["position"]), item_rotation(item), (scale, scale, scale),
                           bool(item["collides"]))


def apply_pass(doc: dict, repo: Path = REPO, probe=None, rng: random.Random | None = None) -> dict:
    """Writes the pass into the world and leaves it "applied-unchecked" (a walkability check promotes it to "applied").

    Everything is planned first, without writing. Then the original page .hfol files are backed up next to the draft and
    the manifest is saved as "applying" with every unique id and file, so even a hard kill mid-write leaves a pass that
    undo can find. Every world write is atomic. Any failure in the process, an interrupt included, rolls the world back.
    """
    if doc["status"] != "planned":
        raise PassError(f"pass {doc['pass_id']} is {doc['status']}, only planned passes can be applied")
    _guard_editor(probe)
    if world_fingerprint(doc["world"], repo) != doc["world_fingerprint"]:
        raise PassError("the world changed since this draft was planned or checked: run `dress.py check` first")
    if doc["checks"].get("items_hash") != items_hash(doc["items"]):
        raise PassError("the draft changed since its last check (or was never checked): run `dress.py check` first")
    if any(v.get("severity") == "error" for v in doc["checks"]["placement"]):
        raise PassError("the draft has placement errors: fix the draft and run `dress.py check`")
    for item in doc["items"]:
        if item.get("store") not in ("wobj", "hfol"):
            raise PassError(f"{item.get('asset')}: unknown store {item.get('store')!r} (expected 'wobj' or 'hfol')")
    rng = rng or random.Random()
    client = client_root(repo)
    taken = _taken_ids(doc["world"], repo)
    written: list[Path] = []
    originals: dict[Path, bytes | None] = {}
    made_dirs: list[Path] = []
    previous = {key: doc.get(key) for key in ("status", "created_files", "applied", "backups")}
    try:
        # 1. Plan every write; nothing touches the disk yet.
        wobj_writes: list[tuple[Path, bytes]] = []
        for item in doc["items"]:
            if item["store"] != "wobj":
                continue
            suffix = item["asset"].rsplit(".", 1)[-1].lower()
            if suffix not in ("hmsh", "hwmo"):
                raise PassError(f"{item['asset']}: only .hmsh and .hwmo can be placed")
            unique_id = new_unique_id(taken, rng)
            x, _, z = item["position"]
            path = entity_file(doc["world"], unique_id, x, z, repo)
            if path.exists():
                raise PassError(f"{path} already exists")
            scale = float(item["scale"])
            data = wobj_bytes(kind="mesh" if suffix == "hmsh" else "wmo", unique_id=unique_id, asset=item["asset"],
                              position=item["position"], rotation=item_rotation(item), scale=(scale, scale, scale),
                              category=f"dressing/{doc['template']}")
            wobj_writes.append((path, data))
            item.update(unique_id=f"0x{unique_id:016x}", file=path.relative_to(client).as_posix(),
                        written_hash=hashlib.sha1(data).hexdigest())
        by_page: dict[int, list[dict]] = {}
        for item in doc["items"]:
            if item["store"] == "hfol":
                if not item["asset"].lower().endswith(".hmsh"):
                    raise PassError(f"{item['asset']}: foliage must be a .hmsh")
                by_page.setdefault(entity_page_index(item["position"][0], item["position"][2]), []).append(item)
        hfol_writes: list[tuple[Path, bytes | None, bytes]] = []  # (path, original bytes or None, new bytes)
        for page, page_items in sorted(by_page.items()):
            path = page_foliage_path(doc["world"], page, repo)
            original = path.read_bytes() if path.is_file() else None
            instances = []
            for item in page_items:
                inst = _instance(item, new_unique_id(taken, rng))
                instances.append(inst)
                item.update(unique_id=f"0x{inst.unique_id:016x}", file=path.relative_to(client).as_posix(),
                            written_hash=instance_hash(inst))
            hfol_writes.append((path, original, write_hfol(append_instances(load_hfol(path), instances))))

        # 2. Back up the page files the pass is about to change, then record the intent.
        backup_dir = passes_dir(repo) / doc["pass_id"] / "backup"
        backups = []
        for path, original, _ in hfol_writes:
            if original is not None:
                backup_dir.mkdir(parents=True, exist_ok=True)
                write_atomic(backup_dir / path.name, original)
                backups.append((backup_dir / path.name).relative_to(repo).as_posix())
        doc["backups"] = backups
        doc["created_files"] = [path.relative_to(client).as_posix() for path, original, _ in hfol_writes if original is None]
        for doc_path in _paths(doc["pass_id"], repo):  # the saves are guarded too: a failing save rolls the world back
            originals[doc_path] = doc_path.read_bytes() if doc_path.is_file() else None
        doc["status"] = "applying"
        save_doc(doc, repo)

        # 3. Write the world, every file atomically.
        for path, data in wobj_writes:
            _make_dirs(path.parent, made_dirs)
            written.append(path)
            write_atomic(path, data)
        for path, original, data in hfol_writes:
            _make_dirs(path.parent, made_dirs)
            originals[path] = original  # recorded only when the write is next: a failure before it leaves the file untouched
            write_atomic(path, data)

        doc["status"] = "applied-unchecked"
        doc["applied"] = datetime.now().isoformat(timespec="seconds")
        save_doc(doc, repo)
    except BaseException as exc:  # any failure, an interrupt included, must leave the world exactly as it was
        failures = _roll_back(written, originals, made_dirs)
        for item in doc["items"]:
            for key in APPLY_KEYS:
                item.pop(key, None)
        for key, value in previous.items():
            if value is None:
                doc.pop(key, None)
            else:
                doc[key] = value
        if not isinstance(exc, Exception):
            raise  # KeyboardInterrupt and friends propagate unchanged, after the rollback
        if failures:
            raise PassError(f"apply failed ({exc}) and the rollback was incomplete, check by hand: {'; '.join(failures)}") from exc
        raise exc if isinstance(exc, PassError) else PassError(f"apply failed and was rolled back: {exc}") from exc
    return doc


def _moved_entity(directory: str, unique_id: str, repo: Path) -> Path | None:
    """The .wobj of a prop that is no longer where the pass wrote it, found by its unique id in any page folder."""
    matches = sorted(entities_dir(directory, repo).glob(f"*/{int(unique_id, 16)}.wobj"))
    return matches[0] if matches else None


@dataclass
class UndoReport:
    removed: list[str] = field(default_factory=list)
    changed: list[str] = field(default_factory=list)
    missing: list[str] = field(default_factory=list)


def undo_pass(doc: dict, repo: Path = REPO, force: bool = False, probe=None) -> UndoReport:
    if doc["status"] not in UNDOABLE:
        raise PassError(f"pass {doc['pass_id']} is {doc['status']}, nothing to undo")
    _guard_editor(probe)
    client = client_root(repo)
    root = client.resolve()
    report = UndoReport()
    kept: list[str] = []

    def contained(rel: str) -> Path | None:
        """The resolved path when it stays inside the client data root, else None (never touched, even when forced)."""
        path = (client / rel).resolve()
        return path if path.is_relative_to(root) else None

    # Plan everything first (reads and parses only), so a malformed file aborts before anything was deleted.
    delete: list[Path] = []
    for item in doc["items"]:
        if item["store"] != "wobj" or "file" not in item:
            continue
        path = contained(item["file"])
        if path is None:
            report.missing.append(f"{item['file']} (outside the client data folder, left alone)")
        elif not path.exists():
            moved = _moved_entity(doc["world"], item["unique_id"], repo)
            if moved is None:
                report.missing.append(item["file"])
            elif force:
                delete.append(moved)
                report.removed.append(moved.relative_to(client).as_posix())
            else:  # mmo_edit moves a prop dragged across a page border into the new page's folder: it is still placed
                report.changed.append(f"{moved.relative_to(client).as_posix()} (moved from {item['file']})")
                kept.append(item["unique_id"])
        elif hashlib.sha1(path.read_bytes()).hexdigest() != item["written_hash"] and not force:
            report.changed.append(item["file"])
            kept.append(item["unique_id"])
        else:
            delete.append(path)
            report.removed.append(item["file"])
    by_file: dict[str, list[dict]] = {}
    for item in doc["items"]:
        if item["store"] == "hfol" and "file" in item:
            by_file.setdefault(item["file"], []).append(item)
    rewrite: list[tuple[Path, bytes | None]] = []  # (path, new bytes), or unlink when the bytes are None
    for rel, file_items in sorted(by_file.items()):
        path = contained(rel)
        if path is None:
            report.missing += [f"{rel}#{i['unique_id']} (outside the client data folder, left alone)" for i in file_items]
            continue
        if not path.is_file():
            report.missing += [f"{rel}#{i['unique_id']}" for i in file_items]
            continue
        try:
            ff = parse_hfol(path.read_bytes(), str(path))
        except (FormatError, OSError) as exc:
            raise PassError(f"undo aborted before changing anything: {exc}") from exc
        current = {i.unique_id: i for i in ff.instances}
        remove = set()
        for item in file_items:
            unique_id = int(item["unique_id"], 16)
            label = f"{rel}#{item['unique_id']}"
            if unique_id not in current:
                report.missing.append(label)
            elif instance_hash(current[unique_id]) != item["written_hash"] and not force:
                report.changed.append(label)
                kept.append(item["unique_id"])
            else:
                remove.add(unique_id)
                report.removed.append(label)
        result = remove_instances(ff, remove)
        if not result.instances and rel in doc.get("created_files", []):
            rewrite.append((path, None))
        elif remove:
            rewrite.append((path, write_hfol(result)))
    for path in delete:
        path.unlink()
    for path, data in rewrite:
        if data is None:
            path.unlink()
        else:
            write_atomic(path, data)
    doc["status"] = "partially-undone" if kept else "undone"
    doc["kept_after_undo"] = kept
    doc["undone"] = datetime.now().isoformat(timespec="seconds")
    save_doc(doc, repo)
    return report
