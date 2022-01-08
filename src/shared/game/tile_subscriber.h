#pragma once

#include "base/typedefs.h"
#include "game_protocol/game_protocol.h"

namespace mmo
{
	class TileSubscriber
	{
		virtual void SendPacket(game::Protocol::OutgoingPacket& packet, const std::vector<uint8>& buffer) = 0;
	};
}
