#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for tools/bugs/bugs.py, the bug API client.

No network: BugApi takes an opener, and these tests hand it a fake one that records the
request and returns a canned JSON body.

    python tools/tests/test_bugs_cli.py
"""

import importlib.util
import io
import json
import os
import sys
import unittest

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MODULE_PATH = os.path.join(REPO_ROOT, "tools", "bugs", "bugs.py")

_spec = importlib.util.spec_from_file_location("bugs", MODULE_PATH)
bugs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(bugs)


class FakeResponse(io.BytesIO):
	def __enter__(self):
		return self

	def __exit__(self, *args):
		self.close()


class FakeOpener:
	def __init__(self, payload):
		self.payload = payload
		self.requests = []

	def __call__(self, request, timeout=None):
		self.requests.append(request)
		return FakeResponse(json.dumps(self.payload).encode("utf-8"))


class BugApiTests(unittest.TestCase):
	def make(self, payload=None):
		opener = FakeOpener(payload if payload is not None else {})
		return bugs.BugApi("https://example.test/", "secret", opener=opener), opener

	def test_list_builds_query_and_sends_key(self):
		api, opener = self.make({"bugs": [], "pagination": {}})
		api.list(status="new", subject="item:42", since="2026-10-01", page=2, limit=5)
		request = opener.requests[0]
		self.assertEqual(request.get_method(), "GET")
		self.assertEqual(
			request.full_url,
			"https://example.test/api/bugs?status=new&subjectType=item&subjectId=42&since=2026-10-01&page=2&limit=5")
		self.assertEqual(request.get_header("X-api-key"), "secret")

	def test_subject_without_id_filters_type_only(self):
		api, opener = self.make({"bugs": []})
		api.list(subject="generic")
		self.assertIn("subjectType=generic", opener.requests[0].full_url)
		self.assertNotIn("subjectId", opener.requests[0].full_url)

	def test_claim_posts_worker(self):
		api, opener = self.make({"_id": "x"})
		api.claim("abc", "agent-a")
		request = opener.requests[0]
		self.assertEqual(request.get_method(), "POST")
		self.assertTrue(request.full_url.endswith("/api/bugs/abc/claim"))
		self.assertEqual(json.loads(request.data), {"worker": "agent-a"})

	def test_update_sends_only_given_fields(self):
		api, opener = self.make({"_id": "x"})
		api.update("abc", status="triaged", note="looks like a data bug", actor="me")
		request = opener.requests[0]
		self.assertEqual(request.get_method(), "PATCH")
		self.assertEqual(json.loads(request.data), {"status": "triaged", "note": "looks like a data bug", "actor": "me"})

	def test_main_requires_api_key(self):
		env = dict(os.environ)
		env.pop("MMO_BUG_API_KEY", None)
		self.assertEqual(bugs.main(["list"], environ=env, out=io.StringIO()), 2)


if __name__ == "__main__":
	unittest.main()
