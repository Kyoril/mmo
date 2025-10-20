// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game_protocol/game_protocol.h"

namespace mmo
{
	class GameUnitS;

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
	};
}
