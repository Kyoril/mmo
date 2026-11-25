// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glider_display.h"

#include "shared/client_data/proto_client/item_display.pb.h"

namespace mmo
{
	std::optional<GliderAttachment> ResolveGliderAttachment(const proto_client::ItemDisplayEntry& display,
		const uint32 modelDisplayId, const ModelMeshResolver& resolveMesh)
	{
		for (const auto& variant : display.variants())
		{
			if (!ItemDisplayVariantAppliesToModel(variant.model(), modelDisplayId, resolveMesh))
			{
				continue;
			}

			if (!variant.has_mesh() || variant.mesh().empty() || !variant.has_attached_bone_default())
			{
				return std::nullopt;
			}

			const auto& bone = variant.attached_bone_default();
			GliderAttachment attachment;
			attachment.mesh = variant.mesh();
			attachment.bone = bone.bone_name();
			attachment.offset = Vector3(bone.offset_x(), bone.offset_y(), bone.offset_z());
			attachment.rotation = Quaternion(bone.rotation_w(), bone.rotation_x(), bone.rotation_y(), bone.rotation_z());
			attachment.scale = Vector3(bone.scale_x(), bone.scale_y(), bone.scale_z());
			return attachment;
		}

		return std::nullopt;
	}
}
