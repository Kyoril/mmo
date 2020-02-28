// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game_incoming_packet.h"
#include "game_outgoing_packet.h"

namespace mmo
{
	namespace game
	{
		struct Protocol
		{
			typedef game::IncomingPacket IncomingPacket;
			typedef game::OutgoingPacket OutgoingPacket;
		};


		////////////////////////////////////////////////////////////////////////////////
		// BEGIN: Client <-> Realm section

		/// Enumerates possible op codes sent by the client to a realm server.
		namespace client_realm_packet
		{
			enum Type
			{
				/// Sent by the client to respond to a AuthChallenge from a realm server.
				AuthSession = 0x00,
			};
		}

		/// Enumerates possible op codes sent by the client to a realm server.
		namespace realm_client_packet
		{
			enum Type
			{
				/// Sent by the client immediatly after connecting to challenge it for authentication.
				AuthChallenge = 0x00,
			};
		}
	}
}
