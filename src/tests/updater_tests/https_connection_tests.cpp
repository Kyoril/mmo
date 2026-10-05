// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include "https_client/connection.h"
#include "updater/https_update_source.h"
#include "updater/update_errors.h"

#include "asio.hpp"
#include "asio/ssl.hpp"

#include <atomic>
#include <chrono>
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

		/// Requests which arrived while an earlier one was still unanswered.
		uint32 GetPipelinedRequests() const { return m_pipelinedRequests; }

		/// Silently drops each connection after `count` requests, like a server limiting
		/// requests per keep-alive connection. Whatever was pipelined behind is lost.
		void SetCloseEvery(const uint32 count) { m_closeEvery = count; }

		/// Delays every request that had to travel over the network, like a round trip.
		void SetLatency(const std::chrono::milliseconds latency) { m_latencyMs = latency.count(); }

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

			bool HasBufferedRequest() const
			{
				const auto data = buffer.data();
				const std::string pending(asio::buffers_begin(data), asio::buffers_end(data));
				return pending.find("\r\n\r\n") != std::string::npos;
			}

			void ReadRequest()
			{
				// Data that has to come in over the network first pays the simulated
				// latency; requests which arrived together with an earlier one do not.
				const bool mustWait = !HasBufferedRequest();

				auto self = shared_from_this();
				asio::async_read_until(stream, buffer, "\r\n\r\n", [self, mustWait](const asio::error_code& ec, const std::size_t length)
				{
					if (ec)
					{
						return;
					}

					const std::chrono::milliseconds latency(self->server.m_latencyMs.load());
					if (mustWait && latency.count() > 0)
					{
						auto timer = std::make_shared<asio::steady_timer>(self->stream.get_executor(), latency);
						timer->async_wait([self, timer, length](const asio::error_code&)
						{
							self->HandleRequest(length);
						});
						return;
					}

					self->HandleRequest(length);
				});
			}

			void HandleRequest(const std::size_t length)
			{
				const auto begin = asio::buffers_begin(buffer.data());
				const std::string head(begin, begin + static_cast<std::ptrdiff_t>(length));
				buffer.consume(length);

				if (HasBufferedRequest())
				{
					++server.m_pipelinedRequests;
				}

				++server.m_requests;
				++requestsOnConnection;
				reply = server.m_respond(head);

				const uint32 closeEvery = server.m_closeEvery;
				if (closeEvery > 0 && requestsOnConnection % closeEvery == 0)
				{
					reply.closeAfter = true;
				}

				auto self = shared_from_this();
				asio::async_write(stream, asio::buffer(reply.data), [self](const asio::error_code& writeError, std::size_t)
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
			}

			uint32 requestsOnConnection = 0;
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
		std::atomic<uint32> m_pipelinedRequests{ 0 };
		std::atomic<uint32> m_closeEvery{ 0 };
		std::atomic<int64> m_latencyMs{ 0 };
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

TEST_CASE("https Connection pipelines requests and keeps the responses in order", "[https_client][network]")
{
	TlsServer server(EchoPath);
	net::https_client::Connection connection("127.0.0.1", server.GetPort(), {}, MakeClientContext());

	std::vector<net::https_client::Request> requests;
	for (int i = 0; i < 10; ++i)
	{
		requests.push_back(MakeRequest("/file" + std::to_string(i)));
	}

	std::vector<std::string> bodies;
	const size_t received = connection.SendPipelined(requests, [&](const size_t index, net::https_client::Response response)
	{
		CHECK(index == bodies.size());
		bodies.push_back(ReadBody(response));
	});

	REQUIRE(received == 10);
	for (int i = 0; i < 10; ++i)
	{
		CHECK(bodies[i] == "/file" + std::to_string(i));
	}

	CHECK(connection.IsOpen());
	CHECK(connection.GetConnectCount() == 1);
	CHECK(server.GetPipelinedRequests() > 0);
}

TEST_CASE("https Connection reports how far a pipeline got before the server closed", "[https_client][network]")
{
	TlsServer server(EchoPath);
	server.SetCloseEvery(3);
	net::https_client::Connection connection("127.0.0.1", server.GetPort(), {}, MakeClientContext());

	std::vector<net::https_client::Request> requests;
	for (int i = 0; i < 8; ++i)
	{
		requests.push_back(MakeRequest("/file" + std::to_string(i)));
	}

	std::vector<std::string> bodies;
	const size_t received = connection.SendPipelined(requests, [&](size_t, net::https_client::Response response)
	{
		bodies.push_back(ReadBody(response));
	});

	CHECK(received == 3);
	CHECK(bodies == std::vector<std::string>{ "/file0", "/file1", "/file2" });
	CHECK_FALSE(connection.IsOpen());

	// The connection is usable again for the rest.
	auto response = connection.Send(MakeRequest("/file3"));
	CHECK(ReadBody(response) == "/file3");
}

namespace
{
	std::vector<updating::RemoteFile> MakeFiles(const int count)
	{
		std::vector<updating::RemoteFile> files;
		for (int i = 0; i < count; ++i)
		{
			files.push_back(updating::RemoteFile{ "dir/file" + std::to_string(i), 32 });
		}
		return files;
	}

	std::string ReadFileContent(updating::IUpdateSource& source, const std::string& path)
	{
		const auto file = source.readFile(path);
		std::ostringstream content;
		content << file.content->rdbuf();
		return content.str();
	}

	updating::SourceOptions MakePrefetchOptions()
	{
		updating::SourceOptions options;
		options.prefetchConnections = 2;
		options.pipelineDepth = 8;
		return options;
	}
}

TEST_CASE("HTTPSUpdateSource prefetches announced files with pipelining", "[updater][network]")
{
	TlsServer server(EchoPath);
	updating::HTTPSUpdateSource source("127.0.0.1", server.GetPort(), "/patch", MakePrefetchOptions(), MakeClientContext());

	const auto files = MakeFiles(100);
	source.prefetch(files);

	for (const auto& file : files)
	{
		CHECK(ReadFileContent(source, file.path) == "/patch/" + file.path);
	}

	// Every file was requested exactly once, whether prefetched or claimed by the reader.
	CHECK(server.GetRequests() == 100);
	CHECK(server.GetPipelinedRequests() > 0);
}

TEST_CASE("HTTPSUpdateSource holds new prefetch batches during pause and resumes them", "[updater][network]")
{
	TlsServer server(EchoPath);
	std::atomic<bool> paused{true};
	auto options = MakePrefetchOptions();
	options.pause = &paused;
	updating::HTTPSUpdateSource source("127.0.0.1", server.GetPort(), "/patch", options, MakeClientContext());
	const auto files = MakeFiles(16);
	source.prefetch(files);
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	CHECK(server.GetRequests() == 0);
	paused = false;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	while (server.GetRequests() < files.size() && std::chrono::steady_clock::now() < deadline)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	REQUIRE(server.GetRequests() == files.size());
	for (const auto& file : files)
	{
		CHECK(ReadFileContent(source, file.path) == "/patch/" + file.path);
	}
	CHECK(server.GetRequests() == files.size());
}

TEST_CASE("HTTPSUpdateSource recovers when the server drops pipelined requests", "[updater][network]")
{
	TlsServer server(EchoPath);
	server.SetCloseEvery(5);
	updating::HTTPSUpdateSource source("127.0.0.1", server.GetPort(), "/patch", MakePrefetchOptions(), MakeClientContext());

	const auto files = MakeFiles(60);
	source.prefetch(files);

	std::vector<std::thread> readers;
	std::atomic<int> mismatches{ 0 };
	for (int t = 0; t < 3; ++t)
	{
		readers.emplace_back([&, t]
		{
			for (size_t i = static_cast<size_t>(t); i < files.size(); i += 3)
			{
				if (ReadFileContent(source, files[i].path) != "/patch/" + files[i].path)
				{
					++mismatches;
				}
			}
		});
	}

	for (std::thread& reader : readers)
	{
		reader.join();
	}

	CHECK(mismatches == 0);
}

TEST_CASE("HTTPSUpdateSource reports errors of prefetched files", "[updater][network]")
{
	TlsServer server([](const std::string& head)
	{
		if (head.find("missing") != std::string::npos)
		{
			return Reply{ "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n" };
		}
		return EchoPath(head);
	});
	updating::HTTPSUpdateSource source("127.0.0.1", server.GetPort(), "/", MakePrefetchOptions(), MakeClientContext());

	source.prefetch({ { "a", 8 }, { "missing", 8 }, { "b", 8 } });

	CHECK(ReadFileContent(source, "a") == "/a");
	CHECK_THROWS_AS(source.readFile("missing"), updating::PermanentSourceError);
	CHECK(ReadFileContent(source, "b") == "/b");
}

TEST_CASE("HTTPSUpdateSource does not stall when files are read out of order", "[updater][network]")
{
	TlsServer server(EchoPath);

	// Room for very few files, so the prefetch fills up with files read last.
	auto options = MakePrefetchOptions();
	options.prefetchBufferLimit = 64;
	updating::HTTPSUpdateSource source("127.0.0.1", server.GetPort(), "/", options, MakeClientContext());

	const auto files = MakeFiles(40);
	source.prefetch(files);

	for (auto it = files.rbegin(); it != files.rend(); ++it)
	{
		CHECK(ReadFileContent(source, it->path) == "/" + it->path);
	}
}

TEST_CASE("HTTPSUpdateSource skips large files when prefetching", "[updater][network]")
{
	TlsServer server(EchoPath);
	updating::HTTPSUpdateSource source("127.0.0.1", server.GetPort(), "/", MakePrefetchOptions(), MakeClientContext());

	source.prefetch({ { "huge", 100 * 1024 * 1024 } });
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	CHECK(server.GetRequests() == 0);
	CHECK(ReadFileContent(source, "huge") == "/huge");
}

TEST_CASE("HTTPSUpdateSource stops prefetching when destroyed", "[updater][network]")
{
	TlsServer server(EchoPath);
	server.SetLatency(std::chrono::milliseconds(200));

	const auto startedAt = std::chrono::steady_clock::now();
	{
		updating::HTTPSUpdateSource source("127.0.0.1", server.GetPort(), "/", MakePrefetchOptions(), MakeClientContext());
		source.prefetch(MakeFiles(1000));
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	CHECK(std::chrono::steady_clock::now() - startedAt < std::chrono::seconds(2));
	CHECK(server.GetRequests() < 1000);
}

TEST_CASE("Pipelining benchmark", "[.bench]")
{
	TlsServer server(EchoPath);
	server.SetLatency(std::chrono::milliseconds(20));
	const auto files = MakeFiles(400);

	const auto measure = [&](const uint32 prefetchConnections)
	{
		updating::SourceOptions options;
		options.prefetchConnections = prefetchConnections;
		updating::HTTPSUpdateSource source("127.0.0.1", server.GetPort(), "/", options, MakeClientContext());
		source.prefetch(files);

		const auto startedAt = std::chrono::steady_clock::now();
		std::vector<std::thread> readers;
		for (int t = 0; t < 8; ++t)
		{
			readers.emplace_back([&, t]
			{
				for (size_t i = static_cast<size_t>(t); i < files.size(); i += 8)
				{
					ReadFileContent(source, files[i].path);
				}
			});
		}
		for (std::thread& reader : readers)
		{
			reader.join();
		}
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt).count();
	};

	const double plain = measure(0);
	const double pipelined = measure(4);
	WARN("400 files, 20 ms latency, 8 readers. Without pipelining (s): " << plain);
	WARN("With 4 pipelined prefetch connections (s): " << pipelined);
}
