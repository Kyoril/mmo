// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "plaza_avatars.h"

#include "client_data/project.h"
#include "game_client/plaza_roster.h"
#include "graphics/material_instance.h"
#include "math/degree.h"
#include "math/math_utils.h"
#include "math/quaternion.h"
#include "math/ray.h"
#include "math/vector4.h"
#include "scene_graph/animation_state.h"
#include "scene_graph/entity.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/skeleton_instance.h"
#include "scene_graph/sub_entity.h"

#include <algorithm>
#include <string>

namespace mmo
{
	namespace
	{
		/// Running speed in metres per second.
		constexpr float runSpeed = 7.0f;

		/// Distance below which a character counts as standing at its target.
		constexpr float arrivalEpsilon = 0.01f;

		/// Height of the nameplate anchor above the head bone, in metres.
		constexpr float nameplateHeadOffset = 0.4f;

		/// Largest turn of a hovered character towards the camera, in degrees.
		constexpr float hoverMaxTurnDegrees = 35.0f;

		/// Seconds the greeting takes to blend in over the pose, and back out at its end.
		constexpr float greetingFadeInSeconds = 0.2f;
		constexpr float greetingFadeOutSeconds = 0.3f;

		/// Warm ivory of the hover glow at full strength. The character materials add the "Tint"
		/// parameter as an emissive rim (Tint x Fresnel), so it brightens the silhouette without
		/// colouring the gear.
		const Vector4 glowTint(0.85f, 0.8f, 0.68f, 1.0f);

		/// Name of the material parameter carrying the glow (see GameUnitC::UpdateTintOnMaterials).
		const char* const glowParameter = "Tint";

		/// Query flag of the avatar entities, so the cursor ray only collects them.
		constexpr uint32 avatarQueryFlag = 1u << 10;

		const String idleAnimation = "Idle";
		const String walkAnimation = "Walk";
		const String runAnimation = "Run";

		/// Greeting clips tried when the emote catalog has no wave or bow emote.
		const char* const fallbackGreetingClips[] = { "EmoteWave", "Bow" };

		/// Emote aliases whose clips greet, in order of preference.
		const char* const greetingEmoteAliases[] = { "wave", "bow" };

		/// Facing of a straight walk from one position to another, in degrees.
		float TravelFacingDegrees(const Vector3& from, const Vector3& to)
		{
			return Radian(DirectionToFacing(to.x - from.x, to.z - from.z)).GetValueDegrees();
		}

		void AddUnique(std::vector<String>& list, const String& value)
		{
			if (!value.empty() && std::find(list.begin(), list.end(), value) == list.end())
			{
				list.push_back(value);
			}
		}
	}

	PlazaAvatars::PlazaAvatars(Scene& scene, const proto_client::Project& project, const GlueSceneDefinition& definition)
		: m_scene(scene)
		, m_definition(definition)
		, m_builder(project)
	{
		// The greeting comes from the emote catalog (its wave, then its bow emote), so it uses the
		// same clips the emotes play in the world.
		for (const char* alias : greetingEmoteAliases)
		{
			for (const auto& entry : project.emotes.getTemplates().entry())
			{
				if (std::find(entry.aliases().begin(), entry.aliases().end(), alias) != entry.aliases().end())
				{
					AddUnique(m_greetingClips, entry.animation());
				}
			}
		}

		for (const char* clip : fallbackGreetingClips)
		{
			AddUnique(m_greetingClips, clip);
		}

		m_rayQuery = m_scene.CreateRayQuery(Ray());
		m_rayQuery->SetQueryMask(avatarQueryFlag);
		m_rayQuery->SetSortByDistance(true);
	}

	PlazaAvatars::~PlazaAvatars()
	{
		Clear();
	}

