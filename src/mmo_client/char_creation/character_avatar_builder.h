// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "game/character_customization/customizable_avatar_definition.h"
#include "game_client/item_display_applier.h"

namespace mmo
{
	class CharacterView;
	class Entity;
	class Scene;
	class SceneNode;

	namespace proto_client
	{
		class Project;
	}

	/// Builds the 3D avatar of a character from the character list: its model, its customization
	/// (sub entity visibility and material overrides) and the meshes of its equipped items.
	class CharacterAvatarBuilder final : public CustomizationPropertyGroupApplier
	{
	public:
		/// Creates a builder.
		/// @param project The client project, for models and item displays. Must outlive this.
		explicit CharacterAvatarBuilder(const proto_client::Project& project);

		~CharacterAvatarBuilder() override = default;

	public:
		/// Creates the entity of a character, attaches it to a node and applies its customization
		/// and equipment. Weapons are shown on their sheathed bones.
		/// @param scene The scene to create the entity and its item attachments in.
		/// @param node The node the entity is attached to.
		/// @param view The character to build.
		/// @param entityName Name of the entity. Must be unique in the scene; item attachments
		///        derive their names from it.
		/// @param attachments Receives the created item attachments. The caller must clear them
		///        with ClearItemDisplayAttachments before destroying the entity.
		/// @return The created entity, or nullptr if the character's model could not be resolved.
		Entity* Build(Scene& scene, SceneNode& node, const CharacterView& view, const String& entityName, ItemDisplayAttachmentMap& attachments);

	public:
		/// @copydoc CustomizationPropertyGroupApplier::Apply
		void Apply(const VisibilitySetPropertyGroup& group, const AvatarConfiguration& configuration) override;

		/// @copydoc CustomizationPropertyGroupApplier::Apply
		void Apply(const MaterialOverridePropertyGroup& group, const AvatarConfiguration& configuration) override;

		/// @copydoc CustomizationPropertyGroupApplier::Apply
		void Apply(const ScalarParameterPropertyGroup& group, const AvatarConfiguration& configuration) override;

	private:
		const proto_client::Project& m_project;

		/// The entity the Apply overloads work on, only set while Build runs.
		Entity* m_entity = nullptr;
	};
}
