// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "game_protocol.h"
#include "game_connection.h"
#include "network/server.h"

namespace mmo
{
	namespace game
	{
		typedef mmo::Server<Connection> Server;
	}
}
