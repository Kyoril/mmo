# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Persistent deployer state (state.json), always written atomically."""

import json
import os
from dataclasses import asdict, dataclass, field, fields
from typing import Optional

HISTORY_LIMIT = 100


@dataclass
class State:
	live: Optional[str] = None
	previous_live: Optional[str] = None
	staged: Optional[str] = None
	staged_tag: Optional[str] = None
	staged_changes: list = field(default_factory=list)
	bad: list = field(default_factory=list)
	paused: bool = False
	last_maintenance_date: Optional[str] = None
	# {"commit": sha, "started": iso time} while a maintenance runs; set at startup means it was interrupted.
	maintenance_in_progress: Optional[dict] = None
	history: list = field(default_factory=list)


def load_state(path):
	if not os.path.exists(path):
		return State()
	with open(path, encoding="utf-8") as handle:
		data = json.load(handle)
	known = {f.name for f in fields(State)}
	return State(**{key: value for key, value in data.items() if key in known})


def save_state(path, state):
	tmp = "{}.tmp".format(path)
	with open(tmp, "w", encoding="utf-8") as handle:
		json.dump(asdict(state), handle, indent=2)
	os.replace(tmp, path)


def record(state, now, event, **details):
	entry = {"time": now.isoformat(), "event": event}
	entry.update(details)
	state.history.append(entry)
	del state.history[:-HISTORY_LIMIT]
