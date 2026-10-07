# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""subprocess.run with a timeout that kills the whole process tree. subprocess.run only kills
the direct child: a build, a test or a claude process that started its own children would
outlive the timeout and, holding the output pipes open, block the loop forever."""

import os
import signal
import subprocess

# How long to wait for the pipes to drain once the tree is gone.
DRAIN_SECONDS = 30


def kill_tree(process):
	"""Best effort: on Windows taskkill /T walks the tree from the child; elsewhere the child
	leads its own process group (start_new_session)."""
	if os.name == "nt":
		subprocess.run(["taskkill", "/T", "/F", "/PID", str(process.pid)], stdout=subprocess.DEVNULL,
			stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL)
	else:
		try:
			os.killpg(process.pid, signal.SIGKILL)
		except OSError:
			pass
	try:
		process.kill()
	except OSError:
		pass


def run_with_tree_kill(command, input=None, capture_output=False, timeout=None, check=False, **kwargs):
	"""The subprocess.run signature; on timeout the whole tree is killed before TimeoutExpired."""
	if capture_output:
		kwargs["stdout"] = subprocess.PIPE
		kwargs["stderr"] = subprocess.PIPE
	if input is not None:
		kwargs["stdin"] = subprocess.PIPE
	if os.name != "nt":
		kwargs.setdefault("start_new_session", True)
	with subprocess.Popen(command, **kwargs) as process:
		try:
			stdout, stderr = process.communicate(input, timeout=timeout)
		except subprocess.TimeoutExpired:
			kill_tree(process)
			try:
				stdout, stderr = process.communicate(timeout=DRAIN_SECONDS)
			except subprocess.TimeoutExpired:
				# Something outside the tree still holds a pipe; give up on the output.
				stdout = stderr = None
			raise subprocess.TimeoutExpired(command, timeout, output=stdout, stderr=stderr)
		except BaseException:
			kill_tree(process)
			raise
		code = process.poll()
	if check and code:
		raise subprocess.CalledProcessError(code, command, output=stdout, stderr=stderr)
	return subprocess.CompletedProcess(command, code, stdout, stderr)
