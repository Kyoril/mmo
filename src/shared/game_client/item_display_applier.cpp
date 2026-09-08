// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "item_display_applier.h"

#include "scene_graph/entity.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh.h"
#include "scene_graph/scene.h"
#include "scene_graph/skeleton.h"
#include "scene_graph/sub_entity.h"
#include "scene_graph/sub_mesh.h"
#include "scene_graph/tag_point.h"
#include "shared/client_data/proto_client/item_display.pb.h"

namespace mmo
{
	namespace
	{
		/// Changes the visibility of every sub entity whose sub mesh carries the given tag.
		void SetSubEntityVisibilityByTag(Entity& entity, const String& tag, const bool visible)
		{
			for (uint32 i = 0; i < entity.GetNumSubEntities(); ++i)
			{
				SubMesh& subMesh = entity.GetMesh()->GetSubMesh(static_cast<uint16>(i));
				if (!subMesh.HasTag(tag))
				{
					continue;
				}

				SubEntity* subEntity = entity.GetSubEntity(static_cast<uint16>(i));
				ASSERT(subEntity);
				subEntity->SetVisible(visible);
			}
		}

		/// Applies a single matching display variant to the given character entity.
		void ApplyItemDisplayVariant(Scene& scene, Entity& entity, const uint32 itemDisplayId,
			const proto_client::ItemDisplayVariant& variant, const bool weaponsDrawn, ItemDisplayAttachmentMap& attachments)
		{
			for (const auto& subEntityName : variant.hidden_by_name())
			{
				if (SubEntity* sub = entity.GetSubEntity(subEntityName))
				{
					sub->SetVisible(false);
				}
			}

			for (const auto& tag : variant.hidden_by_tag())
			{
				SetSubEntityVisibilityByTag(entity, tag, false);
			}

			for (const auto& subEntityName : variant.shown_by_name())
			{
				if (SubEntity* sub = entity.GetSubEntity(subEntityName))
				{
					sub->SetVisible(true);
				}
			}

			for (const auto& tag : variant.shown_by_tag())
			{
				SetSubEntityVisibilityByTag(entity, tag, true);
			}

			for (const auto& materialOverride : variant.material_overrides())
			{
				SubEntity* sub = entity.GetSubEntity(materialOverride.first);
				if (!sub)
				{
					continue;
				}

				if (MaterialPtr material = MaterialManager::Get().Load(materialOverride.second))
				{
					sub->SetMaterial(material);
				}
			}

			// Do we already have that item display attached?
			if (attachments.contains(itemDisplayId))
			{
				return;
			}

			if (!variant.has_mesh() || variant.mesh().empty())
			{
				return;
			}

			if (!entity.HasSkeleton())
			{
				return;
			}

			// Pick the bone for the current draw state (drawn while in combat, sheathed otherwise).
			// Weapons define both; armor only defines the default bone.
			const proto_client::ItemDisplayBoneAttachment* bone = SelectItemBoneAttachment(variant, weaponsDrawn);
			if (!bone || !entity.GetSkeleton()->HasBone(bone->bone_name()))
			{
				return;
			}

			ItemDisplayAttachment attachment;
			attachment.variant = &variant;
			attachment.entity = scene.CreateEntity(entity.GetName() + "_ITEM_" + std::to_string(itemDisplayId), variant.mesh());
			attachment.attachment = entity.AttachObjectToBone(bone->bone_name(), *attachment.entity);
			ApplyItemBoneTransform(attachment.attachment, *bone);
			attachments[itemDisplayId] = attachment;
		}
	}

	const proto_client::ItemDisplayBoneAttachment* SelectItemBoneAttachment(const proto_client::ItemDisplayVariant& variant, const bool drawn)
	{
		if (drawn && variant.has_attached_bone_drawn())
		{
			return &variant.attached_bone_drawn();
		}

		if (!drawn && variant.has_attached_bone_sheath())
		{
			return &variant.attached_bone_sheath();
		}

		if (variant.has_attached_bone_default())
		{
			return &variant.attached_bone_default();
		}

		// No default defined: use whichever state-specific bone exists so the item still attaches.
		if (variant.has_attached_bone_drawn())
		{
			return &variant.attached_bone_drawn();
		}

		if (variant.has_attached_bone_sheath())
		{
			return &variant.attached_bone_sheath();
		}

		return nullptr;
	}

	void ApplyItemBoneTransform(TagPoint* tagPoint, const proto_client::ItemDisplayBoneAttachment& bone)
	{
		if (!tagPoint)
		{
			return;
		}

		tagPoint->SetPosition(Vector3(bone.offset_x(), bone.offset_y(), bone.offset_z()));
		tagPoint->SetOrientation(Quaternion(bone.rotation_w(), bone.rotation_x(), bone.rotation_y(), bone.rotation_z()));
		tagPoint->SetScale(Vector3(bone.scale_x(), bone.scale_y(), bone.scale_z()));
	}

	void ApplyItemDisplay(Scene& scene, Entity& entity, const uint32 modelDisplayId, const uint32 itemDisplayId,
		const proto_client::ItemDisplayEntry& display, const bool weaponsDrawn, ItemDisplayAttachmentMap& attachments)
	{
		for (const auto& variant : display.variants())
		{
			// Does this variant affect the character model we are applying to?
			if (variant.model() != 0 && variant.model() != modelDisplayId)
			{
				continue;
			}

			ApplyItemDisplayVariant(scene, entity, itemDisplayId, variant, weaponsDrawn, attachments);
		}
	}

	void ClearItemDisplayAttachments(Scene& scene, Entity& entity, ItemDisplayAttachmentMap& attachments)
	{
		for (const auto& [displayId, attachment] : attachments)
		{
			if (!attachment.entity)
			{
				continue;
			}

			entity.DetachObjectFromBone(attachment.entity->GetName());
			scene.DestroyEntity(*attachment.entity);
		}

		attachments.clear();
	}

	void RefreshItemDisplayAttachmentBones(Entity& entity, ItemDisplayAttachmentMap& attachments, const bool weaponsDrawn)
	{
		if (!entity.HasSkeleton())
		{
			return;
		}

		for (auto& [displayId, attachment] : attachments)
		{
			if (!attachment.variant || !attachment.entity)
			{
				continue;
			}

			// Only items that define a drawn or sheathed bone (i.e. weapons) move between states.
			if (!attachment.variant->has_attached_bone_drawn() && !attachment.variant->has_attached_bone_sheath())
			{
				continue;
			}

			const proto_client::ItemDisplayBoneAttachment* bone = SelectItemBoneAttachment(*attachment.variant, weaponsDrawn);
			if (!bone || !entity.GetSkeleton()->HasBone(bone->bone_name()))
			{
				continue;
			}

			// Re-attach the existing entity to the bone for the new draw state.
			entity.DetachObjectFromBone(attachment.entity->GetName());
			attachment.attachment = entity.AttachObjectToBone(bone->bone_name(), *attachment.entity);
			ApplyItemBoneTransform(attachment.attachment, *bone);
		}
	}
}
