// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "terrain_access.h"

#include <array>
#include <cstddef>
#include <functional>
#include <vector>

namespace mmo::pcg
{
	/// Axis aligned world rectangle (x/z), inclusive.
	struct WorldRect
	{
		float minX = 0.0f, minZ = 0.0f, maxX = 0.0f, maxZ = 0.0f;
	};

	/// Returns true if the two rectangles overlap or touch.
	bool RectsOverlap(const WorldRect& a, const WorldRect& b);

	/// What a generator wants at a world position. weight 0 = leave untouched.
	struct TerrainTarget
	{
		float height = 0.0f;
		float heightWeight = 0.0f;
		std::array<uint8, 4> splat{};
		float splatWeight = 0.0f;
		/// waterType != 0 = quad gets water.
		uint8 waterType = 0;
		float waterHeight = 0.0f;
		bool setMaterial = false;
	};

	using TerrainTargetFunction = std::function<TerrainTarget(float x, float z, float baseHeight)>;

	/// Everything a generator wrote, with the value that was there before.
	struct TerrainContribution
	{
		template<typename T>
		struct Entry
		{
			int32 x, z;
			T base, written;
		};

		std::vector<Entry<float>> outer, inner, waterHeights;
		std::vector<Entry<uint32>> splat;
		std::vector<Entry<uint8>> waterQuads;
		std::vector<Entry<String>> materials;
		/// For FinishEdits.
		IntRect touchedOuter{};

		/// Returns true if nothing is recorded.
		bool IsEmpty() const;
		/// Drops all entries.
		void Clear();
	};

	/// Fraction of the recorded outer and inner height entries whose current terrain value still
	/// equals what was written. 1 when nothing is recorded. A low ratio means the terrain no longer
	/// carries the contribution (data restored from version control, a failed page save), and
	/// Unapply would then shift every height by -delta instead of restoring the base.
	/// @param matching Receives the number of matching entries if not null.
	/// @param total Receives the number of recorded height entries if not null.
	float ContributionMatchRatio(const ITerrainAccess& terrain, const TerrainContribution& contribution,
		size_t* matching = nullptr, size_t* total = nullptr);

	/// Below this match ratio the record is considered not to belong to the terrain any more.
	constexpr float kMinContributionMatchRatio = 0.5f;

	/// Restores the base where the current value still equals what was written. Heights the user
	/// changed since keep the user's change: current - (written - base). Other kinds keep current.
	/// Clears the contribution.
	void Unapply(ITerrainAccess& terrain, TerrainContribution& contribution);

	/// Reads the terrain's current value at every sample the contribution recorded. The result has the
	/// contribution's samples, base values and touchedOuter, with `written` holding the current value.
	/// RestoreSamples puts exactly these values back, so a build that fails after Unapply can return
	/// the terrain to the state it had when the samples were captured.
	TerrainContribution CaptureSamples(const ITerrainAccess& terrain, const TerrainContribution& contribution);

	/// Writes the `written` value of every entry of a CaptureSamples result back into the terrain, then
	/// calls FinishEdits with its touchedOuter (not when it is empty).
	void RestoreSamples(ITerrainAccess& terrain, const TerrainContribution& captured);

	/// Writes the target over `rect` and records base/written for every changed sample. Entries are
	/// stored sorted by (z, x). `material` is the tile material set where target.setMaterial.
	/// `out` is cleared first. Indices outside the terrain must not be covered by `rect`.
	void Apply(ITerrainAccess& terrain, const WorldRect& rect, const TerrainTargetFunction& target,
		const String& material, TerrainContribution& out);
}
