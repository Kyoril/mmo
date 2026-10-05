// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "send_request.h"
#include "connection.h"
#include "base/macros.h"
#include "base/utilities.h"

#include <iomanip>
#include <sstream>


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

			std::string FormatRequestHead(const Request &request)
			{
				std::ostringstream head;
				head << request.method << " " << escapePath(request.document)
				     << (request.keepAlive ? " HTTP/1.1\r\n" : " HTTP/1.0\r\n");
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
				head << (request.keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n");
				head << "\r\n";
				return head.str();
			}

			https_client::Response sendRequest(
			    const std::string &host,
				uint16 port,
			    const Request &request,
				const RequestOptions &options
			)
			{
				Connection connection(host, port, options);
				return connection.Send(request);
			}
		}
	}
}
