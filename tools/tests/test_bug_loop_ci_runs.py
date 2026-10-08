#!/usr/bin/env python3
# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""The GitHub client (with a fake transport) and the pure CI evaluation of the bug loop."""

import http.client
import io
import json
import os
import sys
import unittest
import urllib.error

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tools", "bugs"))

from bugloop import ci_runs, github  # noqa: E402

CI_LOG = """2026-10-07T21:20:00.0000000Z ##[group]Run cd build && ctest --output-on-failure
2026-10-07T21:20:01.0000000Z 11/33 Test #11: game_protocol_tests ..............   Passed    0.96 sec
2026-10-07T21:20:02.0000000Z 12/33 Test #12: game_server_tests ................Subprocess aborted***Exception:   0.78 sec
2026-10-07T21:20:02.0000000Z double free or corruption (fasttop)
2026-10-07T21:20:02.0000000Z Idle ally joins the fight when a stationary neighbour is attacked
2026-10-07T21:20:02.0000000Z /home/runner/work/mmo/mmo/src/tests/game_server_tests/creature_assist_test.cpp:129: FAILED:
2026-10-07T21:20:02.0000000Z   SIGABRT - Abort (abnormal termination) signal
2026-10-07T21:20:03.0000000Z 97% tests passed, 1 tests failed out of 33
2026-10-07T21:20:03.0000000Z ##[error]Process completed with exit code 8.
"""


class FakeResponse(io.BytesIO):
	def __init__(self, body, status=200, headers=None):
		super().__init__(body if isinstance(body, bytes) else json.dumps(body).encode("utf-8"))
		self.status = status
		self.headers = headers or {}

	def __enter__(self):
		return self

	def __exit__(self, *args):
		self.close()


class FakeOpener:
	def __init__(self, routes):
		self.routes = routes
		self.requests = []

	def __call__(self, request, timeout=None):
		self.requests.append(request)
		url = request.full_url
		for prefix, response in self.routes:
			if url.startswith(prefix):
				if isinstance(response, Exception):
					raise response
				return response
		raise AssertionError("unexpected request " + url)


API = "https://api.github.com/repos/Kyoril/mmo"


class OriginTests(unittest.TestCase):
	def test_ssh_and_https_origins(self):
		self.assertEqual(github.parse_origin("git@github.com:Kyoril/mmo.git"), ("Kyoril", "mmo"))
		self.assertEqual(github.parse_origin("https://github.com/Kyoril/mmo"), ("Kyoril", "mmo"))
		with self.assertRaises(ValueError):
			github.parse_origin("H:/somewhere/mmo")


