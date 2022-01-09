// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "game_protocol/game_protocol.h"

namespace mmo
{
	class TileSubscriber
	{
	public:
		virtual ~TileSubscriber() = default;

	public:
		virtual const GameObject& GetGameObject() const = 0;

		virtual void NotifyObjectsSpawned(std::vector<GameObject*> objects) const = 0;

		virtual void NotifyObjectsDespawned(std::vector<GameObject*> objects) const = 0;

		virtual void SendPacket(game::Protocol::OutgoingPacket& packet, const std::vector<char>& buffer) = 0;
	};
}
