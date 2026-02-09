#pragma once

#include "player.h"

#include <memory>
#include <map>

namespace mmo
{
	/// @brief Class for managing player connection objects.
	class PlayerManager final
	{
	public:
		typedef std::shared_ptr<Player> PlayerPtr;

	public:
		PlayerManager() = default;

	public:
		/// @brief Adds a new player to the player manager.
		/// @param player The player to add to the manager.
		void AddPlayer(const PlayerPtr& player);

		/// @brief Removes a player from the player manager, allowing him to be released.
		/// @param player The player to remove from the player manager.
		void RemovePlayer(const PlayerPtr& player);

		/// @brief Gets a player by character guid.
		/// @param guid Character guid to look up.
		/// @return Shared pointer to the player if found, otherwise nullptr.
		PlayerPtr GetPlayerByCharacterGuid(ObjectGuid guid) const;

	private:
		std::map<ObjectId, PlayerPtr> m_players;
	};
	
}
