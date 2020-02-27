// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "player_manager.h"
#include "player.h"

#include "binary_io/string_sink.h"

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

	void PlayerManager::PlayerDisconnected(Player &player)
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

	void PlayerManager::AddPlayer(std::shared_ptr<Player> added)
	{
		std::scoped_lock playerLock{ m_playerMutex };

		assert(added);
		m_players.push_back(added);

		// Challenge the newly connected client for authentication
		added->SendAuthChallenge();
	}

	Player * PlayerManager::GetPlayerByAccountName(const String &accountName)
	{
		std::scoped_lock playerLock{ m_playerMutex };

		const auto p = std::find_if(
			m_players.begin(),
			m_players.end(),
			[&accountName](const std::shared_ptr<Player> &p)
		{
			return (p->IsAuthentificated() &&
				accountName == p->GetAccountName());
		});

		if (p != m_players.end())
		{
			return (*p).get();
		}

		return nullptr;
	}

	Player * PlayerManager::GetPlayerByAccountID(uint32 accountId)
	{
		std::scoped_lock playerLock{ m_playerMutex };

		const auto p = std::find_if(
			m_players.begin(),
			m_players.end(),
			[accountId](const std::shared_ptr<Player> &p)
		{
			return (p->IsAuthentificated() &&
				accountId == p->GetAccountId());
		});

		if (p != m_players.end())
		{
			return (*p).get();
		}

		return nullptr;
	}
}
