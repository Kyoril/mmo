// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "connection.h"

#include "base/macros.h"

#include "asio.hpp"
#include "asio/ssl.hpp"

#include <algorithm>
#include <exception>
#include <cctype>
#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#	include <wincrypt.h>
#endif

namespace mmo
{
	namespace net
	{
		namespace https_client
		{
			namespace
			{
#ifdef _WIN32
				void AddWindowsRootCerts(asio::ssl::context& ctx)
				{
					HCERTSTORE hStore = CertOpenSystemStore(0, "ROOT");
					if (hStore == nullptr)
					{
						return;
					}

					X509_STORE* store = X509_STORE_new();
					PCCERT_CONTEXT pContext = nullptr;
					while ((pContext = CertEnumCertificatesInStore(hStore, pContext)) != nullptr)
					{
						X509* x509 = d2i_X509(nullptr,
							(const unsigned char**)&pContext->pbCertEncoded,
							pContext->cbCertEncoded);
						if (x509 != nullptr)
						{
							X509_STORE_add_cert(store, x509);
							X509_free(x509);
						}
					}

					CertFreeCertificateContext(pContext);
					CertCloseStore(hStore, 0);

					SSL_CTX_set_cert_store(ctx.native_handle(), store);
				}
#endif

				typedef std::function<void(const asio::error_code&, std::size_t)> CompletionHandler;

				bool EqualsIgnoreCase(const std::string& left, const std::string& right)
				{
					return left.size() == right.size() &&
						std::equal(left.begin(), left.end(), right.begin(), [](const char a, const char b)
						{
							return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
						});
				}

				/// Finds a header by name, ignoring case as HTTP requires.
				const std::string* FindHeader(const std::map<std::string, std::string>& headers, const std::string& name)
				{
					for (const auto& [key, value] : headers)
					{
						if (EqualsIgnoreCase(key, name))
						{
							return &value;
						}
					}

					return nullptr;
				}

				/// True for errors which mean the peer closed the connection.
				bool IsConnectionClosed(const asio::error_code& ec)
				{
					return ec == asio::error::eof
						|| ec == asio::error::connection_reset
						|| ec == asio::error::connection_aborted
						|| ec == asio::error::broken_pipe
						|| ec == asio::ssl::error::stream_truncated;
				}
			}

			std::shared_ptr<asio::ssl::context> GetDefaultContext()
			{
				static const std::shared_ptr<asio::ssl::context> context = []()
				{
					auto ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::sslv23_client);
#ifdef _WIN32
					AddWindowsRootCerts(*ctx);
#else
					ctx->set_default_verify_paths();
#endif
					ctx->set_verify_mode(asio::ssl::verify_peer);
					return ctx;
				}();

				return context;
			}

			struct Connection::Impl
			{
				typedef asio::ssl::stream<asio::ip::tcp::socket> Stream;

				std::string host;
				uint16 port = 0;
				RequestOptions options;
				std::shared_ptr<asio::ssl::context> context;

				asio::io_context io;
				std::unique_ptr<Stream> stream;

				/// Received but not yet consumed bytes. Reading up to a delimiter usually
				/// reads past it, and the rest belongs to whatever is parsed next.
				asio::streambuf buffer;

				bool open = false;
				uint32 connectCount = 0;

				/// Runs one asynchronous operation to completion and returns the number of
				/// bytes it transferred.
				///
				/// The operations are asynchronous only so that they can be bounded: blocking
				/// socket calls cannot be given a timeout portably, and a stalled connection
				/// would otherwise hang the caller forever. `start` begins the operation with
				/// the given handler; `abort` makes a pending operation complete early, which
				/// is what both the timeout and cancellation do.
				std::size_t Await(const std::function<void()>& abort, const std::function<void(CompletionHandler)>& start)
				{
					std::optional<asio::error_code> result;
					std::size_t transferred = 0;
					start([&result, &transferred](const asio::error_code& ec, const std::size_t bytes)
					{
						result = ec;
						transferred = bytes;
					});

					// The previous operation ran the context out of work, which stops it.
					io.restart();

					const auto startedAt = std::chrono::steady_clock::now();
					asio::error_code abortReason;

					// Even after aborting, the handler still has to run: it refers to the locals
					// above, so returning before it did would leave it dangling.
					while (!result)
					{
						if (!abortReason)
						{
							if (options.cancel && options.cancel->load())
							{
								abortReason = asio::error::operation_aborted;
							}
							else if (options.inactivityTimeout.count() > 0 &&
								std::chrono::steady_clock::now() - startedAt >= options.inactivityTimeout)
							{
								abortReason = asio::error::timed_out;
							}

							if (abortReason)
							{
								abort();
							}
						}

						io.run_one_for(std::chrono::milliseconds(50));
					}

					if (abortReason)
					{
						throw asio::system_error(abortReason);
					}

					if (*result)
					{
						throw asio::system_error(*result);
					}

					return transferred;
				}

