# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Discord notifications of the bug loop. Delivery is best effort: a failure is logged by its
exception class only (never the webhook URL) and never stops the loop."""

import json
import urllib.request

LIMIT = 2000
USER_AGENT = "mmo-bug-loop/1"


class Notifier:
	def __init__(self, webhook_url, ui_url="", log=print, opener=urllib.request.urlopen):
		self.webhook_url = webhook_url or ""
		self.ui_url = (ui_url or "").rstrip("/")
		self.log = log
		self.opener = opener

	@property
	def enabled(self):
		return bool(self.webhook_url)

	def bug_link(self, bug_id):
		return "{}/bugs/{}".format(self.ui_url, bug_id) if self.ui_url else "bug " + bug_id

	def send(self, text):
		if not self.enabled:
			return False
		if len(text) > LIMIT:
			text = text[:LIMIT - 1] + "…"
		try:
			# Summaries derive from player reports: never let them ping anyone.
			data = json.dumps({"content": text, "allowed_mentions": {"parse": []}}).encode("utf-8")
			request = urllib.request.Request(self.webhook_url, data=data, method="POST")
			request.add_header("Content-Type", "application/json")
			# Discord's Cloudflare front rejects urllib's default agent.
			request.add_header("User-Agent", USER_AGENT)
			with self.opener(request, timeout=10) as response:
				response.read()
			return True
		except Exception as error:  # a notification must never stop the loop
			try:
				self.log("notification failed: {}".format(type(error).__name__))
			except Exception:
				pass
			return False


def design_question_message(notifier, bug_id, summary, question, branch):
	return "**Design decision needed** — {}\n{}\n**Question:** {}\nBranch: `{}`".format(
		notifier.bug_link(bug_id), summary[:200], question[:900], branch)


def breaker_message(reason):
	return "**Bug loop circuit breaker tripped** — auto-shipping stopped: {}".format(reason[:500])


def shipped_message(notifier, bug_id, commit, by_maintainer):
	return "Shipped {} in `{}`{}".format(notifier.bug_link(bug_id), commit[:8], " (maintainer decision)" if by_maintainer else "")


def refix_limit_message(notifier, bug_id, limit):
	return "**Refix limit reached** — {} had {} guided refixes; finish it by hand.".format(notifier.bug_link(bug_id), limit)


def daily_summary(data, budget, waiting):
	counts = {}
	for entry in data.get("outcomes", []):
		counts[entry.get("outcome")] = counts.get(entry.get("outcome"), 0) + 1
	shipped = counts.get("shipped", 0) + counts.get("shipped-by-maintainer", 0)
	lines = [
		"**Bug loop summary for {}**".format(data.get("day", "?")),
		"shipped {}, parked {}, needs-info {}, abuse flags {}, discarded {}".format(
			shipped, counts.get("parked", 0), counts.get("needs-info", 0), counts.get("abuse", 0), counts.get("discarded", 0)),
		"Claude invocations: {} of {}".format(data.get("invocations", 0), budget),
	]
	if waiting is not None:
		lines.append("waiting for a decision: {}".format(waiting))
	for entry in [entry for entry in data.get("outcomes", []) if entry.get("outcome") == "parked"][:10]:
		reason = (entry.get("reasons") or ["?"])[0]
		lines.append("- {}: {}".format(str(entry.get("bug", ""))[-8:], str(reason)[:120]))
	return "\n".join(lines)
