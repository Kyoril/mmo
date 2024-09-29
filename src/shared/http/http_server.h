// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

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
