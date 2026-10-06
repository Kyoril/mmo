# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

import base64
import json
import os
import tempfile
import unittest

from mmo_deployer.clients import GitHubClient, Notifier, PortainerClient, RestApi
from mmo_deployer.web import Http, HttpError

from .stub_server import StubServer


def _release(tag, created, draft=False, assets=()):
	return {"tag_name": tag, "target_commitish": tag[-8:], "created_at": created, "draft": draft, "assets": list(assets)}


class GitHub(unittest.TestCase):
	def test_list_filters_prefix_and_drafts_newest_first(self):
		with StubServer() as stub:
			stub.route("GET", "/repos/Kyoril/mmo/releases", (200, [
				_release("nightly-20261006-aaaaaaaa", "2026-10-06T00:50:00Z"),
				_release("test-nightly-20261007-bbbbbbbb", "2026-10-07T00:40:00Z"),
				_release("nightly-20261007-cccccccc", "2026-10-07T00:50:00Z"),
				_release("nightly-20261008-dddddddd", "2026-10-08T00:50:00Z", draft=True),
			]))
			client = GitHubClient("Kyoril/mmo", "tok", Http(), stub.url)
			tags = [r["tag_name"] for r in client.list_releases("nightly-")]
			self.assertEqual(tags, ["nightly-20261007-cccccccc", "nightly-20261006-aaaaaaaa"])
			self.assertEqual(stub.requests[0].headers["authorization"], "Bearer tok")

	def test_download_asset_does_not_leak_token_on_redirect(self):
		with StubServer() as stub:
			stub.route("GET", "/repos/Kyoril/mmo/releases/assets/5", (302, {"Location": stub.url + "/storage/blob"}, None))
			stub.route("GET", "/storage/blob", (200, b"payload"))
			client = GitHubClient("Kyoril/mmo", "tok", Http(), stub.url)
			release = _release("nightly-x", "2026-10-07T00:50:00Z", assets=[{"name": "client-bin.zip", "id": 5}])
			with tempfile.TemporaryDirectory() as tmp:
				dest = os.path.join(tmp, "out")
				client.download_asset(release, "client-bin.zip", dest)
				with open(dest, "rb") as handle:
					self.assertEqual(handle.read(), b"payload")
			api_request = stub.find("GET", "/repos/Kyoril/mmo/releases/assets/5")[0]
			self.assertEqual(api_request.headers["accept"], "application/octet-stream")
			self.assertEqual(api_request.headers["authorization"], "Bearer tok")
			self.assertNotIn("authorization", stub.find("GET", "/storage/blob")[0].headers)

	def test_missing_asset(self):
		client = GitHubClient("Kyoril/mmo", "", Http(), "http://127.0.0.1:9")
		with self.assertRaises(KeyError):
			client.download_asset(_release("nightly-x", "t"), "release.json", "unused")


class Rest(unittest.TestCase):
	def test_uptime_uses_basic_auth(self):
		with StubServer() as stub:
			stub.route("GET", "/uptime", (200, {"uptime": 42}))
			api = RestApi("realm01", stub.url, "pw", Http())
			self.assertEqual(api.uptime(), 42)
			expected = "Basic " + base64.b64encode(b"deployer:pw").decode("ascii")
			self.assertEqual(stub.requests[0].headers["authorization"], expected)

	def test_schedule_shutdown_posts_delay(self):
		with StubServer() as stub:
			stub.route("POST", "/shutdown", (200, {"status": "SUCCESS", "delay": 900}))
			self.assertTrue(RestApi("r", stub.url, "pw", Http()).schedule_shutdown(900))
			self.assertEqual(stub.requests[0].form(), {"delay": "900"})

	def test_schedule_shutdown_conflict_means_already_shutting_down(self):
		with StubServer() as stub:
			stub.route("POST", "/shutdown", (409, {"status": "SHUTTING_DOWN"}))
			self.assertFalse(RestApi("r", stub.url, "pw", Http()).schedule_shutdown(900))

	def test_schedule_shutdown_other_errors_raise(self):
		with StubServer() as stub:
			stub.route("POST", "/shutdown", (503, {"status": "UNAVAILABLE"}))
			with self.assertRaises(HttpError):
				RestApi("r", stub.url, "pw", Http()).schedule_shutdown(900)

	def test_cancel_ignores_not_pending(self):
		with StubServer() as stub:
			stub.route("POST", "/shutdown/cancel", (409, {"status": "NOT_PENDING"}))
			RestApi("r", stub.url, "pw", Http()).cancel_shutdown()


