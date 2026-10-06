# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Prepends nightly patch notes to the launcher's launcher.json.

Limits come from docs/launcher_content/README.md.
"""

import json
import logging
import os
import re
from pathlib import Path

MANIFEST_LIMIT = 256 * 1024
MAX_PATCHES = 32
TITLE_LIMIT = 180
DATE_LIMIT = 80
SUMMARY_LIMIT = 400
# Far below the launcher's 64 KiB per body, so 32 entries fit the 256 KiB manifest.
BODY_LIMIT = 6 * 1024
MAX_LINES = 100

_PREFIX = re.compile(r"^\w+(\([^)]*\))?!?:\s*")


def _clip(text, limit):
	encoded = text.encode("utf-8")
	if len(encoded) <= limit:
		return text
	return encoded[:limit - 3].decode("utf-8", "ignore") + "..."


def _encode(content):
	return json.dumps(content, indent=2, ensure_ascii=False).encode("utf-8")


def describe_change(subject):
	stripped = _PREFIX.sub("", subject).strip()
	if not stripped:
		return subject
	return stripped[:1].upper() + stripped[1:]


def add_patch_note(path, date_text, changes):
	path = Path(path)
	if not path.is_file():
		return False
	try:
		content = json.loads(path.read_text(encoding="utf-8-sig"))
	except ValueError as error:
		logging.getLogger("deployer").warning("Failed to parse launcher.json: %s", error)
		return False
	if not isinstance(content, dict):
		logging.getLogger("deployer").warning("launcher.json top level is not a dict")
		return False
	patches_field = content.get("patches")
	if patches_field is not None and not isinstance(patches_field, list):
		logging.getLogger("deployer").warning("launcher.json patches field is not a list")
		return False
	lines = [describe_change(change) for change in changes][:MAX_LINES]
	if not lines:
		summary = "Maintenance update"
		body = "## Changes\n- Maintenance update"
	else:
		summary = lines[0] if len(lines) == 1 else "{} fixes and improvements".format(len(lines))
		body = "## Changes\n" + "\n".join("- " + line for line in lines)
	entry = {
		"title": _clip("Update " + date_text, TITLE_LIMIT),
		"date": _clip(date_text, DATE_LIMIT),
		"summary": _clip(summary, SUMMARY_LIMIT),
		"body": _clip(body, BODY_LIMIT),
	}
	patches = ([entry] + list(content.get("patches") or []))[:MAX_PATCHES]
	content["patches"] = patches
	while len(patches) > 1 and len(_encode(content)) > MANIFEST_LIMIT:
		patches.pop()
	tmp = path.with_name(path.name + ".tmp")
	tmp.write_bytes(_encode(content))
	os.replace(tmp, path)
	return True
