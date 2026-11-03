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

	/// The channels of a TerrainTarget a caller reads (bit flags).
	namespace terrain_channel
	{
		enum Type : uint8
		{
			/// height and heightWeight.
			Heights = 1,
			/// splat and splatWeight.
			Splat = 2,
			/// waterType and waterHeight.
			Water = 4,
			/// setMaterial.
			Material = 8,
			/// Every channel.
			All = Heights | Splat | Water | Material
		};
	}

	/// A TerrainTargetFunction that is told which channels the caller reads. It must fill those channels exactly as
	/// it would for terrain_channel::All; the others may be left at their defaults (so it can skip work only they need).
	using ChannelTerrainTargetFunction = std::function<TerrainTarget(float x, float z, float baseHeight, terrain_channel::Type channels)>;

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

	/// Apply with a target that is told the channel each pass reads: the height passes ask for
	/// terrain_channel::Heights, the splat pass for Splat, the water pass for Water and the tile material pass for
	/// Material. Writes the same as Apply with a target that fills every channel the same way.
	void Apply(ITerrainAccess& terrain, const WorldRect& rect, const ChannelTerrainTargetFunction& target,
		const String& material, TerrainContribution& out);

	/// Apply with one target per worker thread: ComputeTerrainSamples, then WriteTerrainSamples. Writes the same as
	/// Apply with targets[0] alone. Nothing is written when `targets` is empty.
	void Apply(ITerrainAccess& terrain, const WorldRect& rect, const std::vector<ChannelTerrainTargetFunction>& targets,
		const String& material, TerrainContribution& out);

	/// What a target asks for at every sample of a rect, evaluated but not yet written (see ComputeTerrainSamples).
	struct TerrainSamples
	{
		/// An index range of one sample grid: x0 .. x0 + width - 1 by z0 .. z0 + rows - 1, row-major.
		struct Range
		{
			int32 x0 = 0, z0 = 0, width = 0, rows = 0;

			size_t Count() const { return static_cast<size_t>(width) * static_cast<size_t>(rows); }
			size_t Index(const int32 i, const int32 row) const { return static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(i); }
		};

		/// A vertex: the base height it was evaluated on and the target's height and weight.
		struct Height
		{
			float base = 0.0f, height = 0.0f, weight = 0.0f;
		};

		/// A splat pixel: the target's weights and blend weight.
		struct Splat
		{
			float weight = 0.0f;
			std::array<uint8, 4> splat{};
		};

		/// A water quad: the target's type (0 = none) and surface height.
		struct Water
		{
			uint8 type = 0;
			float height = 0.0f;
		};

		/// False until computed (a default object writes nothing).
		bool computed = false;
		Range outer, inner, splat, water, materials;
		std::vector<Height> outerSamples, innerSamples;
		std::vector<Splat> splatSamples;
		std::vector<Water> waterSamples;
		/// 1 = the tile's material is set.
		std::vector<uint8> materialSamples;
	};

	/// Evaluates the targets at every sample Apply writes over `rect`, reading only the base heights (the Get height
	/// methods are called concurrently, so they must only read): the height and splat samples on targets.size()
	/// threads (no target is called from two threads at once, but different targets run concurrently), water quads and
	/// tile materials with targets[0]. Every target must return the same as targets[0]. Nothing is computed when
	/// `targets` is empty. The terrain is not changed, so this may run on another thread while nothing writes it.
	void ComputeTerrainSamples(const ITerrainAccess& terrain, const WorldRect& rect, const std::vector<ChannelTerrainTargetFunction>& targets,
		TerrainSamples& out);

	/// Writes computed samples the way Apply writes its targets and records base/written (`out` is cleared first).
	/// Refuses, writing nothing, if a height sample with weight > 0 was evaluated on another base than the terrain now
	/// holds (the result would differ from Apply's).
	/// @return False if refused.
	bool WriteTerrainSamples(ITerrainAccess& terrain, const TerrainSamples& samples, const String& material, TerrainContribution& out);
}
