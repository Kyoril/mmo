# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

import gzip
import subprocess
import tempfile
import unittest
from pathlib import Path

from mmo_deployer.backup import BackupError, dump_databases, prune_backups

from .fakes import make_config


class FakeRun:
	def __init__(self, returncode=0, stdout=b"-- dump", stderr=b""):
		self.calls = []
		self.result = subprocess.CompletedProcess([], returncode, stdout, stderr)

	def __call__(self, command, **kwargs):
		self.calls.append((command, kwargs))
		return self.result


class Backup(unittest.TestCase):
	def setUp(self):
		self.tmp = tempfile.TemporaryDirectory()
		self.cfg = make_config(self.tmp.name)
		self.dest = Path(self.tmp.name) / "backups" / "20261007-044500-cccccccc"

	def tearDown(self):
		self.tmp.cleanup()

	def test_dumps_every_database_compressed(self):
		run = FakeRun()
		paths = dump_databases(self.cfg, self.dest, run)
		self.assertEqual([p.name for p in paths], ["mmo_login.sql.gz", "mmo_realm_01.sql.gz"])
		with gzip.open(paths[0]) as handle:
			self.assertEqual(handle.read(), b"-- dump")
		command, kwargs = run.calls[0]
		self.assertEqual(command[0], "mysqldump")
		self.assertIn("--single-transaction", command)
		self.assertEqual(command[-1], "mmo_login")
		self.assertEqual(kwargs["env"]["MYSQL_PWD"], "secret")
		self.assertNotIn("secret", command)

	def test_failure_raises(self):
		with self.assertRaises(BackupError) as ctx:
			dump_databases(self.cfg, self.dest, FakeRun(returncode=2, stderr=b"access denied"))
		self.assertIn("access denied", str(ctx.exception))

	def test_empty_dump_raises(self):
		with self.assertRaises(BackupError):
			dump_databases(self.cfg, self.dest, FakeRun(stdout=b""))

	def test_prune_keeps_newest_by_name(self):
		root = Path(self.tmp.name) / "backups"
		for name in ("20261001-0445-a", "20261002-0445-b", "20261003-0445-c"):
			(root / name).mkdir(parents=True)
		self.assertEqual(prune_backups(root, 2), ["20261001-0445-a"])
		self.assertEqual(sorted(p.name for p in root.iterdir()), ["20261002-0445-b", "20261003-0445-c"])

	def test_prune_missing_dir(self):
		self.assertEqual(prune_backups(Path(self.tmp.name) / "none", 3), [])


if __name__ == "__main__":
	unittest.main()
