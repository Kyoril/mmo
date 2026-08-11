// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/non_copyable.h"
#include "auth_protocol/auth_protocol.h"
#include <memory>
#include <mutex>
#include <list>
#include <optional>
#include <vector>

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
		/// @param reason Forwarded to the client so it can explain the disconnect. Omit where the
		///	       disconnect needs no explanation, such as the reconnect forced by a GM level change.
		void KickPlayerByAccountId(uint64 accountId, std::optional<auth::SessionKickReason> reason = std::nullopt);

		/// Kicks every session of an account except one. Used to enforce a single live session per
		/// account: the session that just authenticated stays, all older ones are displaced.
		///
		/// Safe to call from any thread; each kick is posted to its own connection's strand.
		///
		/// @param accountId The account whose other sessions are to be displaced.
		/// @param except The session to keep -- normally the caller, which has just authenticated.
		/// @param reason Forwarded to each displaced client so it can explain the disconnect.
		void KickOtherSessionsForAccount(uint64 accountId, const Player& except, auth::SessionKickReason reason);

		/// Number of connected players, authenticated or not.
		size_t GetPlayerCount();

		/// Disconnects every managed player. Used at shutdown so peers see a closed connection
		/// rather than a socket that simply stops answering. Safe to call from any thread.
		void DisconnectAll();

	private:

		/// Collects owning references to every authenticated session of an account.
		/// @param except Optionally one session to leave out, such as the caller.
		std::vector<std::shared_ptr<Player>> CollectSessionsForAccount(uint64 accountId, const Player* except);

	private:

		Players m_players;
		size_t m_playerCapacity;
		std::mutex m_playerMutex;
	};
}