class Portainer(unittest.TestCase):
	def _stub(self, stub, env):
		stub.route("GET", "/api/stacks/7", (200, {"Id": 7, "Name": "mmo", "Env": env}))
		stub.route("GET", "/api/stacks/7/file", (200, {"StackFileContent": "services: {}\n"}))
		stub.route("PUT", "/api/stacks/7", (200, {"Id": 7}))

	def test_redeploy_sets_tag_keeps_other_env_and_pulls(self):
		with StubServer() as stub:
			self._stub(stub, [{"name": "MMO_TAG", "value": "old"}, {"name": "OTHER", "value": "1"}])
			PortainerClient(stub.url, "key", 7, 2, Http()).redeploy("newsha")
			put = stub.find("PUT", "/api/stacks/7")[0]
			self.assertEqual(put.query, {"endpointId": ["2"]})
			self.assertEqual(put.headers["x-api-key"], "key")
			body = put.json()
			self.assertEqual(body["stackFileContent"], "services: {}\n")
			self.assertTrue(body["pullImage"])
			self.assertFalse(body["prune"])
			self.assertEqual(sorted((e["name"], e["value"]) for e in body["env"]), [("MMO_TAG", "newsha"), ("OTHER", "1")])

	def test_current_tag_defaults_to_latest(self):
		with StubServer() as stub:
			self._stub(stub, None)
			self.assertEqual(PortainerClient(stub.url, "key", 7, 2, Http()).current_tag(), "latest")

	def test_service_states(self):
		with StubServer() as stub:
			self._stub(stub, [])
			stub.route("GET", "/api/endpoints/2/docker/containers/json", (200, [
				{"Names": ["/mmo-realm_server_01-1"], "State": "exited", "Labels": {"com.docker.compose.service": "realm_server_01"}},
				{"Names": ["/mmo-world_node_01-1"], "State": "running", "Labels": {"com.docker.compose.service": "world_node_01"}},
			]))
			states = PortainerClient(stub.url, "key", 7, 2, Http()).service_states()
			self.assertEqual(states, {"realm_server_01": "exited", "world_node_01": "running"})
			query = stub.find("GET", "/api/endpoints/2/docker/containers/json")[0].query
			self.assertEqual(json.loads(query["filters"][0]), {"label": ["com.docker.compose.project=mmo"]})


class Notify(unittest.TestCase):
	def test_discord_payload(self):
		with StubServer() as stub:
			stub.route("POST", "/hook", (204, None))
			Notifier(stub.url + "/hook", "discord", Http()).send("hello")
			self.assertEqual(stub.requests[0].json(), {"content": "hello"})

	def test_slack_payload(self):
		with StubServer() as stub:
			stub.route("POST", "/hook", (200, None))
			Notifier(stub.url + "/hook", "slack", Http()).send("hello")
			self.assertEqual(stub.requests[0].json(), {"text": "hello"})

	def test_failures_never_raise(self):
		with StubServer() as stub:
			stub.route("POST", "/hook", (500, {"error": "down"}))
			Notifier(stub.url + "/hook", "discord", Http()).send("hello")
		Notifier("http://127.0.0.1:9/hook", "discord", Http(timeout=1)).send("hello")

	def test_no_url_sends_nothing(self):
		Notifier("", "discord", Http()).send("hello")


if __name__ == "__main__":
	unittest.main()