	void PlazaAvatars::SetCharacters(const std::vector<CharacterView>& ordered)
	{
		std::vector<std::unique_ptr<Avatar>> previous = std::move(m_avatars);
		m_avatars.clear();
		m_avatars.reserve(ordered.size());

		for (const CharacterView& view : ordered)
		{
			auto it = std::find_if(previous.begin(), previous.end(), [&view](const std::unique_ptr<Avatar>& avatar)
				{
					return avatar && avatar->guid == view.GetGuid();
				});

			if (it == previous.end())
			{
				m_avatars.push_back(CreateAvatar(view));
				continue;
			}

			std::unique_ptr<Avatar> existing = std::move(*it);
			previous.erase(it);

			if (existing->appearance == MakePlazaAppearance(view))
			{
				m_avatars.push_back(std::move(existing));
				continue;
			}

			// Same character, different look: rebuild the model where the old one stood.
			std::unique_ptr<Avatar> rebuilt = CreateAvatar(view);
			AdoptPlacement(*rebuilt, *existing);
			DestroyAvatar(*existing);
			m_avatars.push_back(std::move(rebuilt));
		}

		// A character that left and is no longer in the roster needs no protection anymore.
		m_leavingGuids.erase(std::remove_if(m_leavingGuids.begin(), m_leavingGuids.end(), [&ordered](const uint64 guid)
			{
				return std::none_of(ordered.begin(), ordered.end(), [guid](const CharacterView& view) { return view.GetGuid() == guid; });
			}), m_leavingGuids.end());

		for (auto& avatar : previous)
		{
			if (avatar->walking && avatar->hideOnArrival)
			{
				// Running out after a delete: let them finish the run.
				m_departing.push_back(std::move(avatar));
				continue;
			}

			DestroyAvatar(*avatar);
		}
	}

	void PlazaAvatars::Clear()
	{
		for (const auto& avatar : m_avatars)
		{
			DestroyAvatar(*avatar);
		}
		m_avatars.clear();

		for (const auto& avatar : m_departing)
		{
			DestroyAvatar(*avatar);
		}
		m_departing.clear();

		m_leavingGuids.clear();
		m_selectedGuid = 0;
		m_selectedRotationDegrees = 0.0f;
		m_hoveredGuid = 0;
	}

	void PlazaAvatars::Select(int32 orderIndex, const bool animate)
	{
		const int32 numCharacters = static_cast<int32>(m_avatars.size());
		if (orderIndex < 0 || orderIndex >= numCharacters)
		{
			orderIndex = -1;
		}

		const PlazaAssignment assignment = AssignPlaza(numCharacters, orderIndex);

		const uint64 selectedGuid = orderIndex >= 0 ? m_avatars[orderIndex]->guid : 0;
		if (selectedGuid != m_selectedGuid)
		{
			m_selectedRotationDegrees = 0.0f;
			m_selectedGuid = selectedGuid;
		}

		for (int32 index = 0; index < numCharacters; ++index)
		{
			Avatar& avatar = *m_avatars[index];

			if (IsLeaving(avatar.guid))
			{
				// Running out before a delete: it must not come back while the list is refreshed.
				avatar.selected = false;
				continue;
			}

			const GlueSpot* target = nullptr;
			const bool isVisitor = index == assignment.visitor;
			if (isVisitor)
			{
				target = &m_definition.front;
			}
			else
			{
				for (int32 spot = 0; spot < PlazaSpotCount; ++spot)
				{
					if (assignment.spotCharacter[spot] == index)
					{
						target = &m_definition.spots[spot];
						break;
					}
				}
			}

			const bool wasSelected = avatar.selected;
			avatar.selected = index == orderIndex;
			if (wasSelected != avatar.selected)
			{
				// The rotation belongs to the selected character only.
				ApplyTransform(avatar);
			}

			if (target == nullptr)
			{
				// Not on the plaza for this selection.
				avatar.pose.clear();
				if (!animate)
				{
					ResetHover(avatar);
					avatar.walking = false;
					avatar.hideOnArrival = false;
					SetVisible(avatar, false);
				}
				else if (avatar.visible && !(avatar.walking && avatar.hideOnArrival))
				{
					WalkTo(avatar, m_definition.entrance, avatar.facingDegrees, idleAnimation, true);
				}

				continue;
			}

			const String& pose = target->pose;
			avatar.pose = pose;

			if (!animate)
			{
				ResetHover(avatar);
				avatar.walking = false;
				avatar.hideOnArrival = false;
				PlaceAt(avatar, target->position, target->facingDegrees);
				PlayAnimation(avatar, pose);
				SetVisible(avatar, true);
				continue;
			}

			if (avatar.visible)
			{
				const bool atTarget = !avatar.walking && (target->position - avatar.position).GetLength() <= arrivalEpsilon;
				if (atTarget)
				{
					// Already standing there (selecting a plaza character moves nobody): keep the pose
					// and any hover reaction running. Playing the same pose again changes nothing.
					PlayAnimation(avatar, pose);
					continue;
				}

				WalkTo(avatar, target->position, target->facingDegrees, pose, false);
				continue;
			}

			if (isVisitor)
			{
				// A visitor runs in from the street.
				PlaceAt(avatar, m_definition.entrance, TravelFacingDegrees(m_definition.entrance, target->position));
				SetVisible(avatar, true);
				WalkTo(avatar, target->position, target->facingDegrees, pose, false);
				continue;
			}

			// A plaza character that was not shown yet (a new character): it simply stands there.
			avatar.walking = false;
			avatar.hideOnArrival = false;
			PlaceAt(avatar, target->position, target->facingDegrees);
			PlayAnimation(avatar, pose);
			SetVisible(avatar, true);
		}
	}

