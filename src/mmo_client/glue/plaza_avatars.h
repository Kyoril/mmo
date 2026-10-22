// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "char_creation/character_avatar_builder.h"
#include "game/character_view.h"
#include "game_client/glue_scene_definition.h"
#include "game_client/item_display_applier.h"
#include "game_client/plaza_interaction.h"
#include "game_client/plaza_roster.h"
#include "graphics/material.h"
#include "math/vector3.h"

#include <memory>
#include <optional>
#include <vector>

namespace mmo
{
	class AnimationState;
	class Entity;
	class MaterialInstance;
	class RaySceneQuery;
	class Scene;
	class SceneNode;
	class SubEntity;
	struct Ray;

	namespace proto_client
	{
		class Project;
	}

	/// The characters of the character list standing on the Haven plaza: up to PlazaSpotCount at
	/// their own spots (selected or not), and visitors (characters beyond the spots) running in
	/// from the entrance to the visitor spot when selected. The character under the cursor reacts
	/// to the hover: a greeting, a partial turn towards the camera and a soft rim glow.
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
		/// and look the same (see PlazaAppearance) are kept; new characters, and characters whose
		/// appearance changed, get a new, hidden avatar until the next Select; avatars of
		/// removed characters are destroyed, unless they are walking out (see Leave), in which
		/// case they are destroyed when they reach the entrance.
		/// @param ordered The characters in roster order.
		void SetCharacters(const std::vector<CharacterView>& ordered);

		/// Destroys every avatar, including those walking out, and forgets the selected character's
		/// rotation, the hover and the characters sent away. Used when the realm changes: guids are only
		/// unique per realm, so no avatar of the previous realm may be reused.
		void Clear();

		/// Places the characters for a selection.
		/// @param orderIndex Order index of the selected character, or -1 for none.
		/// @param animate False places everyone instantly. True makes characters run to their new
		///        place; a selected visitor runs in from the entrance and a previous visitor runs
		///        back to the entrance and is then hidden.
		void Select(int32 orderIndex, bool animate);

		/// Walks a character to the entrance, where it is hidden. Used before a delete. Until
		/// SetCharacters drops the character (or ForgetLeaving is called), Select leaves it alone,
		/// so a selection made before the refreshed character list arrives does not bring it back.
		/// @param orderIndex Order index of the character.
		void Leave(int32 orderIndex);

		/// Lets Select place characters that were sent away with Leave again, for when the
		/// character list arrived and still holds them (the delete failed).
		void ForgetLeaving() { m_leavingGuids.clear(); }

		/// Turns the selected character, wherever it stands.
		/// @param degrees Yaw to add, in degrees.
		void Rotate(float degrees);

		/// Finds the character under a ray: the nearest one whose model bounds the ray hits, among
		/// the characters standing on the plaza (not hidden, running or leaving).
		/// @param ray The ray, usually from the glue camera through the cursor.
		/// @return The character's order index, or -1.
		[[nodiscard]] int32 Pick(const Ray& ray) const;

		/// Sets the character under the cursor, which reacts while it stands still.
		/// @param orderIndex Order index of the character, or -1 for none.
		void SetHovered(int32 orderIndex);

		/// Gets the position a character stands at (or is running towards).
		/// @param orderIndex Order index of the character.
		/// @return The position, or nullopt for characters not on the plaza.
		[[nodiscard]] std::optional<Vector3> GetCharacterPosition(int32 orderIndex) const;

		/// Gets the world position for a character's nameplate: 0.4 m above the head bone, or the
		/// top of the model's bounds when it has no head bone.
		/// @param orderIndex Order index of the character.
		/// @return The position, or nullopt for characters not on the plaza.
		[[nodiscard]] std::optional<Vector3> GetNameplateAnchor(int32 orderIndex) const;

		/// Moves running characters, advances the animations and the hover reaction.
		/// @param deltaSeconds Real time since the last update.
		/// @param cameraPosition The camera's position, which a hovered character turns towards.
		void Update(float deltaSeconds, const Vector3& cameraPosition);

	private:
		/// One character's avatar.
		struct Avatar
		{
			uint64 guid = 0;

			/// What the model was built from, to rebuild it when the character looks different.
			PlazaAppearance appearance;

			/// Positioned and turned by the facing.
			SceneNode* node = nullptr;

			/// Child of node, holding the mesh's base alignment. The entity hangs off this one.
			SceneNode* offsetNode = nullptr;

			/// May be null when the model could not be built.
			Entity* entity = nullptr;
			ItemDisplayAttachmentMap attachments;

			AnimationState* animationState = nullptr;
			String animationName;

			/// The pose the avatar holds while standing at its place.
			String pose;

			bool visible = false;

