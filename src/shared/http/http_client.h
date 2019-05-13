// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "http_server_side.h"

#include "network/connection.h"

namespace mmo
{
	namespace net
	{
		namespace http
		{
			typedef Connection<ServerSide> Client;
			typedef IConnectionListener<ServerSide> IClientListener;
		}
	}
}
