// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "send_request.h"
#include "base/macros.h"
#include "base/utilities.h"

#include <array>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <memory>
#include <map>
#include <optional>

#include "asio.hpp"
#include "asio/ssl.hpp"

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
				std::string escapePath(const std::string &path)
				{
					std::ostringstream escaped;
					for (const char c : path)
					{
						if (std::isgraph(c) && (c != '%'))
						{
							escaped << c;
						}
						else
						{
							escaped
							        << '%'
							        << std::hex
							        << std::setw(2)
							        << std::setfill('0')
							        << static_cast<unsigned>(c);
						}
					}
					return escaped.str();
				}
			}

#ifdef _WIN32
			namespace
			{
				void add_windows_root_certs(asio::ssl::context& ctx)
				{
					HCERTSTORE hStore = CertOpenSystemStore(0, "ROOT");
					if (hStore == nullptr) {
						return;
					}

					X509_STORE* store = X509_STORE_new();
					PCCERT_CONTEXT pContext = nullptr;
					while ((pContext = CertEnumCertificatesInStore(hStore, pContext)) != nullptr) {
						X509* x509 = d2i_X509(nullptr,
							(const unsigned char**)&pContext->pbCertEncoded,
							pContext->cbCertEncoded);
						if (x509 != nullptr) {
							X509_STORE_add_cert(store, x509);
							X509_free(x509);
						}
					}

					CertFreeCertificateContext(pContext);
					CertCloseStore(hStore, 0);

					SSL_CTX_set_cert_store(ctx.native_handle(), store);
				}
			}