				/// Await for an operation on the stream; aborting closes the socket.
				std::size_t AwaitStream(const std::function<void(CompletionHandler)>& start)
				{
					return Await([this] { CloseSocket(); }, start);
				}

				void CloseSocket()
				{
					if (stream)
					{
						asio::error_code ignored;
						stream->lowest_layer().close(ignored);
					}
				}

				void Close()
				{
					CloseSocket();
					stream.reset();
					buffer.consume(buffer.size());
					open = false;
				}

				void Connect()
				{
					Close();

					stream = std::make_unique<Stream>(io, *context);

					if (!SSL_set_tlsext_host_name(stream->native_handle(), host.c_str()))
					{
						asio::error_code ec(::ERR_get_error(), asio::error::get_ssl_category());
						throw asio::system_error(ec);
					}

					stream->set_verify_mode(asio::ssl::verify_peer);
					stream->set_verify_callback(asio::ssl::rfc2818_verification(host));

					asio::ip::tcp::resolver resolver(io);
					asio::ip::tcp::resolver::results_type endpoints;
					Await([&resolver] { resolver.cancel(); }, [&](CompletionHandler done)
					{
						resolver.async_resolve(host, std::to_string(port),
							[&endpoints, done](const asio::error_code& ec, asio::ip::tcp::resolver::results_type results)
							{
								endpoints = std::move(results);
								done(ec, 0);
							});
					});

					AwaitStream([&](CompletionHandler done)
					{
						asio::async_connect(stream->lowest_layer(), endpoints,
							[done](const asio::error_code& ec, const asio::ip::tcp::endpoint&)
							{
								done(ec, 0);
							});
					});

					stream->lowest_layer().set_option(asio::ip::tcp::no_delay(true));

					AwaitStream([&](CompletionHandler done)
					{
						stream->async_handshake(asio::ssl::stream_base::client, [done](const asio::error_code& ec)
						{
							done(ec, 0);
						});
					});

					open = true;
					++connectCount;
				}

				void Write(const std::string& data)
				{
					AwaitStream([&](CompletionHandler done)
					{
						asio::async_write(*stream, asio::buffer(data), done);
					});
				}

				/// Receives more data into the buffer.
				void ReadSome()
				{
					constexpr std::size_t ChunkSize = 16 * 1024;

					const std::size_t received = AwaitStream([&](CompletionHandler done)
					{
						stream->async_read_some(buffer.prepare(ChunkSize), done);
					});
					buffer.commit(received);
				}

				/// Reads one line and returns it without the line break.
				std::string ReadLine()
				{
					const std::size_t length = AwaitStream([&](CompletionHandler done)
					{
						asio::async_read_until(*stream, buffer, "\r\n", done);
					});

					const auto begin = asio::buffers_begin(buffer.data());
					std::string line(begin, begin + static_cast<std::ptrdiff_t>(length - 2));
					buffer.consume(length);
					return line;
				}

				void ReportBody(const std::size_t bytes) const
				{
					if (bytes > 0 && options.onBodyReceived)
					{
						options.onBodyReceived(bytes);
					}
				}

