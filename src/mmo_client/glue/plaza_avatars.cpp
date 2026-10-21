// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "plaza_avatars.h"

#include "game_client/plaza_roster.h"
#include "math/degree.h"
#include "math/math_utils.h"
#include "math/quaternion.h"
#include "scene_graph/animation_state.h"
#include "scene_graph/entity.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "scene_graph/skeleton_instance.h"

#include <algorithm>
#include <string>

namespace mmo
{
	namespace
	{
		/// Walking speed in metres per second.
		constexpr float walkSpeed = 2.5f;

		/// Distance below which a character counts as standing at its target.
		constexpr float arrivalEpsilon = 0.01f;

		/// Height of the nameplate anchor above the head bone, in metres.
		constexpr float nameplateHeadOffset = 0.4f;

		const String idleAnimation = "Idle";
		const String walkAnimation = "Walk";

		/// Facing of a straight walk from one position to another, in degrees.
		float TravelFacingDegrees(const Vector3& from, const Vector3& to)
		{
			return Radian(DirectionToFacing(to.x - from.x, to.z - from.z)).GetValueDegrees();
		}
	}

	PlazaAvatars::PlazaAvatars(Scene& scene, const proto_client::Project& project, const GlueSceneDefinition& definition)
		: m_scene(scene)
		, m_definition(definition)
		, m_builder(project)
	{
	}

	PlazaAvatars::~PlazaAvatars()
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

			if (it != previous.end())
			{
				m_avatars.push_back(std::move(*it));
				previous.erase(it);
			}
			else
			{
				m_avatars.push_back(CreateAvatar(view));
			}
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
				// Walking out after a delete: let them finish the walk.
				m_departing.push_back(std::move(avatar));
				continue;
			}

			DestroyAvatar(*avatar);
		}
	}

	void PlazaAvatars::Select(int32 orderIndex, const bool animate)
	{
		const int32 numCharacters = static_cast<int32>(m_avatars.size());
		if (orderIndex < 0 || orderIndex >= numCharacters)
		{
			orderIndex = -1;
		}

		const PlazaAssignment assignment = AssignPlaza(numCharacters, orderIndex);

		const uint64 frontGuid = assignment.frontCharacter >= 0 ? m_avatars[assignment.frontCharacter]->guid : 0;
		if (frontGuid != m_frontGuid)
		{
			m_frontRotationDegrees = 0.0f;
			m_frontGuid = frontGuid;
		}

		for (int32 index = 0; index < numCharacters; ++index)
		{
			Avatar& avatar = *m_avatars[index];

			if (IsLeaving(avatar.guid))
			{
				// Walking out before a delete: it must not come back while the list is refreshed.
				avatar.atFront = false;
				continue;
			}

			const GlueSpot* target = nullptr;
			const bool isFront = index == assignment.frontCharacter;
			if (isFront)
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

			avatar.atFront = isFront;

			if (target == nullptr)
			{
				// Not on the plaza for this selection.
				if (!animate)
				{
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

			// The front spot plays a calm standing idle; plaza spots their pose.
			const String& pose = isFront ? idleAnimation : target->pose;

			if (!animate)
			{
				avatar.walking = false;
				avatar.hideOnArrival = false;
				PlaceAt(avatar, target->position, target->facingDegrees);
				PlayAnimation(avatar, pose);
				SetVisible(avatar, true);
				continue;
			}

			if (avatar.visible)
			{
				WalkTo(avatar, target->position, target->facingDegrees, pose, false);
				continue;
			}

			if (isFront && assignment.frontIsVisitor)
			{
				// A visitor walks in from the street.
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
		avatar.atFront = false;

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
		m_frontRotationDegrees += degrees;

		for (const auto& avatar : m_avatars)
		{
			if (avatar->atFront)
			{
				ApplyTransform(*avatar);
			}
		}
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

	void PlazaAvatars::Update(const float deltaSeconds)
	{
		for (const auto& avatar : m_avatars)
		{
			if (avatar->walking)
			{
				UpdateWalk(*avatar, deltaSeconds);
			}

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

		const String id = std::to_string(m_nextEntityId++);

		avatar->node = &m_scene.CreateSceneNode("PlazaAvatarNode_" + id);
		m_scene.GetRootSceneNode().AddChild(*avatar->node);

		// The mesh's base alignment, as for units in the world (GameObjectC::SetupSceneObjects).
		Quaternion rotationOffset;
		rotationOffset.FromAngleAxis(Degree(90.0f), Vector3::UnitY);
		avatar->offsetNode = avatar->node->CreateChildSceneNode(Vector3::Zero, rotationOffset);

		avatar->entity = m_builder.Build(m_scene, *avatar->offsetNode, view, "PlazaAvatar_" + id, avatar->attachments);

		SetVisible(*avatar, false);
		return avatar;
	}

	void PlazaAvatars::DestroyAvatar(Avatar& avatar)
	{
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

	void PlazaAvatars::SetVisible(Avatar& avatar, const bool visible)
	{
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
			PlayAnimation(avatar, walkAnimation);
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

		const String& resolved = avatar.entity->HasAnimationState(name) ? name : idleAnimation;
		if (avatar.animationState && avatar.animationName == resolved)
		{
			return;
		}

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
		const float step = walkSpeed * deltaSeconds;

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

	void PlazaAvatars::ApplyTransform(Avatar& avatar)
	{
		float yaw = avatar.facingDegrees;
		if (avatar.atFront && !avatar.walking)
		{
			yaw += m_frontRotationDegrees;
		}

		avatar.node->SetPosition(avatar.position);
		avatar.node->SetOrientation(Quaternion(Degree(yaw), Vector3::UnitY));
	}
}
