// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "updater/http_update_source.h"
#include "updater/https_update_source.h"
#include "updater/update_errors.h"

#include "asio.hpp"

#include <atomic>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>

using namespace mmo;
using namespace mmo::updating;

namespace
{
	/// A loopback server that accepts one connection and hands it to `serve`.
	class LoopbackServer final
	{
	public:
		template <typename Serve>
		explicit LoopbackServer(Serve serve)
			: m_acceptor(m_io, asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0))
			, m_port(m_acceptor.local_endpoint().port())
		{
			m_thread = std::thread([this, serve]()
			{
				asio::ip::tcp::socket socket(m_io);
				asio::error_code ec;
				m_acceptor.accept(socket, ec);
				if (!ec)
				{
					serve(socket);
				}
			});
		}

		~LoopbackServer()
		{
			m_thread.join();
		}

		uint16 GetPort() const { return m_port; }

	private:
		asio::io_context m_io;
		asio::ip::tcp::acceptor m_acceptor;
		uint16 m_port;
		std::thread m_thread;
	};

	/// Reads the request head so that the client is not reset while still writing it.
	void ReadRequestHead(asio::ip::tcp::socket& socket)
	{
		asio::streambuf buffer;
		asio::error_code ec;
		asio::read_until(socket, buffer, "\r\n\r\n", ec);
	}

	/// Keeps the connection open without ever answering, until the client gives up.
	void StaySilent(asio::ip::tcp::socket& socket)
	{
		asio::error_code ec;
		char byte = 0;
		while (!ec)
		{
			socket.read_some(asio::buffer(&byte, 1), ec);
		}
	}
}

TEST_CASE("HTTPUpdateSource returns the complete body", "[updater][network]")
{
	LoopbackServer server([](asio::ip::tcp::socket& socket)
	{
		ReadRequestHead(socket);

		asio::error_code ec;
		const std::string answer = "HTTP/1.0 200 OK\r\nContent-Length: 11\r\n\r\nhello world";
		asio::write(socket, asio::buffer(answer), ec);
		socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
	});

	HTTPUpdateSource source("127.0.0.1", server.GetPort(), "/");
	const auto file = source.readFile("file.txt");

	REQUIRE(file.size);
	CHECK(*file.size == 11);

	std::ostringstream content;
	content << file.content->rdbuf();
	CHECK(content.str() == "hello world");
}

TEST_CASE("HTTPUpdateSource fails a body cut off by the server", "[updater][network]")
{
	LoopbackServer server([](asio::ip::tcp::socket& socket)
	{
		ReadRequestHead(socket);

		asio::error_code ec;
		const std::string answer = "HTTP/1.0 200 OK\r\nContent-Length: 1000\r\n\r\nonly a part";
		asio::write(socket, asio::buffer(answer), ec);
		socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
	});

	HTTPUpdateSource source("127.0.0.1", server.GetPort(), "/");

	// It must fail inside readFile, where a retry can catch it, rather than later when
	// the caller writes the truncated content to disk.
	CHECK_THROWS_AS(source.readFile("file.txt"), std::runtime_error);
}

TEST_CASE("HTTPUpdateSource reports a missing file as permanent", "[updater][network]")
{
	LoopbackServer server([](asio::ip::tcp::socket& socket)
	{
		ReadRequestHead(socket);

		asio::error_code ec;
		const std::string answer = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n";
		asio::write(socket, asio::buffer(answer), ec);
		socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
	});

	HTTPUpdateSource source("127.0.0.1", server.GetPort(), "/");
	CHECK_THROWS_AS(source.readFile("file.txt"), PermanentSourceError);
}

TEST_CASE("HTTPUpdateSource reports a server error as temporary", "[updater][network]")
{
	LoopbackServer server([](asio::ip::tcp::socket& socket)
	{
		ReadRequestHead(socket);

		asio::error_code ec;
		const std::string answer = "HTTP/1.0 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n";
		asio::write(socket, asio::buffer(answer), ec);
		socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
	});

	HTTPUpdateSource source("127.0.0.1", server.GetPort(), "/");

	bool permanent = false;
	bool thrown = false;
	try
	{
		source.readFile("file.txt");
	}
	catch (const PermanentSourceError&)
	{
		permanent = true;
	}
	catch (const std::runtime_error&)
	{
		thrown = true;
	}

	CHECK(thrown);
	CHECK_FALSE(permanent);
}

TEST_CASE("HTTPSUpdateSource gives up on a server that stops responding", "[updater][network]")
{
	LoopbackServer server(StaySilent);

	SourceOptions options;
	options.inactivityTimeout = std::chrono::milliseconds(200);

	HTTPSUpdateSource source("127.0.0.1", server.GetPort(), "/", options);

	const auto startedAt = std::chrono::steady_clock::now();
	CHECK_THROWS_AS(source.readFile("file.txt"), std::runtime_error);
	CHECK(std::chrono::steady_clock::now() - startedAt < std::chrono::seconds(5));
}

TEST_CASE("HTTPSUpdateSource aborts a stalled transfer when cancelled", "[updater][network]")
{
	LoopbackServer server(StaySilent);

	std::atomic<bool> cancel{ false };
	SourceOptions options;
	options.cancel = &cancel;

	HTTPSUpdateSource source("127.0.0.1", server.GetPort(), "/", options);

	std::thread canceller([&cancel]
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		cancel = true;
	});

	const auto startedAt = std::chrono::steady_clock::now();
	CHECK_THROWS_AS(source.readFile("file.txt"), UpdateCancelled);
	CHECK(std::chrono::steady_clock::now() - startedAt < std::chrono::seconds(5));

	canceller.join();
}
