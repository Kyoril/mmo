# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Clients for every remote system the deployer talks to."""

import http.client
import json
import logging
import os
import urllib.parse

from .web import HttpError, basic_auth

log = logging.getLogger("deployer")

# Discord rejects messages over 2000 characters; Slack accepts more but this keeps them readable.
MESSAGE_LIMIT = 1900


class GitHubClient:
	"""Reads nightly releases. A read-only token is enough; a public repo needs none."""

	def __init__(self, repo, token, http, api="https://api.github.com"):
		self.repo = repo
		self.token = token
		self.http = http
		self.api = api.rstrip("/")

	def _auth(self):
		return {"Authorization": "Bearer " + self.token} if self.token else {}

	def list_releases(self, prefix):
		"""Published releases whose tag starts with `prefix`, newest first."""
		url = "{}/repos/{}/releases?per_page=50".format(self.api, self.repo)
		releases = self.http.get_json(url, {"Accept": "application/vnd.github+json"}, self._auth())
		matching = [r for r in releases if r["tag_name"].startswith(prefix) and not r.get("draft")]
		return sorted(matching, key=lambda r: r["created_at"], reverse=True)

	def download_asset(self, release, name, dest):
		for asset in release.get("assets", []):
			if asset["name"] == name:
				url = "{}/repos/{}/releases/assets/{}".format(self.api, self.repo, asset["id"])
				self.http.download(url, dest, {"Accept": "application/octet-stream"}, self._auth())
				return
		raise KeyError("release {} has no asset {}".format(release["tag_name"], name))

	def fetch_manifest(self, release, scratch_dir):
		path = os.path.join(scratch_dir, "release.json")
		self.download_asset(release, "release.json", path)
		with open(path, encoding="utf-8") as handle:
			return json.load(handle)


class RestApi:
	"""Realm or login server REST API. Both check only the Basic auth password."""

	def __init__(self, name, url, password, http):
		self.name = name
		self.url = url.rstrip("/")
		self.password = password
		self.http = http

	def _auth(self):
		return basic_auth("deployer", self.password)

	def uptime(self):
		return int(self.http.get_json(self.url + "/uptime", None, self._auth())["uptime"])

	def shutdown_status(self):
		return self.http.get_json(self.url + "/shutdown", None, self._auth())

	def schedule_shutdown(self, delay):
		"""True if scheduled; False if the realm is already shutting down (409)."""
		try:
			self.http.post_form(self.url + "/shutdown", {"delay": str(delay)}, None, self._auth())
			return True
		except HttpError as error:
			if error.status == 409:
				return False
			raise

	def cancel_shutdown(self):
		try:
			self.http.post_form(self.url + "/shutdown/cancel", {}, None, self._auth())
		except HttpError as error:
			if error.status != 409:
				raise


class PortainerClient:
	"""Updates one file-based (web editor) compose stack through the Portainer CE API."""

	def __init__(self, url, token, stack_id, endpoint_id, http):
		self.url = url.rstrip("/")
		self.token = token
		self.stack_id = stack_id
		self.endpoint_id = endpoint_id
		self.http = http

	def _auth(self):
		return {"X-API-Key": self.token}

	def _stack(self):
		return self.http.get_json("{}/api/stacks/{}".format(self.url, self.stack_id), None, self._auth())

	def ping(self):
		self._stack()

	def current_tag(self):
		for item in self._stack().get("Env") or []:
			if item.get("name") == "MMO_TAG":
				return item.get("value")
		return "latest"

	def redeploy(self, tag):
		"""Sets MMO_TAG and redeploys with image pull. Returns once compose is up."""
		stack = self._stack()
		content = self.http.get_json("{}/api/stacks/{}/file".format(self.url, self.stack_id), None, self._auth())["StackFileContent"]
		env = [item for item in (stack.get("Env") or []) if item.get("name") != "MMO_TAG"]
		env.append({"name": "MMO_TAG", "value": tag})
		url = "{}/api/stacks/{}?endpointId={}".format(self.url, self.stack_id, self.endpoint_id)
		self.http.send_json("PUT", url, {"stackFileContent": content, "env": env, "prune": False, "pullImage": True}, None, self._auth())

	def service_states(self):
		"""Compose service name -> container state ("running", "exited", ...)."""
		name = self._stack()["Name"]
		filters = json.dumps({"label": ["com.docker.compose.project={}".format(name)]})
		url = "{}/api/endpoints/{}/docker/containers/json?all=1&filters={}".format(
			self.url, self.endpoint_id, urllib.parse.quote(filters))
		containers = self.http.get_json(url, None, self._auth())
		return {c["Labels"].get("com.docker.compose.service", c["Names"][0]): c["State"] for c in containers}


class Notifier:
	"""Discord or Slack webhook. Logs every message; delivery failures are only logged."""

	def __init__(self, url, fmt, http):
		self.url = url
		self.key = "content" if fmt == "discord" else "text"
		self.http = http

	def send(self, text):
		log.info("notify: %s", text)
		if not self.url:
			return
		try:
			payload = json.dumps({self.key: text[:MESSAGE_LIMIT]}).encode("utf-8")
			self.http.request("POST", self.url, {"Content-Type": "application/json"}, payload)
		except HttpError as error:
			# The body is the provider's error ("error code: 1010", Discord's JSON), never the URL.
			log.warning("notification failed: HTTP %s %s", error.status, " ".join(error.body.split())[:160])
		except (OSError, ValueError, http.client.HTTPException) as error:
			log.warning("notification failed: %s", type(error).__name__)
