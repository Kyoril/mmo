// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

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

	void PlayerManager::playerDisconnected(
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
	
	bool PlayerManager::hasPlayerCapacityBeenReached()
	{
		std::scoped_lock playerLock{ m_playerMutex };
		return m_players.size() >= m_playerCapacity;
	}

	void PlayerManager::addPlayer(
		std::shared_ptr<Player> added)
	{
		std::scoped_lock playerLock{ m_playerMutex };

		assert(added);
		m_players.push_back(std::move(added));
	}

	Player * PlayerManager::getPlayerByAccountName(
		const String &accountName)
	{
		std::scoped_lock playerLock{ m_playerMutex };

		const auto p = std::find_if(
			m_players.begin(),
			m_players.end(),
			[&accountName](const std::shared_ptr<Player> &p)
		{
			return (p->isAuthentificated() &&
				accountName == p->getAccountName());
		});

		if (p != m_players.end())
		{
			return (*p).get();
		}

		return nullptr;
	}

	Player * PlayerManager::getPlayerByAccountID(
		uint32 accountId)
	{
		std::scoped_lock playerLock{ m_playerMutex };

		const auto p = std::find_if(
			m_players.begin(),
			m_players.end(),
			[accountId](const std::shared_ptr<Player> &p)
		{
			return (p->isAuthentificated() &&
				accountId == p->getAccountId());
		});

		if (p != m_players.end())
		{
			return (*p).get();
		}

		return nullptr;
	}
}
