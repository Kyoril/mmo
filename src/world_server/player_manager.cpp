
#include "player_manager.h"

#include "base/macros.h"

namespace mmo
{
	void PlayerManager::AddPlayer(const PlayerPtr& player)
	{
		ASSERT(m_players.find(player->GetCharacterGuid()) == m_players.end());

		m_players.emplace(player->GetCharacterGuid(), player);
	}

	void PlayerManager::RemovePlayer(const PlayerPtr& player)
	{
		std::erase_if(m_players, [&player](const std::pair<ObjectId, PlayerPtr>& pair)
		{
			return pair.second == player;
		});
	}

	PlayerManager::PlayerPtr PlayerManager::GetPlayerByCharacterGuid(ObjectGuid guid) const
	{
		const auto it = m_players.find(guid);
		if (it == m_players.end())
		{
			return nullptr;
		}

		return it->second;
	}
}
