// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game/subsystem.h"

#include <array>
#include <utility>
#include <vector>

namespace mmo
{
	/// Status of every subsystem, indexed by subsystem id.
	typedef std::array<game::SubsystemStatus, game::subsystem::Count_> SubsystemStatusArray;

	/// A list of (subsystem, status) entries as sent to clients.
	typedef std::vector<std::pair<game::Subsystem, game::SubsystemStatus>> SubsystemStatusList;

	/// Returns an array with every subsystem unavailable.
	inline SubsystemStatusArray MakeUnavailableSubsystemArray()
	{
		SubsystemStatusArray result;
		result.fill(game::subsystem_status::Unavailable);
		return result;
	}

	/// The realm's view of subsystem availability. Realm-owned subsystems are decided here;
	/// world-owned ones are reported by each world node and differ per node.
	class RealmSubsystemTable final
	{
	public:
		RealmSubsystemTable()
		{
			// Realm-owned subsystems are available unless switched off.
			m_realmOwned.fill(game::subsystem_status::Available);
		}

		/// Switches a realm-owned subsystem.
		/// @returns true if the status changed.
		bool SetRealmOwned(const game::Subsystem id, const bool enabled)
		{
			if (id >= game::subsystem::Count_ || game::GetSubsystemOwner(id) != game::subsystem_owner::Realm)
			{
				return false;
			}

			const auto status = enabled ? game::subsystem_status::Available : game::subsystem_status::Unavailable;
			if (m_realmOwned[id] == status)
			{
				return false;
			}

			m_realmOwned[id] = status;
			return true;
		}

		/// The status of every subsystem for a player on a world node.
		/// @param worldStatus The node's reported status, or nullptr if the player is on no node.
		[[nodiscard]] SubsystemStatusList Compose(const SubsystemStatusArray* worldStatus) const
		{
			SubsystemStatusList list;
			for (uint8 i = 0; i < game::subsystem::Count_; ++i)
			{
				const auto id = static_cast<game::Subsystem>(i);
				if (game::GetSubsystemOwner(id) == game::subsystem_owner::Realm)
				{
					list.emplace_back(id, m_realmOwned[i]);
				}
				else
				{
					list.emplace_back(id, worldStatus ? (*worldStatus)[i] : game::subsystem_status::Unavailable);
				}
			}
			return list;
		}

	private:
		SubsystemStatusArray m_realmOwned;
	};
}
