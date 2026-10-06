# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""mysqldump of the login/realm databases before every redeploy.

Migrations apply themselves when the new servers start; this is their only undo.
"""

import gzip
import os
import shutil
import subprocess
from pathlib import Path

DUMP_TIMEOUT_S = 1800


class BackupError(Exception):
	pass


def dump_databases(cfg, dest, run=subprocess.run):
	dest = Path(dest)
	dest.mkdir(parents=True, exist_ok=True)
	env = dict(os.environ, MYSQL_PWD=cfg.mysql_password)
	paths = []
	for database in cfg.backup_databases:
		try:
			result = run(
				# --no-tablespaces: MySQL >= 8.0.21 otherwise demands the global PROCESS privilege.
				["mysqldump", "--single-transaction", "--no-tablespaces", "--routines", "--triggers",
					"-h", cfg.mysql_host, "-u", cfg.mysql_user, database],
				capture_output=True, env=env, timeout=DUMP_TIMEOUT_S)
		except subprocess.TimeoutExpired:
			raise BackupError("mysqldump {} timed out after {} seconds".format(database, DUMP_TIMEOUT_S))
		except OSError as error:
			raise BackupError("mysqldump {} failed: {}".format(database, str(error)))
		if result.returncode != 0:
			raise BackupError("mysqldump {} failed: {}".format(database, result.stderr.decode("utf-8", "replace")[-1000:]))
		if not result.stdout:
			raise BackupError("mysqldump {} produced no output".format(database))
		target = dest / (database + ".sql.gz")
		with gzip.open(target, "wb") as out:
			out.write(result.stdout)
		paths.append(target)
	return paths


def prune_backups(root, keep):
	"""Backup folders are named <timestamp>-<sha8>, so name order is age order."""
	root = Path(root)
	if not root.is_dir():
		return []
	folders = sorted((p for p in root.iterdir() if p.is_dir()), key=lambda p: p.name, reverse=True)
	removed = []
	for path in folders[keep:]:
		shutil.rmtree(path)
		removed.append(path.name)
	return removed