	void PlazaAvatars::Leave(const int32 orderIndex)
	{
		if (orderIndex < 0 || orderIndex >= static_cast<int32>(m_avatars.size()))
		{
			return;
		}

		Avatar& avatar = *m_avatars[orderIndex];
		avatar.selected = false;

		if (!IsLeaving(avatar.guid))
		{
			m_leavingGuids.push_back(avatar.guid);
		}

		if (!avatar.visible)
		{
			return;
		}

		WalkTo(avatar, m_definition.entrance, avatar.facingDegrees, idleAnimation, true);
	}

	void PlazaAvatars::Rotate(const float degrees)
	{
		m_selectedRotationDegrees += degrees;

		for (const auto& avatar : m_avatars)
		{
			if (avatar->selected)
			{
				ApplyTransform(*avatar);
			}
		}
	}

	int32 PlazaAvatars::Pick(const Ray& ray) const
	{
		m_rayQuery->ClearResult();
		m_rayQuery->SetRay(ray);

		std::vector<PlazaPickHit> hits;
		for (const RaySceneQueryResultEntry& entry : m_rayQuery->Execute())
		{
			for (size_t index = 0; index < m_avatars.size(); ++index)
			{
				const Avatar& avatar = *m_avatars[index];
				if (avatar.entity == nullptr || entry.movable != avatar.entity)
				{
					continue;
				}

				hits.push_back(PlazaPickHit{ static_cast<int32>(index), entry.distance, IsStandingOnPlaza(avatar) });
				break;
			}
		}

		return PickNearestPlazaHit(hits);
	}

	void PlazaAvatars::SetHovered(const int32 orderIndex)
	{
		m_hoveredGuid = (orderIndex >= 0 && orderIndex < static_cast<int32>(m_avatars.size())) ? m_avatars[orderIndex]->guid : 0;
	}

	std::optional<Vector3> PlazaAvatars::GetCharacterPosition(const int32 orderIndex) const
	{
		if (orderIndex < 0 || orderIndex >= static_cast<int32>(m_avatars.size()))
		{
			return std::nullopt;
		}

		const Avatar& avatar = *m_avatars[orderIndex];
		if (!avatar.visible || (avatar.walking && avatar.hideOnArrival))
		{
			return std::nullopt;
		}

		return avatar.walking ? avatar.walkTarget : avatar.position;
	}

