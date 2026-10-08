# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

"""Minimal GitHub Actions client of the bug loop (stdlib only). The token reads Actions and
contents and may start a workflow; it is never logged or put into an error message."""

import http.client
import json
import re
import urllib.error
import urllib.parse
import urllib.request

API = "https://api.github.com"
USER_AGENT = "mmo-bug-loop/1"
_ORIGIN = re.compile(r"(?:git@github\.com:|https://github\.com/)([^/]+)/([^/]+?)(?:\.git)?/?$")


class GitHubError(RuntimeError):
	pass


class _NoRedirect(urllib.request.HTTPRedirectHandler):
	def redirect_request(self, *args, **kwargs):
		return None


def parse_origin(url):
	match = _ORIGIN.match((url or "").strip())
	if not match:
		raise ValueError("not a GitHub remote: " + str(url)[:100])
	return match.group(1), match.group(2)


class GitHub:
	def __init__(self, owner, repo, token, opener=None):
		self.base = "{}/repos/{}/{}".format(API, owner, repo)
		self.token = token
		# Redirects are followed by hand: the log download URL is pre-signed and must not get the token.
		self.opener = opener or urllib.request.build_opener(_NoRedirect).open

	def _request(self, url, method="GET", body=None, auth=True):
		data = json.dumps(body).encode("utf-8") if body is not None else None
		request = urllib.request.Request(url, data=data, method=method)
		request.add_header("User-Agent", USER_AGENT)
		request.add_header("Accept", "application/vnd.github+json")
		request.add_header("X-GitHub-Api-Version", "2022-11-28")
		if auth:
			request.add_header("Authorization", "Bearer " + self.token)
		if data is not None:
			request.add_header("Content-Type", "application/json")
		return request

	def _open(self, request):
		try:
			with self.opener(request, timeout=30) as response:
				return response.read()
		except urllib.error.HTTPError as error:
			try:
				location = error.headers.get("Location") if error.headers else None
				if error.code in (301, 302, 303, 307, 308) and location:
					if not location.startswith("https://"):
						raise GitHubError("GitHub redirected to a non-https location for {}".format(_path(request.full_url)))
					raise _Redirect(location)
				raise GitHubError("GitHub answered {} for {} {}".format(error.code, request.get_method(), _path(request.full_url)))
			finally:
				error.close()
		except (urllib.error.URLError, OSError, http.client.HTTPException) as error:
			raise GitHubError("GitHub unreachable ({}) for {}".format(type(error).__name__, _path(request.full_url)))

	def _json(self, path, **query):
		url = self.base + path + ("?" + urllib.parse.urlencode(query) if query else "")
		try:
			body = self._open(self._request(url)) or b"{}"
		except _Redirect:
			# A moved repository (301) or endpoint: the configured origin is stale. No Location in the message.
			raise GitHubError("GitHub redirected {}".format(_path(url)))
		try:
			return json.loads(body)
		except ValueError:
			raise GitHubError("GitHub sent a non-JSON body for {}".format(_path(url)))

	def runs(self, workflow, branch=None, per_page=20):
		query = {"per_page": per_page}
		if branch:
			query["branch"] = branch
		return self._json("/actions/workflows/{}/runs".format(workflow), **query).get("workflow_runs", [])

	def jobs(self, run_id):
		return self._json("/actions/runs/{}/jobs".format(run_id)).get("jobs", [])

	def job_log(self, job_id):
		try:
			body = self._open(self._request(self.base + "/actions/jobs/{}/logs".format(job_id)))
		except _Redirect as redirect:
			try:
				body = self._open(self._request(redirect.location, auth=False))
			except _Redirect:
				raise GitHubError("GitHub log download redirected twice for job {}".format(job_id))
		return body.decode("utf-8", "replace")

	def dispatch(self, workflow, ref):
		url = self.base + "/actions/workflows/{}/dispatches".format(workflow)
		try:
			self._open(self._request(url, method="POST", body={"ref": ref}))
		except _Redirect:
			raise GitHubError("GitHub redirected {}".format(_path(url)))


class _Redirect(Exception):
	def __init__(self, location):
		super().__init__(location)
		self.location = location


def _path(url):
	return urllib.parse.urlsplit(url).path
