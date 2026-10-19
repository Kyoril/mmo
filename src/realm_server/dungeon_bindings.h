// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/game.h"

#include <map>

namespace mmo
{
	/// Remembers which dungeon instance a character is bound to while it is online.
	///
	/// A realm session outlives the character logged in on it: logging out returns the client to
	/// character selection and the next character continues on the very same session. Bindings
	/// belong to a single character though, so they are tagged with their owner and dropped as
	/// soon as another character takes the session over.
	class DungeonBindings final
	{
	public:
		/// Makes a character the owner of the bindings. Bindings of a previously owning character
		/// are dropped, so a different character never inherits them.
		/// @param characterGuid Guid of the character which owns the bindings from now on.
		void SetOwner(const uint64 characterGuid)
		{
			if (m_ownerGuid != characterGuid)
			{
				m_bindings.clear();
				m_ownerGuid = characterGuid;
			}
		}

		/// Binds the owning character to a dungeon instance on a map.
		/// @param mapId The dungeon map id.
		/// @param instanceId The instance to bind to.
		void Set(const MapId mapId, const InstanceId instanceId)
		{
			m_bindings[mapId] = instanceId;
		}

		/// Gets the instance the owning character is bound to on a map.
		/// @param mapId The dungeon map id.
		/// @returns The bound instance id or a nil id if there is no binding.
		[[nodiscard]] InstanceId Find(const MapId mapId) const
		{
			const auto it = m_bindings.find(mapId);
			return it != m_bindings.end() ? it->second : InstanceId();
		}

		/// Gets every binding of the owning character.
		/// @returns All bindings as map id -> instance id.
		[[nodiscard]] const std::map<MapId, InstanceId>& GetAll() const
		{
			return m_bindings;
		}

		/// Removes the binding of a map, if any.
		/// @param mapId The dungeon map id.
		void Remove(const MapId mapId)
		{
			m_bindings.erase(mapId);
		}

		/// Removes every binding which refers to a given instance.
		/// @param instanceId The instance id to remove bindings of.
		void RemoveByInstanceId(const InstanceId instanceId)
		{
			for (auto it = m_bindings.begin(); it != m_bindings.end(); )
			{
				if (it->second == instanceId)
				{
					it = m_bindings.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

	private:
		/// Guid of the character the bindings belong to, 0 if no character owns them.
		uint64 m_ownerGuid = 0;

		/// Dungeon instance bindings of the owning character (mapId -> instanceId).
		std::map<MapId, InstanceId> m_bindings;
	};
}