	std::optional<Vector3> PlazaAvatars::GetNameplateAnchor(const int32 orderIndex) const
	{
		if (orderIndex < 0 || orderIndex >= static_cast<int32>(m_avatars.size()))
		{
			return std::nullopt;
		}

		const Avatar& avatar = *m_avatars[orderIndex];
		if (!avatar.visible || !avatar.entity || (avatar.walking && avatar.hideOnArrival))
		{
			return std::nullopt;
		}

		if (avatar.entity->HasSkeleton())
		{
			const auto& skeleton = avatar.entity->GetSkeleton();
			for (const char* boneName : { "head", "Head" })
			{
				if (!skeleton->HasBone(boneName))
				{
					continue;
				}

				// Bone positions are skeleton-local; the entity hangs off the offset node, whose
				// transform turns them into world space (same math as projectile targeting).
				const Vector3 local = skeleton->GetBone(boneName)->GetDerivedPosition();
				const SceneNode& node = *avatar.offsetNode;
				const Vector3 head = node.GetDerivedPosition() + node.GetDerivedOrientation() * (node.GetDerivedScale() * local);
				return head + Vector3(0.0f, nameplateHeadOffset, 0.0f);
			}
		}

		const AABB& bounds = avatar.entity->GetWorldBoundingBox(true);
		return Vector3(avatar.position.x, bounds.max.y, avatar.position.z);
	}

	void PlazaAvatars::Update(const float deltaSeconds, const Vector3& cameraPosition)
	{
		for (const auto& avatar : m_avatars)
		{
			if (avatar->walking)
			{
				UpdateWalk(*avatar, deltaSeconds);
			}

			UpdateHover(*avatar, deltaSeconds, cameraPosition);

			if (avatar->visible && avatar->animationState)
			{
				avatar->animationState->AddTime(deltaSeconds);
			}
		}

		for (auto it = m_departing.begin(); it != m_departing.end();)
		{
			Avatar& avatar = **it;
			if (!avatar.walking || UpdateWalk(avatar, deltaSeconds))
			{
				DestroyAvatar(avatar);
				it = m_departing.erase(it);
				continue;
			}

			if (avatar.animationState)
			{
				avatar.animationState->AddTime(deltaSeconds);
			}

			++it;
		}
	}

	std::unique_ptr<PlazaAvatars::Avatar> PlazaAvatars::CreateAvatar(const CharacterView& view)
	{
		auto avatar = std::make_unique<Avatar>();
		avatar->guid = view.GetGuid();
		avatar->appearance = MakePlazaAppearance(view);

		const String id = std::to_string(m_nextEntityId++);

		avatar->node = &m_scene.CreateSceneNode("PlazaAvatarNode_" + id);
		m_scene.GetRootSceneNode().AddChild(*avatar->node);

		// The mesh's base alignment, as for units in the world (GameObjectC::SetupSceneObjects).
		Quaternion rotationOffset;
		rotationOffset.FromAngleAxis(Degree(90.0f), Vector3::UnitY);
		avatar->offsetNode = avatar->node->CreateChildSceneNode(Vector3::Zero, rotationOffset);

		avatar->entity = m_builder.Build(m_scene, *avatar->offsetNode, view, "PlazaAvatar_" + id, avatar->attachments);
		if (avatar->entity)
		{
			// Only the avatars answer the cursor ray.
			avatar->entity->SetQueryFlags(avatarQueryFlag);
		}

		SetVisible(*avatar, false);
		return avatar;
	}

