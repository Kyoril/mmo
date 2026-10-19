// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_model.h"

#include "base/typedefs.h"

#include <memory>
#include <vector>

namespace mmo
{
	/// @brief A prop kit is a standalone asset (.hkit) holding props and lights that can be
	/// applied to a placed world model. Props and lights are stored in world model space.
	struct PropKit
	{
		/// @brief Display name of the kit.
		String name;

		/// @brief Path of the world model (.hwmo) this kit was authored against, used for previews.
		String previewWorldModel;

		/// @brief The props of this kit.
		std::vector<WorldModelMeshRef> props;

		/// @brief The lights of this kit.
		std::vector<WorldModelLight> lights;
	};

	typedef std::shared_ptr<PropKit> PropKitPtr;
}
