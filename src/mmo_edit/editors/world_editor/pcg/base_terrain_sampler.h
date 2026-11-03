// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "editor_terrain_access.h"
#include "editor_terrain_sampler.h"

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "pcg/graph/base_terrain_view.h"
#include "pcg/terrain_contribution.h"

namespace mmo
{
	namespace terrain
	{
		class Terrain;
	}

	/// @brief The editor terrain as it would be after Unapply of a contribution (pcg::graph::BaseTerrainView over the
	///        editor's live sampler and EditorTerrainAccess), so an evaluation sees the base terrain a rebuild
	///        evaluates on while the live terrain still carries the instance's previous build.
	/// @details Heights, normals, splat and water are undone like Unapply undoes them; tile materials are not
	///          (HasMaterialChanges). Pages that only became terrain through the contribution keep their heights here,
	///          which matches the rebuild: Unapply does not turn pages back into ocean either. Main thread only.
	class BaseTerrainSampler final : public pcg::graph::ITerrainSampler, public NonCopyable
	{
	public:
		/// @brief Creates the sampler.
		/// @param live The live sampler of `terrain`. Must outlive the sampler.
		/// @param terrain The terrain. Must outlive the sampler.
		/// @param contribution The contribution to undo; its entries are copied.
		BaseTerrainSampler(const EditorTerrainSampler& live, terrain::Terrain& terrain, const pcg::TerrainContribution& contribution);

	public:
		/// @copydoc pcg::graph::BaseTerrainView::Height
		bool Height(float x, float z, float& h) const override { return m_view.Height(x, z, h); }

		/// @copydoc pcg::graph::BaseTerrainView::Normal
		Vector3 Normal(float x, float z) const override { return m_view.Normal(x, z); }

		/// @copydoc pcg::graph::BaseTerrainView::SplatWeight
		float SplatWeight(const String& textureKey, float x, float z) const override { return m_view.SplatWeight(textureKey, x, z); }

		/// @copydoc pcg::graph::BaseTerrainView::WaterLevel
		bool WaterLevel(float x, float z, float& level) const override { return m_view.WaterLevel(x, z, level); }

		/// @copydoc pcg::graph::BaseTerrainView::Revision
		uint64 Revision() const override { return m_view.Revision(); }

		/// @brief True if the contribution changed tile materials, which are not undone.
		[[nodiscard]] bool HasMaterialChanges() const { return m_view.HasMaterialChanges(); }

	private:
		EditorTerrainAccess m_access;
		pcg::graph::BaseTerrainView m_view;
	};
}
