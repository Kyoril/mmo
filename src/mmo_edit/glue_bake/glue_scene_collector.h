// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game_common/glue_bake.h"
#include "game_common/glue_scene_definition.h"
#include "math/aabb.h"
#include "math/matrix4.h"
#include "paging/page.h"
#include "scene_graph/mesh.h"

#include <vector>

namespace mmo
{
	class WorldEditorInstance;

	/// @brief One mesh the glue cameras can see, in world space.
	struct GlueRenderItem
	{
		MeshPtr mesh;
		String meshPath;

		/// @brief Mesh-to-world transform.
		Matrix4 world;

		/// @brief Material per submesh (one entry per submesh); an empty entry keeps the mesh's own
		///        material. Empty when nothing is overridden.
		std::vector<String> materialOverrides;

		/// @brief Whether the mesh can be merged into static world-space geometry (no skeleton,
		///        standard static vertex layout; see WorldModelInstance::CanMergeMesh).
		bool mergeable = false;

		/// @brief The mesh bounds transformed into world space.
		AABB worldBounds;
	};

	/// @brief Everything a glue bake takes from the world.
	struct GlueCollection
	{
		std::vector<GlueRenderItem> items;
		std::vector<GlueBakeFoliage> foliage;
		std::vector<GlueBakeLight> lights;
		std::vector<GlueBakeSound> sounds;
		std::vector<GlueBakeParticles> particles;

		/// @brief Terrain pages (with a .tile file) whose bounds the frusta intersect.
		std::vector<PagePosition> terrainPages;

		/// @brief Every file the collection read, whether what it holds was kept or culled, with its
		///        content hash, sorted by path. A file that could not be read has the hash "missing".
		std::vector<GlueBakeInputFile> inputs;

		/// @brief Pure interior WMO groups left out (once per placed occurrence).
		uint32 skippedInterior = 0;

		/// @brief Pure interior WMO groups whose own meshes were kept as facades (once per placed
		///        occurrence); their doodads, kit props, lights and child world models are still skipped.
		uint32 includedInterior = 0;

		/// @brief Meshes and foliage left out because they never reach the small-object threshold.
		uint32 skippedSmall = 0;

		/// @brief Items that cannot be merged (mergeable == false).
		uint32 unmergeable = 0;

		/// @brief Meshes and foliage left out because no frustum sees them.
		uint32 skippedOutside = 0;

		/// @brief Mesh references, doodads and kit props left out because they lie in an interior group.
		uint32 skippedInteriorMeshes = 0;

		/// @brief Child world models left out because their origin lies in an interior group of their parent.
		uint32 skippedInteriorChildren = 0;

		/// @brief Meshes that failed to load.
		uint32 missingMeshes = 0;

		/// @brief Sound emitters left out because no camera pose is within their audible range.
		uint32 inaudibleSounds = 0;

		/// @brief Pages whose entities and foliage were scanned.
		std::vector<PagePosition> entityPages;
	};

	/// @brief Collects what the glue cameras can see from the world open in the editor: every placement
	///        on the pages around the visible terrain (world models flattened through their exterior
	///        groups, child world models, active doodads and prop kits; plain meshes), authored foliage,
	///        the lights, sound emitters and particle systems they bring, and the visible terrain pages.
	/// @details Loads the entities of the pages it needs and releases them again before it returns.
	/// @param world The world editor instance holding the glue scene's world.
	/// @param def The glue scene definition (only used for the sound audibility test via the frusta).
	/// @param frusta The glue camera frusta.
	/// @param smallObjectPixels Meshes whose largest projected size at 1080p stays below this are dropped.
	GlueCollection CollectGlueScene(WorldEditorInstance& world, const GlueSceneDefinition& def, const GlueBakeFrustumSet& frusta, float smallObjectPixels = 2.0f);
}