				/// Appends exactly `count` bytes to `out`.
				void ReadExactly(const std::uintmax_t count, std::string& out)
				{
					if (options.maxResponseBytes > 0 && (out.size() > options.maxResponseBytes || count > options.maxResponseBytes - out.size()))
					{
						throw std::runtime_error("HTTP response exceeds configured body limit");
					}
					const std::size_t target = out.size() + static_cast<std::size_t>(count);
					out.reserve(target);

					while (out.size() < target)
					{
						if (buffer.size() == 0)
						{
							ReadSome();
						}

						const std::size_t take = std::min(buffer.size(), target - out.size());
						const auto begin = asio::buffers_begin(buffer.data());
						out.append(begin, begin + static_cast<std::ptrdiff_t>(take));
						buffer.consume(take);
						ReportBody(take);
					}
				}

				/// Appends everything up to the end of the connection to `out`.
				void ReadToEnd(std::string& out)
				{
					for (;;)
					{
						const auto begin = asio::buffers_begin(buffer.data());
						if (options.maxResponseBytes > 0 && (out.size() > options.maxResponseBytes || buffer.size() > options.maxResponseBytes - out.size()))
						{
							throw std::runtime_error("HTTP response exceeds configured body limit");
						}
						const std::size_t take = buffer.size();
						out.append(begin, begin + static_cast<std::ptrdiff_t>(take));
						buffer.consume(take);
						ReportBody(take);

						try
						{
							ReadSome();
						}
						catch (const asio::system_error& ex)
						{
							if (ex.code() == asio::error::eof || ex.code() == asio::ssl::error::stream_truncated)
							{
								return;
							}

							throw;
						}
					}
				}

				/// Decodes a body sent with "Transfer-Encoding: chunked".
				void ReadChunked(std::string& out)
				{
					for (;;)
					{
						const std::string sizeLine = ReadLine();

						std::uintmax_t chunkSize = 0;
						std::size_t parsed = 0;
						for (const char c : sizeLine)
						{
							if (!std::isxdigit(static_cast<unsigned char>(c)))
							{
								// Chunk extensions after ';' are allowed and ignored.
								break;
							}

							chunkSize = chunkSize * 16 + static_cast<std::uintmax_t>(
								std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : std::tolower(static_cast<unsigned char>(c)) - 'a' + 10);
							++parsed;
						}

						if (parsed == 0 || parsed > 15)
						{
							throw std::runtime_error("Invalid chunk size in HTTP response");
						}

						if (chunkSize == 0)
						{
							// Skip the trailer section, which ends with an empty line.
							while (!ReadLine().empty())
							{
							}
							return;
						}

						ReadExactly(chunkSize, out);

						if (!ReadLine().empty())
						{
							throw std::runtime_error("Malformed chunk in HTTP response");
						}
					}
				}

				/// Reads one response to `request`. `responseStarted` is set once the first
				/// byte of it arrived; up to then, a failure on a reused connection just means
				/// the server had closed the connection while it was idle.
				Response ReadResponse(const Request& request, bool& responseStarted)
				{
					std::string version;
					unsigned status = 0;
					std::map<std::string, std::string> headers;

					// Interim 1xx responses carry no body and precede the real one.
					do
					{
						const std::string statusLine = ReadLine();
						responseStarted = true;

						std::istringstream statusStream(statusLine);
						statusStream >> version >> status;
						if (!statusStream || version.compare(0, 5, "HTTP/") != 0)
						{
							throw std::runtime_error("Invalid response");
						}

						headers.clear();
						for (std::string line = ReadLine(); !line.empty(); line = ReadLine())
						{
							const auto colon = line.find(':');
							if (colon == std::string::npos)
							{
								continue;
							}

							std::string value = line.substr(colon + 1);
							value.erase(0, value.find_first_not_of(" \t"));
							value.erase(value.find_last_not_of(" \t") + 1);
							headers[line.substr(0, colon)] = std::move(value);
						}
					} while (status >= 100 && status < 200 && status != 101);

					std::optional<std::uintmax_t> bodySize;
					if (const std::string* contentLength = FindHeader(headers, "Content-Length"))
					{
						try
						{
							bodySize = std::stoull(*contentLength);
						}
						catch (const std::exception&)
						{
							throw std::runtime_error("Invalid Content-Length in HTTP response");
						}
					}

					const std::string* transferEncoding = FindHeader(headers, "Transfer-Encoding");
					const bool chunked = transferEncoding && EqualsIgnoreCase(*transferEncoding, "chunked");
					const bool hasBody = request.method != "HEAD" && status != 204 && status != 304;

					// Without a length or chunking, the body ends where the connection does.
					bool bodyDelimited = true;

					std::string body;
					if (!hasBody)
					{
					}
					else if (chunked)
					{
						ReadChunked(body);
						bodySize = body.size();
					}
					else if (bodySize)
					{
						ReadExactly(*bodySize, body);
					}
					else
					{
						ReadToEnd(body);
						bodyDelimited = false;
					}

					const std::string* connectionHeader = FindHeader(headers, "Connection");
					const bool serverKeepsAlive = version == "HTTP/1.0"
						? (connectionHeader && EqualsIgnoreCase(*connectionHeader, "keep-alive"))
						: !(connectionHeader && EqualsIgnoreCase(*connectionHeader, "close"));

					if (!request.keepAlive || !bodyDelimited || !serverKeepsAlive)
					{
						Close();
					}

					Response response(status, bodySize, std::make_unique<std::istringstream>(std::move(body)), nullptr);
					response.headers = std::move(headers);
					return response;
				}
			};

