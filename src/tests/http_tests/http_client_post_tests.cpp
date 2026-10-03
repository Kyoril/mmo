// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "http_client/send_request.h"

#include "asio.hpp"

#include <string>
#include <thread>

using namespace mmo;

TEST_CASE("FormatRequestHead keeps plain GET requests unchanged", "[http_client]")
{
	net::http_client::Request request;
	request.host = "example.test";
	request.document = "/api/x";

	CHECK(net::http_client::FormatRequestHead(request) ==
		"GET /api/x HTTP/1.0\r\nHost: example.test\r\nAccept: */*\r\nConnection: close\r\n\r\n");
}

TEST_CASE("FormatRequestHead writes method, custom headers and content length", "[http_client]")
{
	net::http_client::Request request;
	request.host = "example.test";
	request.document = "/api/bugs";
	request.method = "POST";
	request.headers.emplace_back("X-Api-Key", "secret");
	request.headers.emplace_back("Content-Type", "application/json");
	request.body = "{\"a\":1}";

	CHECK(net::http_client::FormatRequestHead(request) ==
		"POST /api/bugs HTTP/1.0\r\n"
		"Host: example.test\r\n"
		"X-Api-Key: secret\r\n"
		"Content-Type: application/json\r\n"
		"Accept: */*\r\n"
		"Content-Length: 7\r\n"
		"Connection: close\r\n"
		"\r\n");
}

TEST_CASE("http_client POSTs a body to a loopback server", "[http_client]")
{
	asio::io_context io;
	asio::ip::tcp::acceptor acceptor(io, asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0));
	const uint16 port = acceptor.local_endpoint().port();

	std::string received;
	std::thread server([&]()
	{
		asio::ip::tcp::socket socket(io);
		acceptor.accept(socket);

		// Read until the announced body has fully arrived.
		asio::error_code ec;
		std::string data;
		char buf[1024];
		while (!ec)
		{
			const size_t n = socket.read_some(asio::buffer(buf), ec);
			data.append(buf, n);
			const auto headerEnd = data.find("\r\n\r\n");
			if (headerEnd != std::string::npos)
			{
				const auto lengthPos = data.find("Content-Length: ");
				const size_t length = std::stoul(data.substr(lengthPos + 16));
				if (data.size() >= headerEnd + 4 + length)
				{
					break;
				}
			}
		}
		received = data;

		const std::string answer = "HTTP/1.0 201 Created\r\nContent-Length: 2\r\n\r\nok";
		asio::write(socket, asio::buffer(answer), ec);
		socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
	});

	net::http_client::Request request;
	request.host = "127.0.0.1";
	request.document = "/api/bugs";
	request.method = "POST";
	request.headers.emplace_back("X-Api-Key", "secret");
	request.body = "{\"comment\":\"hello\"}";

	const auto response = net::http_client::sendRequest("127.0.0.1", port, request);
	server.join();

	CHECK(response.status == 201);
	CHECK(received.rfind("POST /api/bugs HTTP/1.0\r\n", 0) == 0);
	CHECK(received.find("X-Api-Key: secret\r\n") != std::string::npos);
	CHECK(received.substr(received.size() - request.body.size()) == request.body);
}
