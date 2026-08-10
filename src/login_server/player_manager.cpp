// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "player_manager.h"
#include "player.h"
#include "binary_io/string_sink.h"
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

	void PlayerManager::KickPlayerByAccountId(uint64 accountId)
	{
		// The lookup takes and releases the mutex itself, and PostKick takes no lock at all, so
		// the deadlock this function used to guard against by hand cannot arise.
		const auto player = GetPlayerByAccountID(accountId);
		if (!player)
		{
			return;
		}

		// Posted rather than called directly: this runs on whichever thread served the request
		// that asked for the kick -- the REST ban handler, above all -- and Kick() may only run
		// on the connection's strand.
		player->PostKick();
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
