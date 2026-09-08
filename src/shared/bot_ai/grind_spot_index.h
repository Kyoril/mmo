// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "math/vector3.h"

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace mmo
{
	namespace proto
	{
		class Project;
		class FactionTemplateEntry;
	}

	/// One place in the world where something of a known level and faction is spawned.
	struct GrindSpot final
	{
		uint32 mapId { 0 };
		uint32 unitEntry { 0 };
		Vector3 position { Vector3::Zero };
		uint32 minLevel { 1 };
		uint32 maxLevel { 1 };
		uint32 factionTemplate { 0 };

		/// How many of this creature the spawn keeps alive. A bot prefers a busier spot, because
		/// it can keep killing there instead of travelling again after one kill.
		uint32 maxCount { 1 };
	};

	/// Every creature spawn a bot might want to grind at, indexed for "what is near me and at my
	/// level".
	///
	/// This is the piece that replaces a hand-maintained travel graph. The spawn table is already
	/// in the proto project the bot loads, which means the bot knows where things live, what
	/// level they are and what faction they belong to before it has ever seen one. A private
	/// server implementation without that data has to scrape it out of the client or build the
	/// graph by hand; we can just read it.
	///
	/// Built once per process and shared, const, by every bot. It is only a navigation hint: what
	/// the bot actually attacks is decided from what it can see once it arrives, because a spawn
	/// point says nothing about what is alive right now.
	class GrindSpotIndex final : public NonCopyable
	{
	public:
		/// Reads every spawn out of the project and keeps the ones worth grinding.
		///
		/// @param project Loaded proto project.
		/// @param playerFactionTemplate Faction template the bots belong to. Spawns not hostile to
		///        it are dropped, using the same rule the server applies. Pass 0 when the bots have
		///        no faction template - a dataset that does not populate them - and the faction
		///        filter is skipped entirely rather than rejecting everything.
		///
		/// Skipping is the right degradation rather than a shortcut: the runtime target picker
		/// ignores faction too and attacks any creature without NPC flags, so an index that kept
		/// nothing would send bots nowhere while they remained perfectly willing to fight.
		void Build(const proto::Project& project, uint32 playerFactionTemplate);

		/// Whether the last Build applied the faction filter.
		[[nodiscard]] bool WasFactionFiltered() const { return m_factionFiltered; }

		[[nodiscard]] const std::vector<GrindSpot>& GetSpots() const { return m_spots; }
		[[nodiscard]] std::size_t GetSpotCount() const { return m_spots.size(); }
		[[nodiscard]] const GrindSpot& GetSpot(const std::size_t index) const { return m_spots[index]; }

		/// Indices of spots on the given map whose level range overlaps [minLevel, maxLevel],
		/// within maxDistance of origin, nearest first.
		[[nodiscard]] std::vector<std::size_t> FindCandidates(
			uint32 mapId,
			const Vector3& origin,
			uint32 minLevel,
			uint32 maxLevel,
			float maxDistance,
			std::size_t maxResults) const;

	private:
		/// Spot indices bucketed by map, so a query never looks at another map.
		std::unordered_map<uint32, std::vector<std::size_t>> m_spotsByMap;
		std::vector<GrindSpot> m_spots;
		bool m_factionFiltered { false };
	};

	/// Whether the two faction templates are friendly with one another.
	///
	/// Mirrors GameUnitS::UnitIsFriendly, and exists for the same reason IsFactionHostile does:
	/// the bot decides what to attack, and it must reach the same answer the server will. Without
	/// it the bot only knows not to attack units carrying an NPC flag, which says nothing about a
	/// town guard - so bots picked fights with their own faction's guards, who cannot fight back.
	[[nodiscard]] bool IsFactionFriendly(
		const proto::Project& project,
		uint32 attackerFactionTemplate,
		uint32 defenderFactionTemplate);

	/// Whether a unit of the attacker faction template counts the defender one as an enemy.
	/// Mirrors GameUnitS::UnitIsEnemy so that the spots a bot travels to are the ones it will
	/// actually be willing to fight when it gets there.
	[[nodiscard]] bool IsFactionHostile(
		const proto::Project& project,
		uint32 attackerFactionTemplate,
		uint32 defenderFactionTemplate);
}
