#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Tests for the bug loop's Discord notifier. No network: the opener is a fake."""

import io
import json
import os
import sys
import unittest
import urllib.error

sys.dont_write_bytecode = True

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools", "bugs"))

from bugloop import notify  # noqa: E402

HOOK = "https://discord.example/api/webhooks/123/secret-token"


class FakeResponse(io.BytesIO):
	def __enter__(self):
		return self

	def __exit__(self, *args):
		self.close()


class FakeOpener:
	def __init__(self, error=None):
		self.error = error
		self.requests = []

	def __call__(self, request, timeout=None):
		self.requests.append((request, timeout))
		if self.error:
			raise self.error
		return FakeResponse(b"")


class NotifierTests(unittest.TestCase):
	def make(self, error=None, url=HOOK, ui=""):
		self.logs = []
		self.opener = FakeOpener(error)
		return notify.Notifier(url, ui, log=self.logs.append, opener=self.opener)

	def test_disabled_without_webhook(self):
		notifier = self.make(url="")
		self.assertFalse(notifier.enabled)
		self.assertFalse(notifier.send("hello"))
		self.assertEqual(self.opener.requests, [])

	def test_payload_agent_timeout_and_no_mentions(self):
		notifier = self.make()
		self.assertTrue(notifier.send("@everyone look"))
		request, timeout = self.opener.requests[0]
		body = json.loads(request.data.decode("utf-8"))
		self.assertEqual(body["content"], "@everyone look")
		self.assertEqual(body["allowed_mentions"], {"parse": []})
		self.assertEqual(request.get_header("User-agent"), notify.USER_AGENT)
		self.assertEqual(timeout, 10)

	def test_long_messages_are_cut(self):
		notifier = self.make()
		notifier.send("x" * 5000)
		body = json.loads(self.opener.requests[0][0].data.decode("utf-8"))
		self.assertEqual(len(body["content"]), notify.LIMIT)

	def test_failures_are_swallowed_without_the_url(self):
		for error in (urllib.error.URLError("unreachable " + HOOK), TimeoutError(), ValueError(HOOK)):
			notifier = self.make(error=error)
			self.assertFalse(notifier.send("hi"))
			self.assertEqual(len(self.logs), 1)
			self.assertNotIn("secret-token", self.logs[0])

	def test_bug_links(self):
		self.assertEqual(self.make(ui="https://ui.example/").bug_link("abc"), "https://ui.example/bugs/abc")
		self.assertEqual(self.make().bug_link("abc"), "bug abc")

	def test_malformed_url_returns_false_and_logs_safely(self):
		notifier = self.make(url="not a url")
		self.assertFalse(notifier.send("hi"))
		self.assertEqual(len(self.logs), 1)
		self.assertNotIn("not a url", self.logs[0])


class MessageTests(unittest.TestCase):
	def test_design_question(self):
		notifier = notify.Notifier("", "https://ui.example")
		text = notify.design_question_message(notifier, "6ac666bcb69a0e836462d2bc", "Bandits do not assist", "Chain beyond one level?", "bugfix/6462d2bc")
		self.assertIn("Design decision needed", text)
		self.assertIn("https://ui.example/bugs/6ac666bcb69a0e836462d2bc", text)
		self.assertIn("Chain beyond one level?", text)
		self.assertIn("bugfix/6462d2bc", text)

	def test_shipped_and_breaker_and_refix_limit(self):
		notifier = notify.Notifier("")
		self.assertIn("maintainer decision", notify.shipped_message(notifier, "a" * 24, "1234567890", True))
		self.assertNotIn("maintainer decision", notify.shipped_message(notifier, "a" * 24, "1234567890", False))
		self.assertIn("red nightly", notify.breaker_message("red nightly"))
		self.assertIn("3", notify.refix_limit_message(notifier, "a" * 24, 3))

	def test_daily_summary(self):
		data = {"day": "2026-10-07", "invocations": 17, "outcomes": [
			{"bug": "a" * 24, "outcome": "shipped"},
			{"bug": "b" * 24, "outcome": "shipped-by-maintainer"},
			{"bug": "c" * 16 + "6462d2bc", "outcome": "parked", "reasons": ["review: chains without limit"]},
			{"bug": "d" * 24, "outcome": "abuse"},
		]}
		text = notify.daily_summary(data, 40, 3)
		self.assertIn("2026-10-07", text)
		self.assertIn("shipped 2", text)
		self.assertIn("parked 1", text)
		self.assertIn("abuse flags 1", text)
		self.assertIn("17 of 40", text)
		self.assertIn("waiting for a decision: 3", text)
		self.assertIn("6462d2bc: review: chains without limit", text)

	def test_feature_ready(self):
		notifier = notify.Notifier("", "https://ui.example")
		text = notify.feature_ready_message(notifier, "a" * 24, "Bandits ignore stationary casters", "bugfix/aaaaaaaa")
		self.assertIn("Feature ready for review", text)
		self.assertIn("https://ui.example/bugs/" + "a" * 24, text)
		self.assertIn("bugfix/aaaaaaaa", text)

	def test_ci_messages(self):
		notifier = notify.Notifier("", "https://ui.example")
		red = notify.ci_red_message(notifier, "a" * 24, "Linux Servers", "tests", "https://github.com/run/1")
		self.assertIn("develop is red", red)
		self.assertIn("https://ui.example/bugs/" + "a" * 24, red)
		self.assertIn("https://github.com/run/1", red)
		self.assertIn("Emergency fix shipped", notify.emergency_shipped_message(notifier, "a" * 24, "abcdef123"))
		self.assertIn("needs you", notify.emergency_needs_you_message(notifier, "a" * 24, "3 attempts failed"))
		self.assertIn("green again", notify.ci_green_message(["Linux Servers"]))


if __name__ == "__main__":
	unittest.main()
