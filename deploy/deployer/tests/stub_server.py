# Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
"""A tiny threaded HTTP server for client and integration tests."""

import json
import threading
import urllib.parse
from collections import namedtuple
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class StubRequest(namedtuple("StubRequest", "method path query headers body")):
	def form(self):
		return {key: values[0] for key, values in urllib.parse.parse_qs(self.body.decode("ascii")).items()}

	def json(self):
		return json.loads(self.body.decode("utf-8"))


def _normalize(result):
	if len(result) == 2:
		status, payload = result
		return status, {}, payload
	return result


class StubServer:
	def __init__(self):
		self.routes = {}
		self.requests = []
		stub = self

		class Handler(BaseHTTPRequestHandler):
			def log_message(self, *args):
				pass

			def _handle(self):
				length = int(self.headers.get("Content-Length") or 0)
				body = self.rfile.read(length) if length else b""
				parsed = urllib.parse.urlsplit(self.path)
				request = StubRequest(
					self.command, parsed.path, urllib.parse.parse_qs(parsed.query),
					{key.lower(): value for key, value in self.headers.items()}, body)
				stub.requests.append(request)
				response = stub.routes.get((self.command, parsed.path))
				if response is None:
					status, headers, payload = 404, {}, {"status": "NOT_FOUND"}
				else:
					status, headers, payload = _normalize(response(request) if callable(response) else response)
				if isinstance(payload, bytes):
					data = payload
				elif payload is None:
					data = b""
				else:
					data = json.dumps(payload).encode("utf-8")
				self.send_response(status)
				for key, value in headers.items():
					self.send_header(key, value)
				self.send_header("Content-Length", str(len(data)))
				self.end_headers()
				self.wfile.write(data)

			do_GET = _handle
			do_POST = _handle
			do_PUT = _handle

		self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
		self.url = "http://127.0.0.1:{}".format(self.server.server_address[1])
		self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

	def route(self, method, path, response):
		self.routes[(method, path)] = response

	def find(self, method, path):
		return [r for r in self.requests if r.method == method and r.path == path]

	def __enter__(self):
		self.thread.start()
		return self

	def __exit__(self, *exc):
		self.server.shutdown()
		self.server.server_close()