	void PlazaAvatars::DestroyAvatar(Avatar& avatar)
	{
		StopGreeting(avatar);
		RestoreMaterials(avatar);
		avatar.animationState = nullptr;

		if (avatar.entity)
		{
			// Attachments hang off the entity's bones: they go first, or they dangle.
			ClearItemDisplayAttachments(m_scene, *avatar.entity, avatar.attachments);

			avatar.offsetNode->DetachObject(*avatar.entity);
			m_scene.DestroyEntity(*avatar.entity);
			avatar.entity = nullptr;
		}

		avatar.attachments.clear();

		if (avatar.offsetNode)
		{
			m_scene.DestroySceneNode(*avatar.offsetNode);
			avatar.offsetNode = nullptr;
		}

		if (avatar.node)
		{
			m_scene.DestroySceneNode(*avatar.node);
			avatar.node = nullptr;
		}
	}

	void PlazaAvatars::AdoptPlacement(Avatar& avatar, const Avatar& from)
	{
		avatar.position = from.position;
		avatar.facingDegrees = from.facingDegrees;
		avatar.walking = from.walking;
		avatar.walkTarget = from.walkTarget;
		avatar.arrivalFacingDegrees = from.arrivalFacingDegrees;
		avatar.arrivalPose = from.arrivalPose;
		avatar.hideOnArrival = from.hideOnArrival;
		avatar.selected = from.selected;
		avatar.pose = from.pose;
		ApplyTransform(avatar);

		if (!from.animationName.empty())
		{
			PlayAnimation(avatar, from.animationName);
		}

		SetVisible(avatar, from.visible);
	}

	void PlazaAvatars::SetVisible(Avatar& avatar, const bool visible)
	{
		if (!visible)
		{
			ResetHover(avatar);
		}

		avatar.visible = visible;
		avatar.node->SetVisible(visible, true);

		// Item meshes hang off bones, not off the node.
		for (auto& [displayId, attachment] : avatar.attachments)
		{
			if (attachment.entity)
			{
				attachment.entity->SetVisible(visible);
			}
		}
	}

	void PlazaAvatars::PlaceAt(Avatar& avatar, const Vector3& position, const float facingDegrees)
	{
		avatar.position = position;
		avatar.facingDegrees = facingDegrees;
		ApplyTransform(avatar);
	}

	void PlazaAvatars::WalkTo(Avatar& avatar, const Vector3& target, const float arrivalFacingDegrees, const String& arrivalPose, const bool hideOnArrival)
	{
		// Hover never interrupts a run, and a running character does not react.
		ResetHover(avatar);

		avatar.walkTarget = target;
		avatar.arrivalFacingDegrees = arrivalFacingDegrees;
		avatar.arrivalPose = arrivalPose;
		avatar.hideOnArrival = hideOnArrival;

		if ((target - avatar.position).GetLength() <= arrivalEpsilon)
		{
			// Already there: just turn and take the pose.
			avatar.walking = true;
			UpdateWalk(avatar, 0.0f);
			return;
		}

		if (!avatar.walking)
		{
			PlayAnimation(avatar, runAnimation);
		}

		avatar.walking = true;
		avatar.facingDegrees = TravelFacingDegrees(avatar.position, target);
		ApplyTransform(avatar);
	}

	void PlazaAvatars::PlayAnimation(Avatar& avatar, const String& name)
	{
		if (!avatar.entity)
		{
			return;
		}

		// Skeletons without a run cycle walk; anything missing falls back to Idle.
		const String* resolvedPtr = &idleAnimation;
		if (avatar.entity->HasAnimationState(name))
		{
			resolvedPtr = &name;
		}
		else if (name == runAnimation && avatar.entity->HasAnimationState(walkAnimation))
		{
			resolvedPtr = &walkAnimation;
		}

		const String resolved = *resolvedPtr;
		if (avatar.animationState && avatar.animationName == resolved)
		{
			return;
		}

		// A greeting belongs to the pose it plays over.
		StopGreeting(avatar);

		if (avatar.animationState)
		{
			avatar.animationState->SetEnabled(false);
			avatar.animationState = nullptr;
		}

		avatar.animationName = resolved;
		if (!avatar.entity->HasAnimationState(resolved))
		{
			return;
		}

		avatar.animationState = avatar.entity->GetAnimationState(resolved);
		if (avatar.animationState)
		{
			avatar.animationState->SetTimePosition(0.0f);
			avatar.animationState->SetWeight(1.0f);
			avatar.animationState->SetLoop(true);
			avatar.animationState->SetEnabled(true);
		}
	}

