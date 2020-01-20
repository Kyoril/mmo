// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "typedefs.h"
#include "big_number.h"
#include "config.h"

namespace mmo
{
	namespace constants
	{
		/// This port is used by the game client to connect with the login server.
		static const uint16 DefaultLoginPlayerPort = 3724;
		/// This is the default port used by a world server.
		static const uint16 DefaultRealmPlayerPort = 8129;
		/// This is the default port on which the login server listens for realms.
		static const uint16 DefaultLoginRealmPort = 6279;
		/// This is the default port on which the realm server listens for worlds.
		static const uint16 DefaultRealmWorldPort = 6280;
		/// This is the default port on which the login server listens for team servers.
		static const uint16 DefaultLoginTeamPort = 6282;
		/// This is the default port used by MySQL servers.
		static const uint16 DefaultMySQLPort = 3306;
		/// Maximum number of realms.
		static const uint16 MaxRealmCount = 255;

		/// Every packet of the wowpp protocol has to start with this constant.
		static const mmo::PacketBegin PacketBegin = 0xfc;

		/// Invalid identifier constant
		static const uint32 InvalidId = 0xffffffff;

		// These are shared constants used by srp-6 calculations
		namespace srp
		{
			static const BigNumber N(Srp6N);
			static const BigNumber g(Srp6g);
		}
	}
}
