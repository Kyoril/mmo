// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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

		/// Gets a player by his account name.
		///
		/// Returns an owning reference rather than a raw pointer: the mutex below protects the
		/// list, not the lifetime of what is taken out of it, so a caller on another thread
		/// could otherwise be left holding a pointer to a session that has since disconnected.
		std::shared_ptr<Player> GetPlayerByAccountName(const String &accountName);

		/// Gets a player by account id. See GetPlayerByAccountName for why this owns.
		std::shared_ptr<Player> GetPlayerByAccountID(uint64 accountId);

		/// Kicks a player by account id if connected. Safe to call from any thread.
		void KickPlayerByAccountId(uint64 accountId);

		/// Number of connected players, authenticated or not.
		size_t GetPlayerCount();

		/// Disconnects every managed player. Used at shutdown so peers see a closed connection
		/// rather than a socket that simply stops answering. Safe to call from any thread.
		void DisconnectAll();

	private:

		Players m_players;
		size_t m_playerCapacity;
		std::mutex m_playerMutex;
	};
}
