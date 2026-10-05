// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace mmo
{
	namespace net
	{
		namespace https_client
		{
			struct Request
			{
				std::string host;
				std::string document;
				/// Request method, e.g. "GET" or "POST".
				std::string method = "GET";
				/// Additional request headers (Host, Connection and Content-Length are written automatically).
				std::vector<std::pair<std::string, std::string>> headers;
				/// Request body. A Content-Length header is sent whenever the method is not GET.
				std::string body;
				/// Sends the request as HTTP/1.1 and asks the server to keep the connection
				/// open. Only meaningful with a Connection, which can reuse it.
				bool keepAlive = false;
			};
		}
	}
}
