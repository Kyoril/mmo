// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/non_copyable.h"
#include "base/signal.h"
#include <memory>
#include <mutex>
#include <list>
#include <functional>

namespace mmo
{
	class Player;
	class MOTDManager;

	/// Manages all connected players.
	class PlayerManager final : public NonCopyable
	{
	public:

		typedef std::list<std::shared_ptr<Player>> Players;

	public:

		/// Initializes a new instance of the player manager class.
		/// @param playerCapacity The maximum number of connections that can be connected at the same time.
		explicit PlayerManager(
		    size_t playerCapacity,
			MOTDManager& motdManager
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

		Player* GetPlayerByCharacterName(const String& characterName);

		/// Gets the current Message of the Day from the MOTD manager.
		const String& GetMessageOfTheDay() const;

		/// Execute a function for each connected player.
		void ForEachPlayer(std::function<void(Player&)> callback) const;

		/// Broadcasts the Message of the Day to all connected players.
		void BroadcastMessageOfTheDay(const String& motd);

	private:

		Players m_players;
		size_t m_playerCapacity;
		mutable std::mutex m_playerMutex;
		MOTDManager& m_motdManager;
		scoped_connection m_motdChangedConnection;
	};
}
