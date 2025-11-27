// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "send_request.h"
#include "base/macros.h"
#include "base/utilities.h"

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

			https_client::Response sendRequest(
			    const std::string &host,
				uint16 port,
			    const Request &request
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

				asio::ip::tcp::resolver resolver(io_context);
				auto endpoints = resolver.resolve(host, std::to_string(port));
				asio::connect(ssl_stream->lowest_layer(), endpoints);
				ssl_stream->lowest_layer().set_option(asio::ip::tcp::no_delay(true));
				ssl_stream->handshake(asio::ssl::stream_base::client);

			std::ostringstream request_stream;
			request_stream << "GET " << escapePath(request.document) << " HTTP/1.0\r\n";
			request_stream << "Host: " << request.host << "\r\n";
			request_stream << "Accept: */*\r\n";
			
			// Add Range header if byte range is specified
			if (request.byteRange)
			{
				request_stream << "Range: bytes=" 
				              << request.byteRange->first 
				              << "-" 
				              << request.byteRange->second 
				              << "\r\n";
			}
			
			request_stream << "Connection: close\r\n";
			request_stream << "\r\n";				std::string request_str = request_stream.str();

				asio::write(*ssl_stream, asio::buffer(request_str));

				asio::streambuf response_buf;
				asio::read_until(*ssl_stream, response_buf, "\r\n");

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
				asio::read_until(*ssl_stream, response_buf, "\r\n\r\n");

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

				// Read until EOF or Content-Length
				if (bodySize)
				{
					std::size_t bytes_remaining = *bodySize - body.size();
					while (bytes_remaining > 0)
					{
						char buf[1024];
						std::size_t bytes_to_read = std::min(bytes_remaining, sizeof(buf));
						std::size_t bytes_read = ssl_stream->read_some(asio::buffer(buf, bytes_to_read));
						if (bytes_read == 0)
							break;
						body.append(buf, bytes_read);
						bytes_remaining -= bytes_read;
					}
				}
				else
				{
					asio::error_code ec;
					while (true)
					{
						char buf[1024];
						std::size_t bytes_read = ssl_stream->read_some(asio::buffer(buf), ec);
						if (ec == asio::error::eof)
							break;

						if (ec)
							throw asio::system_error(ec);
						body.append(buf, bytes_read);
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
