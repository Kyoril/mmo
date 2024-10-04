// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/non_copyable.h"
#include <memory>
#include <mutex>
#include <list>

namespace mmo
{
	class Player;

	/// Manages all connected players.
	class PlayerManager final : public NonCopyable
	{
	public:

		typedef std::list<std::shared_ptr<Player>> Players;

	public:

		/// Initializes a new instance of the player manager class.
		/// @param playerCapacity The maximum number of connections that can be connected at the same time.
		explicit PlayerManager(
		    size_t playerCapacity
		);
		~PlayerManager();

		/// Notifies the manager that a player has been disconnected which will
		/// delete the player instance.
		void PlayerDisconnected(Player &player);

		/// Determines whether the player capacity limit has been reached.
		bool HasPlayerCapacityBeenReached();

		/// Adds a new player instance to the manager.
		void AddPlayer(std::shared_ptr<Player> added);

		void KickPlayerByAccountId(uint64 accountId);

		/// Gets a player by his account name.
		Player *GetPlayerByAccountName(const String &accountName);

		Player* GetPlayerByCharacterGuid(uint64 characterGuid);

	private:

		Players m_players;
		size_t m_playerCapacity;
		std::mutex m_playerMutex;
	};
}
