// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game_protocol/game_protocol.h"

namespace mmo
{
	class GameUnitS;
	class GameObjectS;

	class TileSubscriber
	{
	public:
		virtual ~TileSubscriber() = default;

	public:
		virtual GameUnitS& GetGameUnit() const = 0;

		virtual void NotifyObjectsUpdated(const std::vector<GameObjectS*>& objects) = 0;

		virtual void NotifyObjectsSpawned(const std::vector<GameObjectS*>& objects) = 0;

		virtual void NotifyObjectsDespawned(const std::vector<GameObjectS*>& objects) = 0;

		virtual GameTime ClientToServerTime(GameTime clientTimestamp) const = 0;

		virtual GameTime ServerToClientTime(GameTime serverTimestamp) const = 0;

		virtual bool HasReceivedTimeSyncResponse() const = 0;

		virtual void SendPacket(game::Protocol::OutgoingPacket& packet, const std::vector<char>& buffer, bool flush = true) = 0;

		/// Returns true if the client behind this subscriber has already received a creation
		/// packet for the given object GUID and has not yet received a destroy packet.
		/// Used to guard against sending spurious spawn or despawn packets.
		virtual bool IsObjectKnown(uint64 guid) const { return false; }
	};
}