			Connection::Connection(
				std::string host,
				const uint16 port,
				RequestOptions options,
				std::shared_ptr<asio::ssl::context> context)
				: m_impl(std::make_unique<Impl>())
			{
				m_impl->host = std::move(host);
				m_impl->port = port;
				m_impl->options = options;
				m_impl->context = context ? std::move(context) : GetDefaultContext();
			}

			Connection::~Connection()
			{
				m_impl->Close();
			}

			Response Connection::Send(Request request)
			{
				std::optional<Response> result;
				SendPipelined({ std::move(request) }, [&result](std::size_t, Response response)
				{
					result = std::move(response);
				});

				// SendPipelined throws unless at least one response arrived.
				ASSERT(result);
				return std::move(*result);
			}

			std::size_t Connection::SendPipelined(std::vector<Request> requests, const ResponseHandler& onResponse)
			{
				if (requests.empty())
				{
					return 0;
				}

				std::string data;
				for (Request& request : requests)
				{
					if (request.host.empty())
					{
						request.host = m_impl->host;
					}

					data += FormatRequestHead(request);
					data += request.body;
				}

				std::size_t received = 0;

				// Exceptions from the handler are the caller's own and always passed on;
				// only network errors after the first response are absorbed below.
				std::exception_ptr handlerError;

				for (bool firstTry = true; ; firstTry = false)
				{
					const bool reused = m_impl->open;
					if (!reused)
					{
						m_impl->Connect();
					}

					bool responseStarted = false;
					try
					{
						// All requests go out at once. They are small, so this cannot fill the
						// socket buffers while the server is already sending responses back.
						m_impl->Write(data);

						while (received < requests.size())
						{
							responseStarted = false;
							Response response = m_impl->ReadResponse(requests[received], responseStarted);
							++received;

							try
							{
								onResponse(received - 1, std::move(response));
							}
							catch (...)
							{
								handlerError = std::current_exception();
								throw;
							}

							// The server closed the connection after this response, e.g. because
							// it limits the requests per connection. Whatever was pipelined
							// behind it is lost and is the caller's to send again.
							if (!m_impl->open)
							{
								break;
							}
						}

						return received;
					}
					catch (const asio::system_error& ex)
					{
						m_impl->Close();

						if (received > 0)
						{
							return received;
						}

						// A server may close an idle keep-alive connection at any time, and we
						// only learn about it when using it. Resend once on a fresh connection.
						if (firstTry && reused && !responseStarted && IsConnectionClosed(ex.code()))
						{
							continue;
						}

						throw;
					}
					catch (...)
					{
						m_impl->Close();

						if (received > 0 && !handlerError)
						{
							return received;
						}

						throw;
					}
				}
			}

			bool Connection::IsOpen() const
			{
				return m_impl->open;
			}

			void Connection::Close()
			{
				m_impl->Close();
			}

			uint32 Connection::GetConnectCount() const
			{
				return m_impl->connectCount;
			}
		}
	}
}
