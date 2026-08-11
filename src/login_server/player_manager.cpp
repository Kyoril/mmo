// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "player_manager.h"
#include "player.h"
#include "binary_io/string_sink.h"
#include "log/default_log_levels.h"
#include <vector>
#include <cassert>

namespace mmo
{
	PlayerManager::PlayerManager(
	    size_t playerCapacity)
		: m_playerCapacity(playerCapacity)
	{
	}

	PlayerManager::~PlayerManager()
	{
	}

	void PlayerManager::PlayerDisconnected(
		Player &player)
	{
		std::scoped_lock playerLock{ m_playerMutex };

		const auto p = std::find_if(
		                   m_players.begin(),
		                   m_players.end(),
		                   [&player](const std::shared_ptr<Player> &p)
		{
			return (&player == p.get());
		});
		assert(p != m_players.end());
		m_players.erase(p);
	}
	
	bool PlayerManager::HasPlayerCapacityBeenReached()
	{
		std::scoped_lock playerLock{ m_playerMutex };
		return m_players.size() >= m_playerCapacity;
	}

	void PlayerManager::AddPlayer(
		std::shared_ptr<Player> added)
	{
		std::scoped_lock playerLock{ m_playerMutex };

		assert(added);
		m_players.push_back(std::move(added));
	}

	std::shared_ptr<Player> PlayerManager::GetPlayerByAccountName(const String &accountName)
	{
		std::scoped_lock playerLock{ m_playerMutex };

		const auto p = std::find_if(
			m_players.begin(),
			m_players.end(),
			[&accountName](const std::shared_ptr<Player> &p)
		{
			return (p->IsAuthenticated() &&
				accountName == p->GetAccountName());
		});

		if (p != m_players.end())
		{
			return *p;
		}

		return nullptr;
	}

	std::shared_ptr<Player> PlayerManager::GetPlayerByAccountID(uint64 accountId)
	{
		std::scoped_lock playerLock{ m_playerMutex };

		const auto p = std::find_if(
			m_players.begin(),
			m_players.end(),
			[accountId](const std::shared_ptr<Player> &p)
		{
			return (p->IsAuthenticated() &&
				accountId == p->GetAccountId());
		});

		if (p != m_players.end())
		{
			return *p;
		}

		return nullptr;
	}

	void PlayerManager::KickPlayerByAccountId(const uint64 accountId, const std::optional<auth::SessionKickReason> reason)
	{
		// Every session of the account, not just the first match. An account is not supposed to
		// hold more than one once duplicate-login displacement is in play, but the ban path is
		// exactly where "not supposed to" is not good enough: leaving a session behind would leave
		// a banned account playing.
		for (const auto& player : CollectSessionsForAccount(accountId, nullptr))
		{
			// Posted rather than called directly: this runs on whichever thread served the request
			// that asked for the kick -- the REST ban handler, above all -- and Kick() may only run
			// on the connection's strand.
			player->PostKick(reason);
		}
	}

	void PlayerManager::KickOtherSessionsForAccount(const uint64 accountId, const Player& except, const auth::SessionKickReason reason)
	{
		for (const auto& player : CollectSessionsForAccount(accountId, &except))
		{
			ILOG("Displacing an older session of account " << accountId << " because it was logged in again");

			// Posted: the login server runs two io threads, so the session being displaced almost
			// never belongs to the strand this call is running on.
			player->PostKick(reason);
		}
	}

	std::vector<std::shared_ptr<Player>> PlayerManager::CollectSessionsForAccount(const uint64 accountId, const Player* except)
	{
		// Collected under the lock and kicked outside it: a kick removes the player from this
		// manager, which takes the same mutex.
		std::vector<std::shared_ptr<Player>> sessions;

		std::scoped_lock playerLock{ m_playerMutex };

		for (const auto& player : m_players)
		{
			// An unauthenticated session has no session key and cannot act on the account, so it
			// is not a session of it -- only a challenge that was never completed. Skipping those
			// is not a nicety: every session reports account id 0 until its challenge resolves, so
			// without this a kick for account 0 would drop every client still logging in.
			if (player.get() == except ||
				!player->IsAuthenticated() ||
				player->GetAccountId() != accountId)
			{
				continue;
			}

			sessions.push_back(player);
		}

		return sessions;
	}

	void PlayerManager::DisconnectAll()
	{
		// Copy the list out under the lock first: the kick removes the player from this manager,
		// which takes the same mutex.
		std::vector<std::shared_ptr<Player>> players;
		{
			std::scoped_lock playerLock{ m_playerMutex };
			players.assign(m_players.begin(), m_players.end());
		}

		for (const auto& player : players)
		{
			// Posted, because this is called from the shutdown handler, which asio may run on
			// either io thread.
			player->PostKick();
		}
	}

	size_t PlayerManager::GetPlayerCount()
	{
		std::scoped_lock playerLock{ m_playerMutex };
		return m_players.size();
	}
}