			Vector3 position;
			float facingDegrees = 0.0f;

			bool walking = false;
			Vector3 walkTarget;
			float arrivalFacingDegrees = 0.0f;
			String arrivalPose;
			bool hideOnArrival = false;

			/// Whether the avatar is the selected character, which Rotate turns.
			bool selected = false;

			/// Glow and turn of the hover reaction.
			PlazaHoverReaction hover;

			/// Yaw added at full turn, in degrees. Fixed when the hover starts, so a rotation while
			/// hovered still turns the character.
			float hoverTurnDegrees = 0.0f;

			/// The greeting clip while it plays, over the pose.
			AnimationState* greetingState = nullptr;
			float greetingElapsed = 0.0f;

			/// A per-avatar material instance carrying the glow tint, and the material it replaced.
			struct GlowMaterial
			{
				SubEntity* subEntity = nullptr;
				MaterialPtr original;
				std::shared_ptr<MaterialInstance> instance;
			};

			/// Empty while the avatar does not glow.
			std::vector<GlowMaterial> glowMaterials;
		};

	private:
		/// Creates a hidden avatar for a character.
		std::unique_ptr<Avatar> CreateAvatar(const CharacterView& view);

		/// Clears the item attachments, then destroys the entity and the nodes.
		void DestroyAvatar(Avatar& avatar);

		/// Takes over another avatar's position, facing, walk, selected flag, pose, animation and
		/// visibility, for a model rebuilt in place of an older one of the same character.
		void AdoptPlacement(Avatar& avatar, const Avatar& from);

		/// Shows or hides the avatar including its item meshes.
		void SetVisible(Avatar& avatar, bool visible);

		/// Puts the avatar at a position and facing, without walking.
		void PlaceAt(Avatar& avatar, const Vector3& position, float facingDegrees);

		/// Starts running towards a target. Drops the hover reaction at once.
		void WalkTo(Avatar& avatar, const Vector3& target, float arrivalFacingDegrees, const String& arrivalPose, bool hideOnArrival);

		/// Loops the named animation, or Idle if the model has no such animation. Run falls back
		/// to Walk before Idle.
		void PlayAnimation(Avatar& avatar, const String& name);

		/// Moves a running avatar. Returns true when it arrived this update.
		bool UpdateWalk(Avatar& avatar, float deltaSeconds);

		/// Applies position and facing to the node; the selected character also gets the
		/// rotation, a hovered one the turn towards the camera.
		void ApplyTransform(Avatar& avatar);

		/// Advances the hover reaction of an avatar: glow, turn and greeting.
		void UpdateHover(Avatar& avatar, float deltaSeconds, const Vector3& cameraPosition);

		/// Starts the greeting clip over the pose, if the model has one and the pose is standing.
		void StartGreeting(Avatar& avatar);

		/// Advances a playing greeting and blends it with the pose; ends it after the clip.
		void UpdateGreeting(Avatar& avatar, float deltaSeconds);

		/// Stops a playing greeting at once and gives the pose its full weight back.
		void StopGreeting(Avatar& avatar);

		/// Sets the glow tint, creating the material instances first if needed.
		void ApplyGlow(Avatar& avatar, float strength);

		/// Puts the original materials back and drops the glow instances.
		void RestoreMaterials(Avatar& avatar);

		/// Ends everything of the hover reaction at once: greeting, turn and glow.
		void ResetHover(Avatar& avatar);

		/// Whether the avatar stands still on the plaza, so it may be picked and react to the cursor.
		[[nodiscard]] bool IsStandingOnPlaza(const Avatar& avatar) const;

		/// Whether the character was sent away with Leave.
		[[nodiscard]] bool IsLeaving(uint64 guid) const;

	private:
		Scene& m_scene;
		const GlueSceneDefinition& m_definition;
		CharacterAvatarBuilder m_builder;

		/// In roster order.
		std::vector<std::unique_ptr<Avatar>> m_avatars;

		/// Removed from the roster while walking out; destroyed on arrival.
		std::vector<std::unique_ptr<Avatar>> m_departing;

		/// Characters sent away with Leave that are still in the roster.
		std::vector<uint64> m_leavingGuids;

		/// The selected character, so the rotation is reset when it changes. 0 for none.
		uint64 m_selectedGuid = 0;
		float m_selectedRotationDegrees = 0.0f;

		/// The character under the cursor. 0 for none.
		uint64 m_hoveredGuid = 0;

		/// Greeting clips to try, in order (from the emote catalog's wave and bow emotes).
		std::vector<String> m_greetingClips;

		/// Finds the avatars under the cursor ray.
		std::unique_ptr<RaySceneQuery> m_rayQuery;

		uint32 m_nextEntityId = 0;
	};
}
