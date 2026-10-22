// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/quaternion.h"
#include "math/vector3.h"
#include "scene_graph/mesh.h"
#include "scene_graph/world_model.h"

#include <vector>

namespace mmo
{
	/// @brief A placement (.wobj) the world editor has loaded, as the glue bake reads it.
	struct GlueBakePlacement
	{
		/// @brief Whether this places a world model (.hwmo) rather than a plain mesh.
		bool isWorldModel = false;

		/// @brief Path of the placed asset (.hwmo or .hmsh).
		String assetPath;

		/// @brief World transform of the placement.
		Vector3 position;
		Quaternion rotation;
		Vector3 scale { 1.0f, 1.0f, 1.0f };

		/// @brief The placed world model (world models only).
		WorldModelPtr worldModel;

		/// @brief The active doodad set of the world model instance.
		uint32 doodadSet = 0;

		/// @brief Ordered prop kit paths (world models only).
		std::vector<String> kitPaths;

		/// @brief The placed mesh (plain meshes only).
		MeshPtr mesh;

		/// @brief Material overrides per submesh (plain meshes only); an empty entry keeps the mesh's
		///        own material. Empty when nothing is overridden.
		std::vector<String> materialOverrides;
	};
}