	bool PlazaAvatars::UpdateWalk(Avatar& avatar, const float deltaSeconds)
	{
		const Vector3 toTarget = avatar.walkTarget - avatar.position;
		const float distance = toTarget.GetLength();
		const float step = runSpeed * deltaSeconds;

		if (distance > arrivalEpsilon && step < distance)
		{
			avatar.position += toTarget * (step / distance);
			ApplyTransform(avatar);
			return false;
		}

		avatar.walking = false;
		avatar.position = avatar.walkTarget;
		avatar.facingDegrees = avatar.arrivalFacingDegrees;
		ApplyTransform(avatar);
		PlayAnimation(avatar, avatar.arrivalPose);

		if (avatar.hideOnArrival)
		{
			avatar.hideOnArrival = false;
			SetVisible(avatar, false);
		}

		return true;
	}

	bool PlazaAvatars::IsLeaving(const uint64 guid) const
	{
		return std::find(m_leavingGuids.begin(), m_leavingGuids.end(), guid) != m_leavingGuids.end();
	}

	bool PlazaAvatars::IsStandingOnPlaza(const Avatar& avatar) const
	{
		return avatar.visible && avatar.entity != nullptr && !avatar.walking && !IsLeaving(avatar.guid);
	}

	void PlazaAvatars::ApplyTransform(Avatar& avatar)
	{
		float yaw = avatar.facingDegrees;
		if (!avatar.walking)
		{
			if (avatar.selected)
			{
				yaw += m_selectedRotationDegrees;
			}

			yaw += avatar.hoverTurnDegrees * avatar.hover.GetTurn();
		}

		avatar.node->SetPosition(avatar.position);
		avatar.node->SetOrientation(Quaternion(Degree(yaw), Vector3::UnitY));
	}

	void PlazaAvatars::UpdateHover(Avatar& avatar, const float deltaSeconds, const Vector3& cameraPosition)
	{
		const bool active = m_hoveredGuid != 0 && avatar.guid == m_hoveredGuid && IsStandingOnPlaza(avatar);
		if (!active && avatar.hover.IsIdle())
		{
			return;
		}

		const bool entered = avatar.hover.Update(active, deltaSeconds);

		const bool seated = IsPlazaSeatedPose(avatar.animationName);
		if (entered)
		{
			// The turn is fixed for this hover: a rotation while hovered still turns the character.
			float yaw = avatar.facingDegrees;
			if (avatar.selected)
			{
				yaw += m_selectedRotationDegrees;
			}

			avatar.hoverTurnDegrees = seated ? 0.0f : PlazaTurnTowardsDegrees(yaw, avatar.position, cameraPosition, hoverMaxTurnDegrees);
			if (!seated)
			{
				StartGreeting(avatar);
			}
		}

		UpdateGreeting(avatar, deltaSeconds);
		ApplyTransform(avatar);

		const float glow = avatar.hover.GetGlow();
		if (glow > 0.0f)
		{
			ApplyGlow(avatar, glow);
		}
		else if (avatar.hover.IsIdle())
		{
			RestoreMaterials(avatar);
			avatar.hoverTurnDegrees = 0.0f;
		}
	}

	void PlazaAvatars::StartGreeting(Avatar& avatar)
	{
		if (!avatar.entity || avatar.greetingState)
		{
			return;
		}

		for (const String& clip : m_greetingClips)
		{
			if (clip == avatar.animationName || !avatar.entity->HasAnimationState(clip))
			{
				continue;
			}

			AnimationState* state = avatar.entity->GetAnimationState(clip);
			if (!state || state->GetLength() <= 0.0f)
			{
				continue;
			}

			avatar.greetingState = state;
			avatar.greetingElapsed = 0.0f;
			state->SetTimePosition(0.0f);
			state->SetLoop(false);
			state->SetWeight(0.0f);
			state->SetEnabled(true);
			return;
		}
	}

