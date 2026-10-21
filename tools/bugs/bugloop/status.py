# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""What the bug loop is doing right now: artifacts/bug-loop/status.json, rewritten at every stage
change, and its rendering for `bug_loop.py status`. The file is for people; nothing reads it
back to decide anything."""

import ctypes
import datetime
import json
import os
import sys

STATUS_FILE = "status.json"
# A dry run (shadow mode) must not overwrite the live loop's status.
DRY_STATUS_FILE = "status-dry.json"

# Stage -> what it means, for the status command.
STAGE_LABELS = {
	"starting": "starting up",
	"polling": "checking decisions, CI and new reports",
	"triaging": "triaging a new report",
	"preparing": "bringing its worktree to origin/develop",
	"fixing": "the fixer is working on a bug",
	"reviewing": "the reviewer reads the candidate diff",
	"waiting-for-gate-lock": "waiting for the gate lock (another gate is running)",
	"proving": "running the regression proof",
	"gating": "running the fast gate on the candidate",
	"shipping": "merging and gating a ship",
	"sleeping": "sleeping until the next poll",
}

# Mode -> what it means, for the status command.
MODE_LABELS = {
	"busy": "busy: works the backlog without waiting",
	"idle": "idle: nothing to do, polls for new reports",
	"paused": "paused: bugs wait, but the loop cannot work them now",
}


def write(artifacts_dir, snapshot, name=STATUS_FILE):
	"""Atomically replaces the status file."""
	os.makedirs(artifacts_dir, exist_ok=True)
	path = os.path.join(artifacts_dir, name)
	temp = path + ".tmp"
	with open(temp, "w", encoding="utf-8") as handle:
		json.dump(snapshot, handle, indent=1, sort_keys=True, default=str)
	os.replace(temp, path)


def read(artifacts_dir, name=STATUS_FILE):
	"""The last snapshot, or None without a (readable) status file."""
	try:
		with open(os.path.join(artifacts_dir, name), "r", encoding="utf-8") as handle:
			return json.load(handle)
	except (OSError, ValueError):
		return None


def pid_alive(pid):
	"""True while a process with this id runs. Off Windows a signal 0 probe, on Windows the exit code."""
	if not isinstance(pid, int) or pid <= 0:
		return False
	if sys.platform != "win32":
		try:
			os.kill(pid, 0)
		except PermissionError:
			return True
		except OSError:
			return False
		return True
	kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
	kernel32.OpenProcess.restype = ctypes.c_void_p
	kernel32.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_bool, ctypes.c_uint32]
	kernel32.GetExitCodeProcess.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
	kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
	process_query_limited_information = 0x1000
	still_active = 259
	handle = kernel32.OpenProcess(process_query_limited_information, False, pid)
	if not handle:
		return False
	try:
		code = ctypes.c_uint32()
		if not kernel32.GetExitCodeProcess(handle, ctypes.byref(code)):
			return False
		return code.value == still_active
	finally:
		kernel32.CloseHandle(handle)


def _parse(text):
	try:
		return datetime.datetime.fromisoformat(text)
	except (TypeError, ValueError):
		return None


def _ago(now, text):
	moment = _parse(text)
	if moment is None:
		return "?"
	seconds = max(0, int((now - moment).total_seconds()))
	if seconds < 120:
		return "{} s".format(seconds)
	if seconds < 7200:
		return "{} min".format(seconds // 60)
	return "{:.1f} h".format(seconds / 3600)


def render(snapshot, now, alive):
	"""The status command's text. `alive` tells whether the loop process still runs."""
	if snapshot is None:
		return "bug loop: no status yet (the loop has not run since status reporting was added)"
	lines = []
	if not alive:
		lines.append("bug loop: NOT RUNNING (pid {} is gone; last update {} ago)".format(
			snapshot.get("pid"), _ago(now, snapshot.get("updated_at"))))
	else:
		lines.append("bug loop: running (pid {}, {}, up {})".format(
			snapshot.get("pid"), "dry run" if snapshot.get("dry_run") else "live", _ago(now, snapshot.get("started_at"))))
	mode = snapshot.get("mode") or {}
	if mode.get("name"):
		reason = " ({})".format(mode["reason"]) if mode.get("reason") else ""
		lines.append("mode:  {}{} since {} ago".format(MODE_LABELS.get(mode["name"], mode["name"]), reason, _ago(now, mode.get("since"))))
	stage = snapshot.get("stage") or "?"
	line = "now:   {} for {}".format(STAGE_LABELS.get(stage, stage), _ago(now, snapshot.get("stage_since")))
	if stage == "sleeping" and snapshot.get("until"):
		until = _parse(snapshot["until"])
		if until is not None:
			line += ", next poll in {} s".format(max(0, int((until - now).total_seconds())))
	lines.append(line)
	if snapshot.get("bug"):
		lines.append("bug:   {}{}".format(snapshot["bug"], " ({})".format(snapshot["branch"]) if snapshot.get("branch") else ""))
		if snapshot.get("summary"):
			lines.append("       " + str(snapshot["summary"])[:160].replace("\n", " "))
	if snapshot.get("detail"):
		lines.append("       " + str(snapshot["detail"])[:200])
	queue = snapshot.get("fix_queue") or []
	lines.append("queue: {} bug(s) to fix{}".format(len(queue), (", next " + ", ".join(queue[:3])) if queue else ""))
	if snapshot.get("ship_queue"):
		lines.append("ships: {} queued".format(snapshot["ship_queue"]))
	lines.append("today: {} of {} Claude invocations, {} of {} auto-ships".format(
		snapshot.get("invocations", 0), snapshot.get("budget", "?"), snapshot.get("autoships", 0), snapshot.get("autoship_cap", "?")))
	if snapshot.get("breaker"):
		lines.append("BREAKER tripped: auto-shipping is stopped")
	if snapshot.get("develop_red"):
		lines.append("develop is red: ships are held")
	last = snapshot.get("last_outcome")
	if last:
		lines.append("last:  {} {}".format(last.get("bug", ""), last.get("outcome", "")))
	return "\n".join(lines)
