// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/signal.h"
#include "game/subsystem.h"

#include <array>
#include <utility>
#include <vector>

namespace mmo
{
	/// Availability of the world-owned subsystems on this node.
	///
	/// A subsystem is available only while three independent switches agree: the node's config
	/// enables it, no GM or REST toggle disabled it, and the subsystem itself reports healthy
	/// (e.g. the bug uploader can reach its API). Keeping them separate means an automatic
	/// recovery never overrides an operator's "off", and an operator's "on" never hides a broken
	/// backend.
	class WorldSubsystemState final
	{
	public:
		typedef std::vector<std::pair<game::Subsystem, game::SubsystemStatus>> StatusList;

		/// Fired with the subsystems whose effective status changed.
		signal<void(const StatusList&)> statusChanged;

	public:
		WorldSubsystemState()
		{
			for (auto& entry : m_entries)
			{
				entry = Entry{};
			}
		}

		/// Sets whether the node's configuration enables the subsystem.
		void SetConfigEnabled(const game::Subsystem id, const bool enabled)
		{
			Update(id, [enabled](Entry& e) { e.config = enabled; });
		}

		/// Sets the operator switch (GM command or realm REST).
		void SetManualEnabled(const game::Subsystem id, const bool enabled)
		{
			Update(id, [enabled](Entry& e) { e.manual = enabled; });
		}

		/// Sets the subsystem's own health.
		void SetHealthy(const game::Subsystem id, const bool healthy)
		{
			Update(id, [healthy](Entry& e) { e.healthy = healthy; });
		}

		[[nodiscard]] bool IsAvailable(const game::Subsystem id) const
		{
			return id < game::subsystem::Count_ && m_entries[id].IsAvailable();
		}

		[[nodiscard]] game::SubsystemStatus GetStatus(const game::Subsystem id) const
		{
			return IsAvailable(id) ? game::subsystem_status::Available : game::subsystem_status::Unavailable;
		}

		/// The status of every world-owned subsystem, for the full list sent after logon.
		[[nodiscard]] StatusList GetWorldOwnedStatus() const
		{
			StatusList list;
			for (uint8 i = 0; i < game::subsystem::Count_; ++i)
			{
				const auto id = static_cast<game::Subsystem>(i);
				if (game::GetSubsystemOwner(id) == game::subsystem_owner::World)
				{
					list.emplace_back(id, GetStatus(id));
				}
			}
			return list;
		}

	private:
		struct Entry
		{
			bool config = false;
			bool manual = true;
			bool healthy = true;

			[[nodiscard]] bool IsAvailable() const
			{
				return config && manual && healthy;
			}
		};

		template<class Mutator>
		void Update(const game::Subsystem id, Mutator&& mutate)
		{
			if (id >= game::subsystem::Count_)
			{
				return;
			}

			const bool before = m_entries[id].IsAvailable();
			mutate(m_entries[id]);
			const bool after = m_entries[id].IsAvailable();
			if (before != after)
			{
				statusChanged(StatusList{ { id, GetStatus(id) } });
			}
		}

	private:
		std::array<Entry, game::subsystem::Count_> m_entries;
	};
}
