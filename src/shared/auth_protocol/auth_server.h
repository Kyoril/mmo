// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "auth_connection.h"
#include "network/server.h"

namespace mmo
{
	namespace auth
	{
		typedef mmo::Server<Connection> Server;
	}
}
