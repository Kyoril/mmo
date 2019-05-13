// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "send_request.h"
#include "base/macros.h"

#include <iomanip>
#include <sstream>
#include <cctype>

#include "base/utilities.h"

#include "asio.hpp"

namespace mmo
{
	namespace net
	{
		namespace http_client
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


			Response sendRequest(
			    const std::string &host,
				uint16 port,
			    const Request &request
			)
			{
				const auto connection = std::make_shared<asio::ip::tcp::iostream>();
				connection->connect(
				    host,
				    std::to_string(port));
				if (!*connection)
				{
					//.error() was introduced in Boost 1.47
					//throw asio::system_error(connection->error());
					throw std::runtime_error("Could not connect to " + host);
				}

				*connection << "GET " << escapePath(request.document) << " HTTP/1.0\r\n";
				*connection << "Host: " << request.host << "\r\n";
				*connection << "Accept: */*\r\n";
				*connection << "Connection: close\r\n";
				*connection << "\r\n";

				std::string responseVersion;
				unsigned status;
				std::string statusMessage;

				*connection
				        >> responseVersion
				        >> status;
				std::getline(*connection, statusMessage);

				std::map<std::string, std::string> headers;
				std::optional<std::uintmax_t> bodySize(0);
				bodySize.reset();

				{
					std::string line;
					while (std::getline(*connection, line) && (line != "\r"))
					{
						const auto endOfKey = std::find(line.begin(), line.end(), ':');
						auto beginOfValue = (endOfKey + 1);
						if (endOfKey == line.end() ||
						        beginOfValue == line.end())
						{
							throw std::runtime_error("Invalid HTTP response header");
						}

						if (*beginOfValue == ' ')
						{
							++beginOfValue;
						}

						auto endOfValue = line.end();
						if (endOfValue[-1] == '\r')
						{
							--endOfValue;
						}

						std::string key(line.begin(), endOfKey);
						std::string value(beginOfValue, endOfValue);

						static const std::string ContentLength = "Content-Length";

						if (_stricmp(key.c_str(), ContentLength.c_str()) == 0)
						{
							bodySize = std::atoll(value.c_str());
						}

						headers.insert(std::make_pair(
						                   std::move(key),
						                   std::move(value)
						               ));
					}
				}

				std::unique_ptr<std::istream> bodyStream(
					std::make_unique<std::istream>(
						connection->rdbuf()
						));

				Response response(
				    status,
				    bodySize,
				    std::move(bodyStream),
				    connection
				);
				response.headers = std::move(headers);
				return response;
			}
		}
	}
}
