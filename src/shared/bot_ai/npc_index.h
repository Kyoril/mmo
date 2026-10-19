// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"

#include <optional>
#include <unordered_map>
#include <vector>

namespace mmo
{
	namespace proto
	{
		class Project;
	}

	/// A service NPC a bot can walk to: a vendor today, trainers later.
	struct BotNpcSpot final
	{
		uint32 mapId { 0 };
		uint32 unitEntry { 0 };
		Vector3 position { Vector3::Zero };
	};

	/// Where the service NPCs are, read once from the spawn data and shared by every bot.
	///
	/// Like GrindSpotIndex, read-only after Build so that one instance serves the whole swarm.
	class NpcIndex final
	{
	public:
		/// Indexes every active spawn of a vendor that is not hostile to the given faction.
		/// @param playerFactionTemplate The bots' faction template; one the data does not have skips the check.
		void Build(const proto::Project& project, uint32 playerFactionTemplate);

		/// The vendor spawn closest to a position on a map, if there is one.
		[[nodiscard]] std::optional<BotNpcSpot> FindNearestVendor(uint32 mapId, const Vector3& position) const;

		[[nodiscard]] std::size_t GetVendorCount() const { return m_vendorCount; }

	private:
		std::unordered_map<uint32, std::vector<BotNpcSpot>> m_vendorsByMap;
		std::size_t m_vendorCount { 0 };
	};
}
