#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Command line client for the central bug API (error.mmo-dev.net /api/bugs).

    python tools/bugs/bugs.py list [--status new] [--subject item:42] [--since 2026-10-01]
    python tools/bugs/bugs.py show <id>
    python tools/bugs/bugs.py claim <id> --worker <name>
    python tools/bugs/bugs.py update <id> [--status triaged] [--note "..."] [--pr <url>]

Environment: MMO_BUG_API_KEY (reader key, required), MMO_BUG_API_URL (default
https://error.mmo-dev.net). Output is JSON on stdout so agents can parse it.
"""

import argparse
import json
import os
import sys
import urllib.parse
import urllib.request

DEFAULT_URL = "https://error.mmo-dev.net"


class BugApi:
	def __init__(self, base_url, api_key, opener=urllib.request.urlopen):
		self.base_url = base_url.rstrip("/")
		self.api_key = api_key
		self.opener = opener

	def _call(self, method, path, query=None, body=None):
		url = self.base_url + path
		if query:
			url += "?" + urllib.parse.urlencode(query)
		data = json.dumps(body).encode("utf-8") if body is not None else None
		request = urllib.request.Request(url, data=data, method=method)
		request.add_header("X-Api-Key", self.api_key)
		request.add_header("Accept", "application/json")
		if data is not None:
			request.add_header("Content-Type", "application/json")
		with self.opener(request, timeout=30) as response:
			return json.loads(response.read().decode("utf-8"))

	def list(self, status=None, subject=None, since=None, page=1, limit=20, decision_pending=False):
		query = []
		if status:
			query.append(("status", status))
		if subject:
			subject_type, _, subject_id = subject.partition(":")
			query.append(("subjectType", subject_type))
			if subject_id:
				query.append(("subjectId", subject_id))
		if since:
			query.append(("since", since))
		if decision_pending:
			query.append(("decisionPending", "true"))
		query.append(("page", str(page)))
		query.append(("limit", str(limit)))
		return self._call("GET", "/api/bugs", query=query)

	def show(self, bug_id):
		return self._call("GET", "/api/bugs/" + urllib.parse.quote(bug_id))

	def claim(self, bug_id, worker):
		return self._call("POST", "/api/bugs/" + urllib.parse.quote(bug_id) + "/claim", body={"worker": worker})

	def update(self, bug_id, release_claim=False, **fields):
		body = {key: value for key, value in fields.items() if value is not None}
		if release_claim:
			# None values are dropped above, so releasing the claim needs its own switch.
			body["claimedBy"] = None
		return self._call("PATCH", "/api/bugs/" + urllib.parse.quote(bug_id), body=body)

	def put_review_diff(self, bug_id, diff, actor="unknown"):
		return self._call("PUT", "/api/bugs/" + urllib.parse.quote(bug_id) + "/review-diff", body={"diff": diff, "actor": actor})


def build_parser():
	parser = argparse.ArgumentParser(description="Central bug API client")
	sub = parser.add_subparsers(dest="command", required=True)

	p_list = sub.add_parser("list", help="list bugs, newest first")
	p_list.add_argument("--status")
	p_list.add_argument("--subject", help="type or type:id, e.g. item:42")
	p_list.add_argument("--since", help="ISO date")
	p_list.add_argument("--page", type=int, default=1)
	p_list.add_argument("--limit", type=int, default=20)

	p_show = sub.add_parser("show", help="full bug report")
	p_show.add_argument("id")

	p_claim = sub.add_parser("claim", help="take a bug for processing")
	p_claim.add_argument("id")
	p_claim.add_argument("--worker", required=True)

	p_update = sub.add_parser("update", help="update workflow fields")
	p_update.add_argument("id")
	p_update.add_argument("--status")
	p_update.add_argument("--note")
	p_update.add_argument("--pr", dest="prUrl")
	p_update.add_argument("--duplicate-of", dest="duplicateOf")
	p_update.add_argument("--actor", default=os.environ.get("USERNAME") or os.environ.get("USER") or "cli")
	return parser


def main(argv=None, environ=None, out=None, err=None):
	environ = os.environ if environ is None else environ
	out = sys.stdout if out is None else out
	err = sys.stderr if err is None else err
	args = build_parser().parse_args(argv)

	api_key = environ.get("MMO_BUG_API_KEY")
	if not api_key:
		print("MMO_BUG_API_KEY is not set (reader key of the bug API)", file=err)
		return 2

	api = BugApi(environ.get("MMO_BUG_API_URL", DEFAULT_URL), api_key)
	if args.command == "list":
		result = api.list(args.status, args.subject, args.since, args.page, args.limit)
	elif args.command == "show":
		result = api.show(args.id)
	elif args.command == "claim":
		result = api.claim(args.id, args.worker)
	else:
		result = api.update(args.id, status=args.status, note=args.note, prUrl=args.prUrl,
			duplicateOf=args.duplicateOf, actor=args.actor)

	json.dump(result, out, indent=2, ensure_ascii=False)
	out.write("\n")
	return 0


if __name__ == "__main__":
	sys.exit(main())
