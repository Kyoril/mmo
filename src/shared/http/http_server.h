// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "http_server_side.h"
#include "http_client.h"
#include "network/server.h"

namespace mmo
{
	namespace net
	{
		namespace http
		{
			typedef mmo::Server<mmo::net::http::Client> Server;
		}
	}
}
