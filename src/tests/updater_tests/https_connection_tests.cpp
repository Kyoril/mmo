// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "https_client/connection.h"
#include "updater/https_update_source.h"
#include "updater/update_errors.h"

#include "asio.hpp"
#include "asio/ssl.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace mmo;

namespace
{
	/// A self-signed certificate for 127.0.0.1, valid until 2126. Test use only.
	const char* const TestCertificate =
		"-----BEGIN CERTIFICATE-----\n"
		"MIIBqTCCAU+gAwIBAgIURAb+qsKxvkqIteVDo9PMqaCQOacwCgYIKoZIzj0EAwIw\n"
		"GzEZMBcGA1UEAwwQbW1vIHVwZGF0ZXIgdGVzdDAgFw0yNjEwMDUxMjE1MTdaGA8y\n"
		"MTI2MDkxMTEyMTUxN1owGzEZMBcGA1UEAwwQbW1vIHVwZGF0ZXIgdGVzdDBZMBMG\n"
		"ByqGSM49AgEGCCqGSM49AwEHA0IABMrNmWnYqHX9iX7o5xSmWpNjgUfWXZaBeUp2\n"
		"gDsJbdLcKqeo+3V7eMyR0QsLIiKHTeOBd0NVY6/0N4eyXDfR/jejbzBtMB0GA1Ud\n"
		"DgQWBBSeA6bja+F1n0Z/HcU6MCK6UArw4DAfBgNVHSMEGDAWgBSeA6bja+F1n0Z/\n"
		"HcU6MCK6UArw4DAPBgNVHRMBAf8EBTADAQH/MBoGA1UdEQQTMBGHBH8AAAGCCWxv\n"
		"Y2FsaG9zdDAKBggqhkjOPQQDAgNIADBFAiEAsHJD0GusLtwQeGDVUQDBlvHXTqv+\n"
		"9vZc9dM5C0A/06YCIByC4qmF6gcy1QTkwic/m9EWY9iHTBaMIi8YpAA1vJOe\n"
		"-----END CERTIFICATE-----\n";

	const char* const TestPrivateKey =
		"-----BEGIN PRIVATE KEY-----\n"
		"MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQgMuFfwD0SNK+PK+iw\n"
		"2ZbMPyt6jyN6hXQvUF8XBOYWidWhRANCAATKzZlp2Kh1/Yl+6OcUplqTY4FH1l2W\n"
		"gXlKdoA7CW3S3CqnqPt1e3jMkdELCyIih03jgXdDVWOv9DeHslw30f43\n"
		"-----END PRIVATE KEY-----\n";

	/// What the server answers to one request.
	struct Reply
	{
		std::string data;
		/// Closes the connection after sending, without telling the client beforehand.
		bool closeAfter = false;
	};

	/// A loopback HTTPS server which answers every request through `respond`.
	class TlsServer final
	{
	public:
		typedef std::function<Reply(const std::string& requestHead)> Responder;

		explicit TlsServer(Responder respond)
			: m_context(asio::ssl::context::tls_server)
			, m_acceptor(m_io, asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0))
			, m_respond(std::move(respond))
		{
			m_context.use_certificate_chain(asio::buffer(std::string(TestCertificate)));
			m_context.use_private_key(asio::buffer(std::string(TestPrivateKey)), asio::ssl::context::pem);

			Accept();
			m_thread = std::thread([this] { m_io.run(); });
		}

		~TlsServer()
		{
			m_io.stop();
			m_thread.join();
		}

		uint16 GetPort() const { return m_acceptor.local_endpoint().port(); }
		uint32 GetConnections() const { return m_connections; }
		uint32 GetRequests() const { return m_requests; }

	private:
		struct Session : std::enable_shared_from_this<Session>
		{
			Session(asio::ip::tcp::socket socket, asio::ssl::context& context, TlsServer& server)
				: stream(std::move(socket), context)
				, server(server)
			{
			}

			void Start()
			{
				auto self = shared_from_this();
				stream.async_handshake(asio::ssl::stream_base::server, [self](const asio::error_code& ec)
				{
					if (!ec)
					{
						self->ReadRequest();
					}
				});
			}

