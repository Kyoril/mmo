#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Startup of tools/bugs/bug_loop.py: the game-data decoder is built eagerly from the runtime
snapshot's schemas with the main checkout's protoc, and its absence only disables data fixes."""

import importlib.util
import os
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MODULE_PATH = os.path.join(REPO_ROOT, "tools", "bugs", "bug_loop.py")
_spec = importlib.util.spec_from_file_location("bug_loop_cli", MODULE_PATH)
bug_loop = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(bug_loop)


class StartupTests(unittest.TestCase):
	def test_decoder_is_built_from_the_runtime_schemas_with_main_protoc(self):
		with tempfile.TemporaryDirectory() as repo:
			protoc = os.path.join(repo, "build", "_deps", "protobuf-build", "Release", "protoc.exe")
			os.makedirs(os.path.dirname(protoc))
			open(protoc, "wb").close()
			calls = []
			decoder = bug_loop.make_decoder("H:/runtime", repo, calls.append,
				factory=lambda root, protoc: calls.append((root, protoc)) or "decoder")
			self.assertEqual(decoder, "decoder")
			self.assertEqual(calls, [("H:/runtime", protoc)])

	def test_missing_protoc_runs_without_decoder(self):
		messages = []
		with tempfile.TemporaryDirectory() as folder:
			decoder = bug_loop.make_decoder(folder, folder, messages.append)
		self.assertIsNone(decoder)
		self.assertIn("decoder unavailable", messages[0])

	def test_runtime_root_is_the_checkout_of_the_script(self):
		self.assertEqual(os.path.normcase(bug_loop.RUNTIME_ROOT), os.path.normcase(REPO_ROOT))


if __name__ == "__main__":
	unittest.main()
