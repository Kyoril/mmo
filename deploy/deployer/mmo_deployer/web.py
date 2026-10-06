# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""Minimal HTTP helper on urllib: stdlib only, so the image needs no pip install."""

import base64
import json
import shutil
import urllib.error
import urllib.parse
import urllib.request


class HttpError(Exception):
	"""HTTP status >= 400. Connection failures surface as OSError instead."""

	def __init__(self, status, body, url):
		super().__init__("HTTP {} from {}: {}".format(status, url, body[:200]))
		self.status = status
		self.body = body


def basic_auth(user, password):
	token = base64.b64encode("{}:{}".format(user, password).encode("utf-8")).decode("ascii")
	return {"Authorization": "Basic " + token}


class Http:
	def __init__(self, timeout=60):
		self.timeout = timeout

	def _open(self, method, url, headers, body, secret_headers):
		request = urllib.request.Request(url, data=body, method=method)
		for key, value in (headers or {}).items():
			request.add_header(key, value)
		# Never forwarded on redirects: GitHub asset downloads redirect to a storage host
		# that rejects a second Authorization header.
		for key, value in (secret_headers or {}).items():
			request.add_unredirected_header(key, value)
		try:
			return urllib.request.urlopen(request, timeout=self.timeout)
		except urllib.error.HTTPError as error:
			raise HttpError(error.code, error.read().decode("utf-8", "replace"), url) from None

	def request(self, method, url, headers=None, body=None, secret_headers=None):
		"""Returns (status, body bytes); raises HttpError for status >= 400."""
		with self._open(method, url, headers, body, secret_headers) as response:
			return response.status, response.read()

	def get_json(self, url, headers=None, secret_headers=None):
		_, body = self.request("GET", url, headers, None, secret_headers)
		return json.loads(body.decode("utf-8"))

	def send_json(self, method, url, payload, headers=None, secret_headers=None):
		merged = dict(headers or {})
		merged["Content-Type"] = "application/json"
		_, body = self.request(method, url, merged, json.dumps(payload).encode("utf-8"), secret_headers)
		return json.loads(body.decode("utf-8")) if body else None

	def post_form(self, url, fields, headers=None, secret_headers=None):
		merged = dict(headers or {})
		merged["Content-Type"] = "application/x-www-form-urlencoded"
		body = urllib.parse.urlencode(fields).encode("ascii")
		_, data = self.request("POST", url, merged, body, secret_headers)
		return json.loads(data.decode("utf-8")) if data else None

	def download(self, url, dest, headers=None, secret_headers=None):
		with self._open("GET", url, headers, None, secret_headers) as response, open(dest, "wb") as out:
			shutil.copyfileobj(response, out, 1024 * 1024)
