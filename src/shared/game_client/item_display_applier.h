// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <unordered_map>

namespace mmo
{
	namespace proto_client
	{
		class ItemDisplayEntry;
		class ItemDisplayVariant;
		class ItemDisplayBoneAttachment;
	}

	class Entity;
	class Scene;
	class TagPoint;

	/// A mesh which was attached to a character entity because of an equipped item's display data.
	struct ItemDisplayAttachment
	{
		/// The entity which was created for the attached mesh.
		Entity* entity{ nullptr };

		/// The tag point the entity is attached to.
		TagPoint* attachment{ nullptr };

		/// The display variant this attachment was created from. Used to re-resolve the correct
		/// bone (drawn vs. sheathed) when the combat state changes.
		const proto_client::ItemDisplayVariant* variant{ nullptr };
	};

	/// Maps an item display id to the attachment it created on a character entity.
	using ItemDisplayAttachmentMap = std::unordered_map<uint32, ItemDisplayAttachment>;

	/// Selects the bone attachment to use for a variant given the current draw state, falling back
	/// to the default attachment when a state specific one is not defined.
	/// @param variant The display variant to pick a bone attachment from.
	/// @param drawn Whether the weapon is currently drawn.
	/// @return The bone attachment to use, or nullptr if the variant does not define any.
	const proto_client::ItemDisplayBoneAttachment* SelectItemBoneAttachment(const proto_client::ItemDisplayVariant& variant, bool drawn);

	/// Applies a bone attachment's offset, rotation and scale to a tag point.
	/// @param tagPoint The tag point to transform. Nullptr is tolerated and does nothing.
	/// @param bone The bone attachment holding the transform to apply.
	void ApplyItemBoneTransform(TagPoint* tagPoint, const proto_client::ItemDisplayBoneAttachment& bone);

	/// Applies the visual effects of a single item display entry to a character entity: sub entity
	/// visibility changes, sub entity material overrides and attached meshes.
	/// @param scene The scene the character entity lives in. Attachment entities are created here.
	/// @param entity The character entity to apply the display data to.
	/// @param modelDisplayId The display id of the character model. Variants are filtered by it.
	/// @param itemDisplayId The id of the item display entry, used as the attachment map key.
	/// @param display The item display entry to apply.
	/// @param weaponsDrawn Whether the character currently has its weapons drawn.
	/// @param attachments Receives created attachments. An item display which already has an entry
	///	       here does not create a second attachment mesh.
	void ApplyItemDisplay(Scene& scene, Entity& entity, uint32 modelDisplayId, uint32 itemDisplayId,
		const proto_client::ItemDisplayEntry& display, bool weaponsDrawn, ItemDisplayAttachmentMap& attachments);

	/// Detaches and destroys every attachment mesh in the given map and clears the map.
	/// @param scene The scene the attachment entities were created in.
	/// @param entity The character entity the attachments are attached to.
	/// @param attachments The attachments to remove.
	void ClearItemDisplayAttachments(Scene& scene, Entity& entity, ItemDisplayAttachmentMap& attachments);

	/// Re-attaches every weapon attachment to the bone which matches the given draw state.
	/// Attachments which only define a default bone (armor) are left untouched.
	/// @param entity The character entity the attachments are attached to.
	/// @param attachments The attachments to update.
	/// @param weaponsDrawn Whether the character currently has its weapons drawn.
	void RefreshItemDisplayAttachmentBones(Entity& entity, ItemDisplayAttachmentMap& attachments, bool weaponsDrawn);
}