#endif

			std::string FormatRequestHead(const Request &request)
			{
				std::ostringstream head;
				head << request.method << " " << escapePath(request.document) << " HTTP/1.0\r\n";
				head << "Host: " << request.host << "\r\n";
				bool hasAccept = false;
				for (const auto& [name, value] : request.headers)
				{
					if (name == "Accept")
					{
						hasAccept = true;
					}
					head << name << ": " << value << "\r\n";
				}
				if (!hasAccept)
				{
					head << "Accept: */*\r\n";
				}
				if (request.method != "GET")
				{
					head << "Content-Length: " << request.body.size() << "\r\n";
				}
				head << "Connection: close\r\n";
				head << "\r\n";
				return head.str();
			}

			namespace
			{
				typedef std::function<void(const asio::error_code&, std::size_t)> CompletionHandler;

				/// Runs one asynchronous operation to completion on `io` and returns the number
				/// of bytes it transferred.
				///
				/// The operations are asynchronous only so that they can be bounded: blocking
				/// socket calls cannot be given a timeout portably, and a stalled connection
				/// would otherwise hang the caller forever. `start` begins the operation and
				/// hands the given handler to it; `abort` makes a pending operation complete
				/// early, which is what both the timeout and cancellation do.
				std::size_t Await(
					asio::io_context& io,
					const RequestOptions& options,
					const std::function<void()>& abort,
					const std::function<void(CompletionHandler)>& start)
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
			}

			https_client::Response sendRequest(
			    const std::string &host,
				uint16 port,
			    const Request &request,
				const RequestOptions &options
			)
			{
				asio::io_context io_context;
				asio::ssl::context ssl_context(asio::ssl::context::sslv23_client);

				// Optionally set verification options
#ifdef _WIN32
				add_windows_root_certs(ssl_context);
#else
				ssl_context.set_default_verify_paths();
#endif
				ssl_context.set_verify_mode(asio::ssl::verify_peer);
				ssl_context.set_verify_callback(asio::ssl::rfc2818_verification(host));

				// Create SSL stream
				auto ssl_stream = std::make_shared<asio::ssl::stream<asio::ip::tcp::socket>>(io_context, ssl_context);

				if (!SSL_set_tlsext_host_name(ssl_stream->native_handle(), host.c_str()))
				{
					asio::error_code ec(::ERR_get_error(), asio::error::get_ssl_category());
					throw asio::system_error(ec);
				}

				// Closing the socket completes whatever is pending on the stream with an error.
				const auto closeSocket = [&ssl_stream]()
				{
					asio::error_code ignored;
					ssl_stream->lowest_layer().close(ignored);
				};

				asio::ip::tcp::resolver resolver(io_context);
				asio::ip::tcp::resolver::results_type endpoints;
				Await(io_context, options, [&resolver] { resolver.cancel(); }, [&](CompletionHandler done)
				{
					resolver.async_resolve(host, std::to_string(port),
						[&endpoints, done](const asio::error_code& ec, asio::ip::tcp::resolver::results_type results)
						{
							endpoints = std::move(results);
							done(ec, 0);
						});
				});

				Await(io_context, options, closeSocket, [&](CompletionHandler done)
				{
					asio::async_connect(ssl_stream->lowest_layer(), endpoints,
						[done](const asio::error_code& ec, const asio::ip::tcp::endpoint&)
						{
							done(ec, 0);
						});
				});

				ssl_stream->lowest_layer().set_option(asio::ip::tcp::no_delay(true));

				Await(io_context, options, closeSocket, [&](CompletionHandler done)
				{
					ssl_stream->async_handshake(asio::ssl::stream_base::client, [done](const asio::error_code& ec)
					{
						done(ec, 0);
					});
				});

				const std::string request_str = FormatRequestHead(request) + request.body;

				Await(io_context, options, closeSocket, [&](CompletionHandler done)
				{
					asio::async_write(*ssl_stream, asio::buffer(request_str), done);
				});

				asio::streambuf response_buf;
				Await(io_context, options, closeSocket, [&](CompletionHandler done)
				{
					asio::async_read_until(*ssl_stream, response_buf, "\r\n", done);
				});

				std::istream response_stream(&response_buf);
				std::string response_version;
				unsigned int status_code;
				std::string status_message;

				response_stream >> response_version >> status_code;
				std::getline(response_stream, status_message);

				if (!response_stream || response_version.substr(0, 5) != "HTTP/")
				{
					throw std::runtime_error("Invalid response");
				}

				std::map<std::string, std::string> headers;
				Await(io_context, options, closeSocket, [&](CompletionHandler done)
				{
					asio::async_read_until(*ssl_stream, response_buf, "\r\n\r\n", done);
				});

				std::string header_line;
				while (std::getline(response_stream, header_line) && header_line != "\r")
				{
					auto colon_pos = header_line.find(':');
					if (colon_pos != std::string::npos)
					{
						std::string header_name = header_line.substr(0, colon_pos);
						std::string header_value = header_line.substr(colon_pos + 1);
						// Trim whitespace
						header_value.erase(0, header_value.find_first_not_of(" \t"));
						header_value.erase(header_value.find_last_not_of(" \t\r") + 1);
						headers[header_name] = header_value;
					}
				}

				std::optional<std::uintmax_t> bodySize;
				auto it = headers.find("Content-Length");
				if (it != headers.end())
				{
					bodySize = std::stoull(it->second);
				}

				std::string body;

				// Read any remaining data in the buffer
				if (response_buf.size() > 0)
				{
					body.append(std::istreambuf_iterator<char>(response_stream), std::istreambuf_iterator<char>());
				}

				std::array<char, 16 * 1024> buf;
				const auto readSome = [&](const std::size_t maxBytes)
				{
					return Await(io_context, options, closeSocket, [&](CompletionHandler done)
					{
						ssl_stream->async_read_some(asio::buffer(buf.data(), maxBytes), done);
					});
				};

				// Read until EOF or Content-Length
				if (bodySize)
				{
					// A connection dropped early surfaces as an error from the read, never as a
					// silently truncated body.
					while (body.size() < *bodySize)
					{
						const std::size_t bytes_remaining = static_cast<std::size_t>(*bodySize - body.size());
						const std::size_t bytes_read = readSome(std::min(bytes_remaining, buf.size()));
						body.append(buf.data(), bytes_read);
					}
				}
				else
				{
					for (;;)
					{
						try
						{
							const std::size_t bytes_read = readSome(buf.size());
							body.append(buf.data(), bytes_read);
						}
						catch (const asio::system_error& ex)
						{
							if (ex.code() == asio::error::eof)
							{
								break;
							}

							throw;
						}
					}
				}

				std::unique_ptr<std::istream> bodyStream = std::make_unique<std::istringstream>(body);

				Response response(
					status_code,
					bodySize,
					std::move(bodyStream),
					nullptr //ssl_stream // Keep the SSL stream alive
				);
				response.headers = std::move(headers);
				return response;
			}
		}
	}
}
