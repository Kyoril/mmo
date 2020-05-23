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
				/// Sent by the client to request the character list.
				CharEnum = 0x01,

				/// Counter constant
				Count_,
			};
		}

		/// Enumerates possible op codes sent by the client to a realm server.
		namespace realm_client_packet
		{
			enum Type
			{
				/// Sent by the client immediatly after connecting to challenge it for authentication.
				AuthChallenge = 0x00,
				/// Send by the realm as response.
				AuthSessionResponse = 0x01,
				/// Sent by the realm as response to the CharEnum packet.
				CharEnum = 0x02,

				/// Counter constant
				Count_,
			};
		}


		////////////////////////////////////////////////////////////////////////////////
		// Typedefs

		/// Enumerates possible authentification result codes.
		namespace auth_result
		{
			enum Type
			{
				/// Success.
				Success,
				/// Could not log in at this time. Please try again later.
				FailDbBusy,
				/// Unable to validate game version. This may be caused by file corruption or interference of another program. Please visit <site> for more information and possible solutions to this issue.
				FailVersionInvalid,
				/// Downloading...
				FailVersionUpdate,
				/// Unable to connect.
				FailInvalidServer,
				/// Unable to connect.
				FailNoAccess,
				/// Internal error.
				FailInternalError,

				Count_
			};
		}

		typedef auth_result::Type AuthResult;
	}
}
