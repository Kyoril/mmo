// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "char_creation/character_avatar_builder.h"
#include "game/character_view.h"
#include "game_client/glue_scene_definition.h"
#include "game_client/item_display_applier.h"
#include "math/vector3.h"

#include <memory>
#include <optional>
#include <vector>

namespace mmo
{
	class AnimationState;
	class Entity;
	class Scene;
	class SceneNode;

	namespace proto_client
	{
		class Project;
	}

	/// The characters of the character list standing on the Haven plaza: up to PlazaSpotCount at
	/// their spots, the selected one at the front, and visitors (characters beyond the spots)
	/// walking in from the entrance when selected.
	/// @remark Main thread only. Must be destroyed before the scene is cleared.
	class PlazaAvatars final : public NonCopyable
	{
	public:
		/// Creates an empty plaza.
		/// @param scene The scene to create the avatars in. Must outlive this.
		/// @param project The client project. Must outlive this.
		/// @param definition The scene definition with the spots. Must outlive this.
		PlazaAvatars(Scene& scene, const proto_client::Project& project, const GlueSceneDefinition& definition);

		/// Destroys every avatar.
		~PlazaAvatars();

	public:
		/// Sets the characters in roster order. Avatars of characters that are still in the list
		/// are kept; new characters get a new, hidden avatar until the next Select; avatars of
		/// removed characters are destroyed, unless they are walking out (see Leave), in which
		/// case they are destroyed when they reach the entrance.
		/// @param ordered The characters in roster order.
		void SetCharacters(const std::vector<CharacterView>& ordered);

		/// Places the characters for a selection.
		/// @param orderIndex Order index of the selected character, or -1 for none.
		/// @param animate False places everyone instantly. True makes characters walk to their new
		///        place; a selected visitor walks in from the entrance and a previous visitor walks
		///        back to the entrance and is then hidden.
		void Select(int32 orderIndex, bool animate);

		/// Walks a character to the entrance, where it is hidden. Used before a delete.
		/// @param orderIndex Order index of the character.
		void Leave(int32 orderIndex);

		/// Turns the front character.
		/// @param degrees Yaw to add, in degrees.
		void Rotate(float degrees);

		/// Gets the world position for a character's nameplate: 0.4 m above the head bone, or the
		/// top of the model's bounds when it has no head bone.
		/// @param orderIndex Order index of the character.
		/// @return The position, or nullopt for characters not on the plaza.
		[[nodiscard]] std::optional<Vector3> GetNameplateAnchor(int32 orderIndex) const;

		/// Moves walking characters and advances the animations.
		/// @param deltaSeconds Real time since the last update.
		void Update(float deltaSeconds);

	private:
		/// One character's avatar.
		struct Avatar
		{
			uint64 guid = 0;

			/// Positioned and turned by the facing.
			SceneNode* node = nullptr;

			/// Child of node, holding the mesh's base alignment. The entity hangs off this one.
			SceneNode* offsetNode = nullptr;

			/// May be null when the model could not be built.
			Entity* entity = nullptr;
			ItemDisplayAttachmentMap attachments;

			AnimationState* animationState = nullptr;
			String animationName;

			bool visible = false;

			Vector3 position;
			float facingDegrees = 0.0f;

			bool walking = false;
			Vector3 walkTarget;
			float arrivalFacingDegrees = 0.0f;
			String arrivalPose;
			bool hideOnArrival = false;

			/// Whether the avatar is (or is walking to) the front spot, where Rotate turns it.
			bool atFront = false;
		};

	private:
		/// Creates a hidden avatar for a character.
		std::unique_ptr<Avatar> CreateAvatar(const CharacterView& view);

		/// Clears the item attachments, then destroys the entity and the nodes.
		void DestroyAvatar(Avatar& avatar);

		/// Shows or hides the avatar including its item meshes.
		void SetVisible(Avatar& avatar, bool visible);

		/// Puts the avatar at a position and facing, without walking.
		void PlaceAt(Avatar& avatar, const Vector3& position, float facingDegrees);

		/// Starts walking towards a target.
		void WalkTo(Avatar& avatar, const Vector3& target, float arrivalFacingDegrees, const String& arrivalPose, bool hideOnArrival);

		/// Loops the named animation, or Idle if the model has no such animation.
		void PlayAnimation(Avatar& avatar, const String& name);

		/// Moves a walking avatar. Returns true when it arrived this update.
		bool UpdateWalk(Avatar& avatar, float deltaSeconds);

		/// Applies position and facing to the node; the front character also gets the rotation.
		void ApplyTransform(Avatar& avatar);

	private:
		Scene& m_scene;
		const GlueSceneDefinition& m_definition;
		CharacterAvatarBuilder m_builder;

		/// In roster order.
		std::vector<std::unique_ptr<Avatar>> m_avatars;

		/// Removed from the roster while walking out; destroyed on arrival.
		std::vector<std::unique_ptr<Avatar>> m_departing;

		/// The character at the front, so the rotation is reset when it changes. 0 for none.
		uint64 m_frontGuid = 0;
		float m_frontRotationDegrees = 0.0f;

		uint32 m_nextEntityId = 0;
	};
}