class ClientTests(unittest.TestCase):
	def test_runs_sends_the_token_and_filters_the_branch(self):
		opener = FakeOpener([(API + "/actions/workflows/ccpp.yml/runs", FakeResponse({"workflow_runs": [{"id": 1}]}))])
		runs = github.GitHub("Kyoril", "mmo", "secret-token", opener=opener).runs("ccpp.yml", branch="develop")
		self.assertEqual(runs, [{"id": 1}])
		request = opener.requests[0]
		self.assertIn("branch=develop", request.full_url)
		self.assertEqual(request.get_header("Authorization"), "Bearer secret-token")

	def test_errors_never_carry_the_token(self):
		error = urllib.error.HTTPError(API, 403, "Forbidden", {}, io.BytesIO(b"rate limit"))
		client = github.GitHub("Kyoril", "mmo", "secret-token", opener=FakeOpener([(API, error)]))
		with self.assertRaises(github.GitHubError) as caught:
			client.runs("ccpp.yml")
		self.assertIn("403", str(caught.exception))
		self.assertNotIn("secret-token", str(caught.exception))

	def test_job_log_follows_the_redirect_without_the_token(self):
		redirect = urllib.error.HTTPError(API, 302, "Found", {"Location": "https://blob.example/log"}, io.BytesIO(b""))
		opener = FakeOpener([(API + "/actions/jobs/7/logs", redirect), ("https://blob.example/log", FakeResponse(CI_LOG.encode("utf-8")))])
		text = github.GitHub("Kyoril", "mmo", "secret-token", opener=opener).job_log(7)
		self.assertIn("double free", text)
		self.assertIsNone(opener.requests[1].get_header("Authorization"))

	def _client(self, routes):
		return github.GitHub("Kyoril", "mmo", "secret-token", opener=FakeOpener(routes))

	def _assert_github_error(self, call):
		with self.assertRaises(github.GitHubError) as caught:
			call()
		self.assertNotIn("secret-token", str(caught.exception))

	def test_second_redirect_is_a_github_error(self):
		first = urllib.error.HTTPError(API, 302, "Found", {"Location": "https://blob.example/log"}, io.BytesIO(b""))
		second = urllib.error.HTTPError("https://blob.example/log", 302, "Found", {"Location": "https://other.example/log"}, io.BytesIO(b""))
		client = self._client([(API + "/actions/jobs/7/logs", first), ("https://blob.example/log", second)])
		self._assert_github_error(lambda: client.job_log(7))

	def test_non_https_redirect_is_a_github_error(self):
		redirect = urllib.error.HTTPError(API, 302, "Found", {"Location": "http://blob.example/log"}, io.BytesIO(b""))
		client = self._client([(API + "/actions/jobs/7/logs", redirect)])
		self._assert_github_error(lambda: client.job_log(7))

	def test_non_json_body_is_a_github_error(self):
		client = self._client([(API, FakeResponse(b"<html>proxy</html>"))])
		self._assert_github_error(lambda: client.runs("ccpp.yml"))

	def test_url_error_is_a_github_error(self):
		client = self._client([(API, urllib.error.URLError("boom secret-token"))])
		self._assert_github_error(lambda: client.runs("ccpp.yml"))

	def test_incomplete_read_is_a_github_error(self):
		client = self._client([(API, http.client.IncompleteRead(b"par"))])
		self._assert_github_error(lambda: client.runs("ccpp.yml"))

	def test_dispatch_posts_the_ref(self):
		opener = FakeOpener([(API + "/actions/workflows/nightly-release.yml/dispatches", FakeResponse(b"", status=204))])
		github.GitHub("Kyoril", "mmo", "t", opener=opener).dispatch("nightly-release.yml", "develop")
		self.assertEqual(json.loads(opener.requests[0].data), {"ref": "develop"})
		self.assertEqual(opener.requests[0].get_method(), "POST")

	def _moved(self):
		# A renamed repository: GitHub answers 301 with the new location.
		return urllib.error.HTTPError(API, 301, "Moved Permanently",
			{"Location": "https://api.github.com/repositories/123/secret-location"}, io.BytesIO(b""))

	def _assert_redirect_error(self, call):
		with self.assertRaises(github.GitHubError) as caught:
			call()
		self.assertIn("redirected", str(caught.exception))
		self.assertNotIn("secret-location", str(caught.exception))

	def test_redirected_runs_are_a_github_error(self):
		client = self._client([(API, self._moved())])
		self._assert_redirect_error(lambda: client.runs("ccpp.yml", branch="develop"))

	def test_redirected_jobs_are_a_github_error(self):
		client = self._client([(API, self._moved())])
		self._assert_redirect_error(lambda: client.jobs(7))

	def test_redirected_dispatch_is_a_github_error(self):
		client = self._client([(API, self._moved())])
		self._assert_redirect_error(lambda: client.dispatch("nightly-release.yml", "develop"))


class EvaluationTests(unittest.TestCase):
	def test_colours(self):
		self.assertEqual(ci_runs.colour({"status": "completed", "conclusion": "failure"}), "red")
		self.assertEqual(ci_runs.colour({"status": "completed", "conclusion": "timed_out"}), "red")
		self.assertEqual(ci_runs.colour({"status": "completed", "conclusion": "success"}), "green")
		self.assertIsNone(ci_runs.colour({"status": "completed", "conclusion": "cancelled"}))
		self.assertIsNone(ci_runs.colour({"status": "in_progress", "conclusion": None}))

	def test_newest_completed_skips_running_and_cancelled_runs(self):
		runs = [  # newest first, as GitHub lists them
			{"id": 4, "status": "in_progress", "conclusion": None, "event": "push"},
			{"id": 3, "status": "completed", "conclusion": "cancelled", "event": "push"},
			{"id": 2, "status": "completed", "conclusion": "failure", "event": "push"},
			{"id": 1, "status": "completed", "conclusion": "success", "event": "push"},
		]
		self.assertEqual(ci_runs.newest_completed(runs, lambda run: run["event"] == "push")["id"], 2)
		self.assertIsNone(ci_runs.newest_completed(runs, lambda run: run["event"] == "schedule"))

	def test_excerpt_keeps_the_failure_and_drops_timestamps(self):
		text = ci_runs.excerpt(CI_LOG)
		self.assertIn("creature_assist_test.cpp:129: FAILED", text)
		self.assertIn("double free or corruption", text)
		self.assertNotIn("2026-10-07T21:20", text)
		self.assertNotIn("game_protocol_tests", text)
		self.assertLessEqual(len(ci_runs.excerpt(CI_LOG * 500, limit=500)), 500)

	def test_excerpt_without_markers_keeps_the_tail(self):
		text = ci_runs.excerpt("\n".join("line {}".format(index) for index in range(200)))
		self.assertIn("line 199", text)
		self.assertNotIn("line 10\n", text)

	def test_failing_step(self):
		jobs = [{"id": 1, "conclusion": "success", "steps": []},
			{"id": 2, "conclusion": "failure", "steps": [{"name": "make", "conclusion": "success"}, {"name": "tests", "conclusion": "failure"}]}]
		job, step = ci_runs.failing_step(jobs)
		self.assertEqual((job["id"], step), (2, "tests"))
		self.assertEqual(ci_runs.failing_step([]), (None, ""))


if __name__ == "__main__":
	unittest.main()
