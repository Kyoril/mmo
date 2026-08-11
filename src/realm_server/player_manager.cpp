// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "player_manager.h"
#include "player.h"

#include <vector>
#include "player_group.h"
#include "motd_manager.h"

#include "binary_io/string_sink.h"
#include "log/default_log_levels.h"

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

	size_t PlayerManager::GetPlayerCount() const
	{
		std::scoped_lock playerLock{ m_playerMutex };
		return m_players.size();
	}

	void PlayerManager::AddPlayer(std::shared_ptr<Player> added)
	{
		std::scoped_lock playerLock{ m_playerMutex };

		assert(added);
		m_players.push_back(added);

		// Challenge the newly connected client for authentication
		added->SendAuthChallenge();
	}

	void PlayerManager::KickPlayerByAccountId(const uint64 accountId, const std::optional<auth::SessionKickReason> reason)
	{
		// Every session of the account goes, not just the first match. An account is not supposed
		// to hold more than one, but if it somehow does -- sessions that predate the login server
		// learning about this account, say -- then leaving the extras behind would defeat the point
		// of the kick.
		//
		// Collected in a scope so that the mutex is held only as long as it is really needed: the
		// kick removes the player from this manager, which takes the same mutex.
		std::vector<std::shared_ptr<Player>> players;
		{
			std::scoped_lock playerLock{ m_playerMutex };

			for (const auto& player : m_players)
			{
				if (player->IsAuthenticated() && accountId == player->GetAccountId())
				{
					players.push_back(player);
				}
			}
		}

		for (const auto& player : players)
		{
			player->Kick(reason);
		}
	}

	void PlayerManager::KickOtherSessionsForAccount(const uint64 accountId, const Player& except, const auth::SessionKickReason reason)
	{
		std::vector<std::shared_ptr<Player>> displaced;
		{
			std::scoped_lock playerLock{ m_playerMutex };

			for (const auto& player : m_players)
			{
				if (player.get() == &except ||
					!player->IsAuthenticated() ||
					player->GetAccountId() != accountId)
				{
					continue;
				}

				displaced.push_back(player);
			}
		}

		for (const auto& player : displaced)
		{
			ILOG("Displacing older session of account " << player->GetAccountName()
				<< " (" << accountId << ") because it authenticated again on this realm");
			player->Kick(reason);
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

	void PlayerManager::DisconnectAll()
	{
		// Copy the list out under the lock first: Kick() removes the player from this manager,
		// which takes the same mutex.
		std::vector<std::shared_ptr<Player>> players;
		{
			std::scoped_lock playerLock{ m_playerMutex };
			players.assign(m_players.begin(), m_players.end());
		}

		for (const auto& player : players)
		{
			player->Kick();
		}
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

	void PlayerManager::OnInstanceDestroyed(InstanceId instanceId)
	{
		// Clear dungeon bindings for all players and their groups
		ForEachPlayer([&instanceId](Player& player) {
			player.ClearDungeonBindingByInstanceId(instanceId);

			if (auto group = player.GetGroup())
			{
				group->RemoveInstanceBindingByInstanceId(instanceId);
			}
		});
	}
}
