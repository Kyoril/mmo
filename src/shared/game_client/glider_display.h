// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/item_display_model_match.h"
#include "math/quaternion.h"
#include "math/vector3.h"

#include <optional>

namespace mmo
{
	namespace proto_client
	{
		class ItemDisplayEntry;
	}

	/// @brief Where and how a glider is attached to a character, taken from its item display.
	struct GliderAttachment
	{
		/// Mesh of the glider.
		String mesh;
		/// Bone of the character the glider is attached to.
		String bone;
		/// Offset from the bone, in bone space.
		Vector3 offset{ Vector3::Zero };
		/// Rotation relative to the bone.
		Quaternion rotation{ Quaternion::Identity };
		/// Scale of the attachment.
		Vector3 scale{ Vector3::UnitScale };
	};

	/// @brief Resolves the glider attachment of an item display for a character model: the first
	///	variant that applies to the model (as for worn items) and has a mesh and a default bone
	///	attachment.
	/// @param display The glider item's display entry.
	/// @param modelDisplayId Model id of the character the glider is drawn on.
	/// @param resolveMesh Resolves model ids to meshes for shared-mesh variant matching (may be empty).
	/// @return The attachment, or nothing if no variant applies or the variant lacks a mesh or bone.
	[[nodiscard]] std::optional<GliderAttachment> ResolveGliderAttachment(const proto_client::ItemDisplayEntry& display,
		uint32 modelDisplayId, const ModelMeshResolver& resolveMesh);
}