	void PlazaAvatars::UpdateGreeting(Avatar& avatar, const float deltaSeconds)
	{
		if (!avatar.greetingState)
		{
			return;
		}

		avatar.greetingElapsed += deltaSeconds;

		const float length = avatar.greetingState->GetLength();
		if (avatar.greetingElapsed >= length)
		{
			StopGreeting(avatar);
			return;
		}

		avatar.greetingState->AddTime(deltaSeconds);

		// The skeleton averages the enabled clips by weight, so pose and greeting share one weight.
		const float weight = PlazaGreetingWeight(avatar.greetingElapsed, length, greetingFadeInSeconds, greetingFadeOutSeconds);
		avatar.greetingState->SetWeight(std::max(weight, 0.001f));
		if (avatar.animationState)
		{
			avatar.animationState->SetWeight(std::max(1.0f - weight, 0.001f));
		}
	}

	void PlazaAvatars::StopGreeting(Avatar& avatar)
	{
		if (!avatar.greetingState)
		{
			return;
		}

		avatar.greetingState->SetEnabled(false);
		avatar.greetingState->SetWeight(1.0f);
		avatar.greetingState = nullptr;
		avatar.greetingElapsed = 0.0f;

		if (avatar.animationState)
		{
			avatar.animationState->SetWeight(1.0f);
		}
	}

	void PlazaAvatars::ApplyGlow(Avatar& avatar, const float strength)
	{
		if (!avatar.entity)
		{
			return;
		}

		if (avatar.glowMaterials.empty())
		{
			// Per-avatar instances, as GameUnitC::EnsureMaterialInstances does for spell tints, so
			// the shared character materials never glow on anyone else.
			const auto addEntity = [&avatar](Entity& entity)
			{
				for (uint32 i = 0; i < entity.GetNumSubEntities(); ++i)
				{
					SubEntity* subEntity = entity.GetSubEntity(static_cast<uint16>(i));
					if (!subEntity || !subEntity->IsVisible())
					{
						continue;
					}

					MaterialPtr original = subEntity->GetMaterial();
					if (!original)
					{
						continue;
					}

					auto instance = std::make_shared<MaterialInstance>(String(entity.GetName()) + "_PlazaGlow_" + std::to_string(i), original);
					subEntity->SetMaterial(std::static_pointer_cast<MaterialInterface>(instance));
					avatar.glowMaterials.push_back(Avatar::GlowMaterial{ subEntity, std::move(original), std::move(instance) });
				}
			};

			addEntity(*avatar.entity);
			for (auto& [displayId, attachment] : avatar.attachments)
			{
				if (attachment.entity)
				{
					addEntity(*attachment.entity);
				}
			}
		}

		const Vector4 tint(glowTint.x * strength, glowTint.y * strength, glowTint.z * strength, 1.0f);
		for (const Avatar::GlowMaterial& material : avatar.glowMaterials)
		{
			material.instance->SetVectorParameter(glowParameter, tint);
		}
	}

	void PlazaAvatars::RestoreMaterials(Avatar& avatar)
	{
		for (const Avatar::GlowMaterial& material : avatar.glowMaterials)
		{
			if (material.subEntity && material.original)
			{
				material.subEntity->SetMaterial(material.original);
			}
		}

		avatar.glowMaterials.clear();
	}

	void PlazaAvatars::ResetHover(Avatar& avatar)
	{
		const bool wasReacting = !avatar.hover.IsIdle();

		StopGreeting(avatar);
		RestoreMaterials(avatar);
		avatar.hover.Reset();
		avatar.hoverTurnDegrees = 0.0f;

		if (wasReacting && avatar.node)
		{
			ApplyTransform(avatar);
		}
	}
}