			void ReadRequest()
			{
				auto self = shared_from_this();
				asio::async_read_until(stream, buffer, "\r\n\r\n", [self](const asio::error_code& ec, const std::size_t length)
				{
					if (ec)
					{
						return;
					}

					const auto begin = asio::buffers_begin(self->buffer.data());
					const std::string head(begin, begin + static_cast<std::ptrdiff_t>(length));
					self->buffer.consume(length);

					++self->server.m_requests;
					self->reply = self->server.m_respond(head);
					asio::async_write(self->stream, asio::buffer(self->reply.data), [self](const asio::error_code& writeError, std::size_t)
					{
						if (writeError)
						{
							return;
						}

						if (self->reply.closeAfter)
						{
							asio::error_code ignored;
							self->stream.lowest_layer().close(ignored);
							return;
						}

						self->ReadRequest();
					});
				});
			}

			asio::ssl::stream<asio::ip::tcp::socket> stream;
			asio::streambuf buffer;
			Reply reply;
			TlsServer& server;
		};

		void Accept()
		{
			m_acceptor.async_accept([this](const asio::error_code& ec, asio::ip::tcp::socket socket)
			{
				if (ec)
				{
					return;
				}

				++m_connections;
				std::make_shared<Session>(std::move(socket), m_context, *this)->Start();
				Accept();
			});
		}

		asio::io_context m_io;
		asio::ssl::context m_context;
		asio::ip::tcp::acceptor m_acceptor;
		Responder m_respond;
		std::atomic<uint32> m_connections{ 0 };
		std::atomic<uint32> m_requests{ 0 };
		std::thread m_thread;
	};

	/// A client context that trusts exactly the test certificate.
	std::shared_ptr<asio::ssl::context> MakeClientContext()
	{
		auto context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
		context->add_certificate_authority(asio::buffer(std::string(TestCertificate)));
		context->set_verify_mode(asio::ssl::verify_peer);
		return context;
	}

	/// Answers with the requested path as the body.
	Reply EchoPath(const std::string& head)
	{
		const auto pathBegin = head.find(' ') + 1;
		const std::string path = head.substr(pathBegin, head.find(' ', pathBegin) - pathBegin);
		return Reply{ "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(path.size()) + "\r\n\r\n" + path };
	}

	std::string ReadBody(net::https_client::Response& response)
	{
		std::ostringstream out;
		out << response.body->rdbuf();
		return out.str();
	}

	net::https_client::Request MakeRequest(const std::string& document)
	{
		net::https_client::Request request;
		request.document = document;
		request.keepAlive = true;
		return request;
	}
}

TEST_CASE("https Connection reuses one connection for several requests", "[https_client][network]")
{
	TlsServer server(EchoPath);
	net::https_client::Connection connection("127.0.0.1", server.GetPort(), {}, MakeClientContext());

	for (const char* path : { "/a", "/b/c", "/d" })
	{
		auto response = connection.Send(MakeRequest(path));
		CHECK(response.status == 200);
		CHECK(ReadBody(response) == path);
		CHECK(connection.IsOpen());
	}

	CHECK(connection.GetConnectCount() == 1);
	CHECK(server.GetConnections() == 1);
	CHECK(server.GetRequests() == 3);
}

TEST_CASE("https Connection decodes chunked bodies", "[https_client][network]")
{
	TlsServer server([](const std::string&)
	{
		return Reply{ "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
			"5\r\nhello\r\n" "1;ext=1\r\n \r\n" "5\r\nworld\r\n" "0\r\nX-Trailer: 1\r\n\r\n" };
	});
	net::https_client::Connection connection("127.0.0.1", server.GetPort(), {}, MakeClientContext());

	auto first = connection.Send(MakeRequest("/"));
	CHECK(ReadBody(first) == "hello world");
	REQUIRE(first.bodySize);
	CHECK(*first.bodySize == 11);

	// The connection must be positioned after the trailer, ready for the next response.
	auto second = connection.Send(MakeRequest("/"));
	CHECK(ReadBody(second) == "hello world");
	CHECK(connection.GetConnectCount() == 1);
}

