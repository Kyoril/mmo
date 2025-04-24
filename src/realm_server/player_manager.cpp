// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "player_manager.h"
#include "player.h"
#include "motd_manager.h"

#include "binary_io/string_sink.h"

#include <cassert>


namespace mmo
{
	PlayerManager::PlayerManager(
	    size_t playerCapacity,
		MOTDManager& motdManager)
		: m_playerCapacity(playerCapacity)
		, m_motdManager(motdManager)
	{
		// Subscribe to MOTD changes
		m_motdChangedConnection = motdManager.motdChanged.connect(this, &PlayerManager::BroadcastMessageOfTheDay);
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

	void PlayerManager::KickPlayerByAccountId(uint64 accountId)
	{
		std::shared_ptr<Player> player;

		// We do finding the player in a scope so that we only lock the m_playerMutex as long as we really need to.
		// After we found the player, we release it because kicking the player will also try to remove him from the list
		// of players, which also tries to lock the mutex which would result in a deadlock.
		{
			std::scoped_lock playerLock{ m_playerMutex };

			const auto p = std::find_if(
					m_players.begin(),
					m_players.end(),
					[&accountId](const std::shared_ptr<Player>& p)
					{
						return (p->IsAuthenticated() && accountId == p->GetAccountId());
					});

			if (p != m_players.end())
			{
				player = *p;
			}
		}

		if (player)
		{
			player->Kick();
		}
	}

	Player * PlayerManager::GetPlayerByAccountName(const String &accountName)
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
			return (*p).get();
		}

		return nullptr;
	}

	Player* PlayerManager::GetPlayerByCharacterGuid(uint64 characterGuid)
	{
		std::scoped_lock playerLock{ m_playerMutex };

		const auto p = std::find_if(
			m_players.begin(),
			m_players.end(),
			[characterGuid](const std::shared_ptr<Player> &p)
		{
			return (p->HasCharacterGuid() && characterGuid == p->GetCharacterGuid());
		});

		if (p != m_players.end())
		{
			return (*p).get();
		}

		return nullptr;
	}

	Player* PlayerManager::GetPlayerByCharacterName(const String& characterName)
	{
		std::scoped_lock playerLock{ m_playerMutex };

		const auto p = std::find_if(
			m_players.begin(),
			m_players.end(),
			[&characterName](const std::shared_ptr<Player>& p)
			{
				return (p->HasCharacterGuid() && characterName == p->GetCharacterName());
			});

		if (p != m_players.end())
		{
			return (*p).get();
		}

		return nullptr;
	}

	const String& PlayerManager::GetMessageOfTheDay() const
	{
		return m_motdManager.GetMessageOfTheDay();
	}

	void PlayerManager::ForEachPlayer(std::function<void(Player&)> callback) const
	{
		std::scoped_lock playerLock{ m_playerMutex };

		for (auto& player : m_players)
		{
			callback(*player);
		}
	}

	void PlayerManager::BroadcastMessageOfTheDay(const String& motd)
	{
		// Send MOTD to all connected players who have loaded a character
		ForEachPlayer([&motd](Player& player) {
			// Only send to players that have loaded a character and are in the world
			if (player.HasCharacterGuid())
			{
				player.SendMessageOfTheDay(motd);
			}
		});
	}
}
