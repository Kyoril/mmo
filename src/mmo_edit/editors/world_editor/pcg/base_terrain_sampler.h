// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "pcg/graph/evaluator.h"
#include "pcg/grid_layout.h"
#include "pcg/terrain_contribution.h"

#include <unordered_map>

namespace mmo
{
	/// @brief Reads the terrain as it would be after Unapply of a contribution, so an evaluation sees the base terrain a
	///        rebuild evaluates on while the live terrain still carries the instance's previous build.
	/// @details Height is the live height minus the bilinear (written - base) of the four outer vertices around (x, z)
	///          (outer vertices the contribution did not record count 0). The editor's smooth height is that bilinear
	///          interpolation of the outer vertices, and Unapply restores every recorded vertex to current -
	///          (written - base), so this matches a rebuild up to float rounding. Normal: where the contribution changes
	///          heights around (x, z), the normal of the base heights by central differences half an outer vertex
	///          step to each side; elsewhere the live normal. SplatWeight and WaterLevel pass through to the live
	///          terrain: they are approximations wherever the contribution painted or wrote water (only heights are
	///          undone here). Where the live terrain has no height (ocean pages), neither does this sampler, even if the
	///          pages only became terrain through the contribution. Main thread only, like the live sampler.
	class BaseTerrainSampler final : public pcg::graph::ITerrainSampler, public NonCopyable
	{
	public:
		/// @brief Creates the sampler.
		/// @param live The live terrain (an EditorTerrainSampler). Must outlive the sampler.
		/// @param contribution The contribution to undo; its outer height entries are copied.
		/// @param layout The terrain's grid layout (EditorTerrainAccess::GetLayout) the contribution's indices use.
		BaseTerrainSampler(const pcg::graph::ITerrainSampler& live, const pcg::TerrainContribution& contribution, const pcg::GridLayout& layout);

	public:
		/// @brief Live height minus the contribution's height change at the position. False where the live terrain
		///        has no height.
		bool Height(float x, float z, float& h) const override;

		/// @brief Normal of the base heights where the contribution changes heights nearby, else the live normal.
		Vector3 Normal(float x, float z) const override;

		/// @brief The live splat weight (paint of the contribution is not undone).
		float SplatWeight(const String& textureKey, float x, float z) const override;

		/// @brief The live water level (water of the contribution is not undone).
		bool WaterLevel(float x, float z, float& level) const override;

		/// @brief The live revision mixed with a hash of the contribution's outer height entries, so a rebuild (which
		///        changes both) and an edit of the terrain both change it.
		uint64 Revision() const override;

		/// @brief Bilinear (written - base) of the recorded outer vertices around a world position (0 where nothing
		///        is recorded).
		float GetHeightChange(float x, float z) const;

		/// @brief Hash of a contribution's outer height entries, as mixed into Revision.
		static uint64 HashContribution(const pcg::TerrainContribution& contribution);

	private:
		/// @brief Recorded written - base of an outer vertex, 0 if it is not recorded.
		float GetVertexChange(int64 vx, int64 vz) const;

	private:
		const pcg::graph::ITerrainSampler& m_live;
		pcg::GridLayout m_layout;

		/// @brief written - base per recorded outer vertex, keyed by (vx, vz) packed into 64 bits.
		std::unordered_map<uint64, float> m_changes;

		/// @brief Hash of the recorded outer entries.
		uint64 m_contributionHash = 0;
	};
}