TEST_CASE("https Connection reconnects when the server dropped an idle connection", "[https_client][network]")
{
	TlsServer server([](const std::string& head)
	{
		Reply reply = EchoPath(head);
		reply.closeAfter = true;
		return reply;
	});
	net::https_client::Connection connection("127.0.0.1", server.GetPort(), {}, MakeClientContext());

	auto first = connection.Send(MakeRequest("/first"));
	CHECK(ReadBody(first) == "/first");

	// Give the close time to arrive, as it would for a connection idling in a pool.
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	auto second = connection.Send(MakeRequest("/second"));
	CHECK(second.status == 200);
	CHECK(ReadBody(second) == "/second");
	CHECK(connection.GetConnectCount() == 2);
}

TEST_CASE("https Connection closes when the server asks for it", "[https_client][network]")
{
	TlsServer server([](const std::string&)
	{
		return Reply{ "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 2\r\n\r\nok" };
	});
	net::https_client::Connection connection("127.0.0.1", server.GetPort(), {}, MakeClientContext());

	auto response = connection.Send(MakeRequest("/"));
	CHECK(ReadBody(response) == "ok");
	CHECK_FALSE(connection.IsOpen());
}

TEST_CASE("https Connection reads a body without length up to the end of the connection", "[https_client][network]")
{
	TlsServer server([](const std::string& head)
	{
		CHECK(head.rfind("GET / HTTP/1.0\r\n", 0) == 0);
		CHECK(head.find("Connection: close\r\n") != std::string::npos);
		return Reply{ "HTTP/1.0 200 OK\r\n\r\nuntil the end", true };
	});
	net::https_client::Connection connection("127.0.0.1", server.GetPort(), {}, MakeClientContext());

	net::https_client::Request request;
	request.document = "/";

	auto response = connection.Send(request);
	CHECK(response.status == 200);
	CHECK(ReadBody(response) == "until the end");
	CHECK_FALSE(connection.IsOpen());
}

TEST_CASE("https Connection rejects a certificate it does not trust", "[https_client][network]")
{
	TlsServer server(EchoPath);

	// No trusted authority: the handshake has to fail.
	auto context = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
	net::https_client::Connection connection("127.0.0.1", server.GetPort(), {}, context);

	CHECK_THROWS(connection.Send(MakeRequest("/")));
}

TEST_CASE("HTTPSUpdateSource keeps connections alive across files", "[updater][network]")
{
	TlsServer server(EchoPath);
	updating::HTTPSUpdateSource source("127.0.0.1", server.GetPort(), "/patch", {}, MakeClientContext());

	SECTION("from a single thread")
	{
		for (int i = 0; i < 20; ++i)
		{
			const auto file = source.readFile("file" + std::to_string(i) + ".bin");
			std::ostringstream content;
			content << file.content->rdbuf();
			CHECK(content.str() == "/patch/file" + std::to_string(i) + ".bin");
		}

		CHECK(server.GetConnections() == 1);
		CHECK(server.GetRequests() == 20);
	}

	SECTION("from several threads at once")
	{
		constexpr int ThreadCount = 4;
		constexpr int FilesPerThread = 25;

		std::atomic<int> mismatches{ 0 };
		std::vector<std::thread> threads;
		for (int t = 0; t < ThreadCount; ++t)
		{
			threads.emplace_back([&source, &mismatches, t]
			{
				for (int i = 0; i < FilesPerThread; ++i)
				{
					const std::string name = std::to_string(t) + "/" + std::to_string(i);
					const auto file = source.readFile(name);
					std::ostringstream content;
					content << file.content->rdbuf();
					if (content.str() != "/patch/" + name)
					{
						++mismatches;
					}
				}
			});
		}

		for (std::thread& thread : threads)
		{
			thread.join();
		}

		CHECK(mismatches == 0);
		CHECK(server.GetRequests() == ThreadCount * FilesPerThread);
		CHECK(server.GetConnections() <= ThreadCount);
	}
}

TEST_CASE("HTTPSUpdateSource reports a missing file as permanent", "[updater][network]")
{
	TlsServer server([](const std::string&)
	{
		return Reply{ "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nnot found" };
	});
	updating::HTTPSUpdateSource source("127.0.0.1", server.GetPort(), "/", {}, MakeClientContext());

	CHECK_THROWS_AS(source.readFile("missing.bin"), updating::PermanentSourceError);

	// The error response was read completely, so the connection stays usable.
	CHECK_THROWS_AS(source.readFile("missing.bin"), updating::PermanentSourceError);
	CHECK(server.GetConnections() == 1);
}
