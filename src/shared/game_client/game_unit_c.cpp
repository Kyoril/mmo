#include "game_unit_c.h"
#include "game_aura_c.h"
#include "game_world_object_c_base.h"
#include "sound_entry_player.h"
#include "spell_visualization_service.h"
#include "movement_log.h"
#include "path_movement_utils.h"
#include "animation/animation_controller.h"

#include <sstream>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>
#include <set>
#include <unordered_map>

#include "debug_interface.h"
#include "net_client.h"
#include "object_mgr.h"
#include "unit_movement.h"
#include "base/clock.h"
#include "base/profiler.h"
#include "client_data/project.h"
#include "frame_ui/font_mgr.h"
#include "game/aura.h"
#include "game/emote_defs.h"
#include "game/spell.h"
#include "game/character_customization/avatar_definition_mgr.h"
#include "log/default_log_levels.h"
#include "math/collision.h"
#include "math/math_utils.h"
#include "scene_graph/animation_notify.h"
#include "scene_graph/animation_state.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/scene.h"
#include "scene_graph/sub_entity.h"
#include "scene_graph/entity.h"
#include "scene_graph/skeleton.h"
#include "scene_graph/skeleton_instance.h"
#include "scene_graph/bone.h"
#include "scene_graph/animation.h"
#include "scene_graph/particle_emitter.h"
#include "scene_graph/particle_emitter_serializer.h"
#include "assets/asset_registry.h"
#include "binary_io/stream_source.h"
#include "binary_io/reader.h"
#include "graphics/material.h"
#include "graphics/material_instance.h"
#include "shared/client_data/proto_client/factions.pb.h"
#include "shared/client_data/proto_client/faction_templates.pb.h"
#include "shared/client_data/proto_client/model_data.pb.h"
#include "shared/client_data/proto_client/spells.pb.h"

namespace mmo
{
	SoundEntryPlayer* GameUnitC::s_soundEntryPlayer = nullptr;

	void GameUnitC::SetSoundEntryPlayer(SoundEntryPlayer* player)
	{
		s_soundEntryPlayer = player;
	}

	GameUnitC::GameUnitC(Scene &scene, NetClient &netDriver, const proto_client::Project &project, uint32 map) : GameObjectC(scene, project, map), m_netDriver(netDriver), m_unitSpeed{0.0f}, m_creatureInfo()
	{
		m_unitMovement = std::make_unique<UnitMovement>(*this);
		m_animationController = std::make_unique<AnimationController>(m_entity, project);
	}

	GameUnitC::~GameUnitC()
	{
		UpdateSparkEmitter(false);

		// Ensure quest giver status is removed
		SetQuestGiverStatus(questgiver_status::None);

		if (m_capsuleDebugObject)
		{
			DestroyCapsuleDebugVisualization();
		}

		if (m_selectionRing)
		{
			m_scene.DestroyManualRenderObject(*m_selectionRing);
			m_selectionRing = nullptr;
		}
	}

	void GameUnitC::QueueMovementEvent(MovementEventType eventType, GameTime timestamp,
									   const MovementInfo &movementInfo)
	{
		m_movementEventQueue.emplace(eventType, timestamp, movementInfo);
	}

	void GameUnitC::UpdateSparkEmitter(const bool active)
	{
		const std::string emitterName = "Spark_Unit_" + std::to_string(GetGuid());

		if (active)
		{
			if (m_sparkEmitter)
			{
				return;
			}

			m_sparkEmitter = m_scene.CreateParticleEmitter(emitterName);
			if (!m_sparkEmitter)
			{
				return;
			}

			const auto file = AssetRegistry::OpenFile("Particles/Sparkles.hpar");
			if (file)
			{
				io::StreamSource source(*file);
				io::Reader reader(source);
				ParticleEmitterSerializer serializer;
				ParticleEmitterParameters params;
				if (serializer.Deserialize(params, reader))
				{
					m_sparkEmitter->SetParameters(params);
				}
			}

			m_sparkEmitterNode = m_entityOffsetNode->CreateChildSceneNode(emitterName + "_Node");
			m_sparkEmitterNode->AttachObject(*m_sparkEmitter);
			m_sparkEmitter->Play();
		}
		else
		{
			if (!m_sparkEmitter)
			{
				return;
			}

			m_sparkEmitter->Stop();
			m_scene.DestroyParticleEmitter(*m_sparkEmitter);
			m_sparkEmitter = nullptr;

			if (m_sparkEmitterNode)
			{
				m_scene.DestroySceneNode(*m_sparkEmitterNode);
				m_sparkEmitterNode = nullptr;
			}
		}
	}

	void GameUnitC::AddYawInput(const Radian &value)
	{
		m_yawInput += value;
	}

	void GameUnitC::OnMovementModeChanged(MovementMode previousMovementMode, MovementMode newMovementMode)
	{
		if (!m_pressedJump || !m_unitMovement->IsFalling())
		{
			ResetJumpState();
		}

		// Entering the water (Falling -> Swimming) is handled by OnStartSwimming, which emits its
		// own StartSwim event. Don't emit a spurious Land event in that case.
		if (previousMovementMode == MovementMode::Falling &&
			newMovementMode != MovementMode::Falling &&
			newMovementMode != MovementMode::Swimming)
		{
			UpdateMovementInfo();
			m_movementInfo.movementFlags &= ~movement_flags::Falling;

			m_netDriver.OnMoveEvent(*this, MovementEvent(movement_event_type::Land, m_movementInfo.timestamp, m_movementInfo));

			// Lock the position so the next start packet uses the same position.
			// This prevents drift from physics adjustments between landing and next movement.
			// Only lock when no position-changing flag remains: when landing while running,
			// the character keeps moving, and a locked (stale) position reported by the next
			// start packet would make the server see a backward teleport followed by an
			// impossibly fast catch-up — tripping the anti-cheat speed validation.
			if (!m_movementInfo.IsChangingPosition())
			{
				LockPositionForSync();
			}
		}
	}

	Vector3 GameUnitC::GetForwardVector() const
	{
		return m_sceneNode->GetOrientation() * Vector3::UnitX;
	}

	Vector3 GameUnitC::GetRightVector() const
	{
		return m_sceneNode->GetOrientation() * Vector3::UnitZ;
	}

	Vector3 GameUnitC::GetUpVector() const
	{
		return m_sceneNode->GetOrientation() * Vector3::UnitY;
	}

	void GameUnitC::Deserialize(io::Reader &reader, const bool complete)
	{
		uint32 updateFlags = 0;
		if (!(reader >> io::read<uint32>(updateFlags)))
		{
			return;
		}

		ASSERT(!complete || (updateFlags & object_update_flags::HasMovementInfo) != 0);
		if (updateFlags & object_update_flags::HasMovementInfo)
		{
			if (!(reader >> m_movementInfo))
			{
				return;
			}
		}

		if (complete)
		{
			if (!(m_fieldMap.DeserializeComplete(reader)))
			{
				ASSERT(false);
			}

			OnEntryChanged();
		}
		else
		{
			if (!(m_fieldMap.DeserializeChanges(reader)))
			{
				ASSERT(false);
			}

			if (!complete && m_fieldMap.IsFieldMarkedAsChanged(object_fields::DisplayId))
			{
				OnDisplayIdChanged();
			}

			if (!complete && m_fieldMap.IsFieldMarkedAsChanged(object_fields::Scale))
			{
				OnScaleChanged();
			}

			HandleFieldMapChanges();
		}

		if (complete || m_fieldMap.IsFieldMarkedAsChanged(object_fields::FactionTemplate))
		{
			OnFactionTemplateChanged();
		}

		if (complete || m_fieldMap.IsFieldMarkedAsChanged(object_fields::Entry))
		{
			OnEntryChanged();
		}

		if (!complete && m_fieldMap.IsFieldMarkedAsChanged(object_fields::Flags))
		{
			UpdateSparkEmitter(CanBeLooted());
		}

		if (!complete && (m_fieldMap.IsFieldMarkedAsChanged(object_fields::StandState) ||
			m_fieldMap.IsFieldMarkedAsChanged(object_fields::SitPoseEmote) ||
			m_fieldMap.IsFieldMarkedAsChanged(object_fields::SleepPoseEmote)))
		{
			RefreshPoseAnimation();
		}

		if (!complete && m_fieldMap.IsFieldMarkedAsChanged(object_fields::MoodEmote))
		{
			RefreshMoodAnimation();
		}

		if (!complete && m_fieldMap.IsFieldMarkedAsChanged(object_fields::IdlePoseEmote))
		{
			// Rebind the special idle clip and restart the idle timer so the new pose eases
			// in after the regular delay.
			UpdateSpecialIdleClip();
			m_animationController->ResetIdleTimer();
		}

		m_fieldMap.MarkAllAsUnchanged();

		reader >> io::read<float>(m_unitSpeed[movement_type::Walk]) >> io::read<float>(m_unitSpeed[movement_type::Run]) >> io::read<float>(m_unitSpeed[movement_type::Backwards]) >> io::read<float>(m_unitSpeed[movement_type::Swim]) >> io::read<float>(m_unitSpeed[movement_type::SwimBackwards]) >> io::read<float>(m_unitSpeed[movement_type::Flight]) >> io::read<float>(m_unitSpeed[movement_type::FlightBackwards]) >> io::read<float>(m_unitSpeed[movement_type::Turn]);

		ASSERT(GetGuid() > 0);
		if (complete)
		{
			SetupSceneObjects();
			UpdateSparkEmitter(CanBeLooted());
		}

		if (updateFlags & object_update_flags::HasMovementInfo)
		{
			m_sceneNode->SetDerivedPosition(m_movementInfo.position);
			m_sceneNode->SetDerivedOrientation(Quaternion(m_movementInfo.facing, Vector3::UnitY));

			// Seed the remote movement queue with the spawn snapshot so the queue
			// always has at least one anchor before the first movement packet arrives.
			// Without this, Sample() would interpolate from timestamp 0 to the first
			// packet timestamp, producing a large elapsed-time value that triggers
			// immediate extrapolation and a visible position spike on first movement.
			if (IsPlayer() && !IsControlledByLocalPlayer())
			{
				EnqueueRemoteMovement(m_movementInfo);
			}
		}
	}

	void GameUnitC::Update(const float deltaTime)
	{
		GameObjectC::Update(deltaTime);

		// Get current game time
		const GameTime now = GetAsyncTimeMs();
		while (!m_movementEventQueue.empty())
		{
			// Get next event (we expect events are in order, so no future event is in front of a past event)
			const auto &moveEvent = m_movementEventQueue.front();
			if (moveEvent.timestamp > now)
			{
				// Event is in the future, stop processing
				break;
			}

			// While the server controls our movement (charge), queued movement events are
			// discarded instead of sent: the server rejects them anyway and the charge ack
			// already carried our latest movement state.
			if (ObjectMgr::GetActivePlayerGuid() == GetGuid() && !m_serverControlledMovement)
			{
				m_netDriver.OnMoveEvent(*this, moveEvent);
			}

			// Remove processed event
			m_movementEventQueue.pop();
		}

		const bool isRemotePlayer = IsPlayer() && !IsControlledByLocalPlayer();

		if (IsControlledByLocalPlayer())
		{
			UpdateMovementInfo();

			// Failsafe: if the server announced taking movement control (charge) but the
			// movement path never arrived (e.g. no path could be found on the server),
			// release control after a short timeout so the player doesn't get stuck.
			if (m_serverControlledMovement && !IsFollowingPath() &&
				now > m_serverControlStartTime + 2000)
			{
				SetServerControlledMovement(false);
			}

			// Safety net: a locked sync position is only meaningful while the character is
			// actually standing still — it exists to absorb sub-tolerance physics settling
			// between a stop and the next start. If the character has moved away from the
			// locked position (e.g. a lock taken while another movement flag was still
			// active), sending the stale position would desync the server's movement
			// validation. Drop the lock as soon as it no longer matches reality.
			if (m_positionLocked && !m_sceneNode->GetPosition().IsNearlyEqual(m_syncedPosition, 0.2f))
			{
				m_positionLocked = false;
			}

			// While swimming, send heartbeats even when no position-changing flag is set: the
			// surface cap can move the player vertically without a Forward/Strafe/etc. flag, so the
			// periodic heartbeat keeps the server in sync with the client's depth.
			if (!m_serverControlledMovement &&
				(m_movementInfo.IsChangingPosition() || m_movementInfo.IsSwimming()) && now > m_lastHeartbeat + 500)
			{
				// Heartbeat sends the current authoritative position to the server.
				// Since the server baseline is now updated, any stale position lock
				// from a previous stop packet is no longer valid — clear it.
				m_positionLocked = false;
				m_lastHeartbeat = now;
				m_netDriver.OnMoveEvent(*this, MovementEvent(movement_event_type::Heartbeat, now, m_movementInfo));

				const Vector3& pos = m_movementInfo.position;
				MOVEMENT_EVENT("HEARTBEAT",
					"pos=(" << pos.x << "," << pos.y << "," << pos.z << ")"
					<< " flags=0x" << std::hex << m_movementInfo.movementFlags << std::dec
					<< " facing=" << m_movementInfo.facing.GetValueRadians());
			}
		}

		// For remote players, use the buffered movement queue for smooth playback.
		// For the local player and NPCs, use the existing flag-based input system.
		if (isRemotePlayer)
		{
			UpdateRemoteMovement(deltaTime);
		}
		else if (!IsFollowingPath())
		{
			// While following a server movement path, movement flags must not feed the physics
			// simulation: path movement positions the node directly, and physics input on top
			// of it makes the character overshoot waypoints and oscillate (visible as a fast,
			// endless yaw spin at the destination).

			// Based on current movement info, update movement states
			const float lateralSpeed = IsWalkModeEnabled() ? GetSpeed(movement_type::Walk) : GetSpeed(movement_type::Run);
			if (m_movementInfo.movementFlags & movement_flags::Forward)
			{
				AddInputVector(GetForwardVector() * lateralSpeed);
			}
			if (m_movementInfo.movementFlags & movement_flags::Backward)
			{
				AddInputVector(-GetForwardVector() * GetSpeed(movement_type::Backwards));
			}
			if (m_movementInfo.movementFlags & movement_flags::StrafeLeft)
			{
				AddInputVector(-GetRightVector() * lateralSpeed);
			}
			if (m_movementInfo.movementFlags & movement_flags::StrafeRight)
			{
				AddInputVector(GetRightVector() * lateralSpeed);
			}
			if (m_movementInfo.movementFlags & movement_flags::TurnLeft)
			{
				AddYawInput(Radian(GetSpeed(movement_type::Turn) * 1.0f));
			}
			if (m_movementInfo.movementFlags & movement_flags::TurnRight)
			{
				AddYawInput(Radian(GetSpeed(movement_type::Turn) * -1.0f));
			}
		}

		// Reflect the swim/dive pitch on the character mesh so the player can see their dive angle.
		UpdateSwimMeshPitch(deltaTime);

		UpdateQuestGiverAndNameVisuals(deltaTime);

		const bool isDead = (GetHealth() <= 0) || GetStandState() == unit_stand_state::Dead;
		if (!isDead)
		{
			UpdateNormalMovement(deltaTime);
		}

		UpdateAnimation(deltaTime, isDead);

		// For remote players, the queue handles position updates directly -
		// UnitMovement::Tick is not needed for them. But we must consume the
		// input vector that was fed for animation detection purposes.
		if (!isRemotePlayer)
		{
			m_unitMovement->Tick(deltaTime);
		}
		else
		{
			ConsumeInputVector();
		}
	}

	void GameUnitC::UpdateQuestGiverAndNameVisuals(const float deltaTime)
	{
		// Update quest giver icon
		if (m_questGiverNode != nullptr)
		{
			if (!m_questOffset.IsNearlyEqual(m_questGiverNode->GetPosition()))
			{
				// Lerp questgivernode position to quest offset node
				m_questGiverNode->SetPosition(m_questGiverNode->GetPosition().Lerp(m_questOffset, deltaTime * 5.0f));
			}

			// TODO: Get rotation to the current camera and yaw the icon to face it!
			const Camera *cam = m_scene.GetCamera((uint32)0);
			if (cam)
			{
				m_questGiverNode->SetFixedYawAxis(true);
				m_questGiverNode->LookAt(cam->GetDerivedPosition(), TransformSpace::World, Vector3::NegativeUnitZ);
			}
		}

		// Update name component to face camera
		if (m_nameComponent && m_nameComponent->IsVisible())
		{
			const Camera *cam = m_scene.GetCamera((uint32)0);
			if (cam)
			{
				m_nameComponentNode->SetFixedYawAxis(true);
				m_nameComponentNode->LookAt(cam->GetDerivedPosition(), TransformSpace::World, Vector3::UnitZ);
			}
		}
	}

	void GameUnitC::UpdateNormalMovement(const float deltaTime)
	{
		// Update target tracking for NPCs
		UpdateTargetTracking();

		// Update path-based movement if we have an active path
		if (!m_movementPath.empty() && !m_pathCompleted)
		{
			UpdatePathMovement(deltaTime);
		}
		else if (!IsControlledByLocalPlayer() && !IsPlayer() && m_unitMovement)
		{
			// For idle non-player units (NPCs without a path), correct ground height.
			// Remote players apply ground correction inside UpdateRemoteMovement()
			// after their dead-reckoning position is written to the scene node.
			m_unitMovement->CorrectGroundHeight();
		}

	}

	void GameUnitC::UpdateTargetTracking() const
	{
		// Check if we should track a target
		// Skip yaw-tracking when the unit is rooted/stunned/sleeping — it can't
		// act or move so it should not visually snap to face the target.
		if (const auto target = m_targetUnit.lock(); !IsPlayer() && target && !IsRooted())
		{
			// Only rotate around the Y axis (yaw) to face the target
			// Calculate the direction vector to the target in the horizontal plane
			const Vector3 targetPos = target->GetSceneNode()->GetDerivedPosition();
			const Vector3 myPos = GetSceneNode()->GetDerivedPosition();

			// Calculate the angle to face the target
			const Radian yawToTarget = GetAngle(myPos.x, myPos.z, targetPos.x, targetPos.z);

			// Set orientation using only the yaw component
			GetSceneNode()->SetOrientation(Quaternion(yawToTarget, Vector3::UnitY));
		}
	}

	void GameUnitC::UpdateAnimation(const float deltaTime, const bool isDead)
	{
		if (!m_animationController)
		{
			return;
		}

		// Local prediction: starting to move while in a locked pose (sitting/sleeping)
		// immediately releases the pose animation - the server confirms by resetting the
		// replicated stand state when it receives the movement packet.
		if (m_animationController->IsPoseLockActive() && IsControlledByLocalPlayer())
		{
			const Vector3 horizontalInput = m_inputVector * Vector3(1.0f, 0.0f, 1.0f);
			if (horizontalInput.GetLength() > 0.1f)
			{
				m_animationController->ClearPoseLoop();

				// Movement cancelled the pose - forget it so the server's stand-state confirm
				// (which may arrive after the player already stopped) never plays the exit clip.
				m_activePoseEmoteId = 0;
				m_poseStandState = unit_stand_state::Stand;
			}
		}

		// Death forgets the active pose so a later stand-state confirm never plays an exit clip.
		if (isDead && m_poseStandState != unit_stand_state::Stand)
		{
			m_activePoseEmoteId = 0;
			m_poseStandState = unit_stand_state::Stand;
		}

		AnimationContext ctx;
		ctx.deltaTime = deltaTime;
		ctx.movementFlags = m_movementInfo.movementFlags;
		const float inputVector2DSize = (m_inputVector * Vector3(1.0f, 0.0f, 1.0f)).GetLength();
		ctx.moving = m_unitMovement && m_unitMovement->IsMovingOnGround() && inputVector2DSize > 0.1f;
		ctx.pathMoving = IsFollowingPath();
		ctx.airborne = m_unitMovement && m_unitMovement->IsFalling() &&
			std::abs(m_unitMovement->GetVelocity().y) > 0.05f;
		ctx.verticalVelocity = m_unitMovement ? m_unitMovement->GetVelocity().y : 0.0f;
		ctx.swimming = m_movementInfo.IsSwimming();
		ctx.walkMode = IsWalkModeEnabled();
		ctx.weaponDrawn = IsWeaponDrawn();
		ctx.weaponClass = m_weaponClass;
		ctx.stealthed = HasStealthAura();
		ctx.dead = isDead;

		m_animationController->Update(ctx);
	}

	void GameUnitC::AdvanceAnimationTimes(const float deltaTime)
	{
		// Scope inside the worker body so the perf HUD thread column proves the
		// parallel path actually engages ("Multiple" instead of "Main").
		PROFILE_SCOPE("GameUnitC::AdvanceAnimationTimes");

		if (m_animationController)
		{
			m_animationController->AdvanceClipTimes(deltaTime);
		}
	}

	void GameUnitC::FlushDeferredAnimationNotifies() const
	{
		if (!m_entity)
		{
			return;
		}

		// Pin the set so a notify handler swapping this unit's mesh mid-flush cannot
		// destroy it under us. Notifies collected for a set that was already replaced
		// by an earlier handler in this flush loop are dropped — before the parallel
		// advance they fired inline during Update and were always delivered, but a
		// mesh swap invalidates them anyway.
		if (const std::shared_ptr<AnimationStateSet> animationStates = m_entity->GetAllAnimationStatesShared())
		{
			animationStates->FlushDeferredNotifies();
		}
	}

	void GameUnitC::ApplyMovementInfo(const MovementInfo &movementInfo)
	{
		m_movementInfo = movementInfo;

		// Set position and orientation directly from the packet.
		// For the local player this keeps physics in sync.
		// For remote units this provides the sync point from which
		// extrapolation continues until the next packet arrives.
		GetSceneNode()->SetDerivedPosition(movementInfo.position);
		GetSceneNode()->SetDerivedOrientation(Quaternion(Radian(movementInfo.facing), Vector3::UnitY));

		if (IsControlledByLocalPlayer())
		{
			MOVEMENT_EVENT("APPLY_MOVE_INFO",
				"pos=(" << movementInfo.position.x << "," << movementInfo.position.y << "," << movementInfo.position.z << ")"
				<< " flags=0x" << std::hex << movementInfo.movementFlags << std::dec
				<< " facing=" << movementInfo.facing.GetValueRadians()
				<< " ts=" << movementInfo.timestamp);
		}

		// Handle falling state transitions
		if (m_movementInfo.IsFalling())
		{
			m_unitMovement->SetVelocity(m_movementInfo.jumpVelocity);
			if (!m_unitMovement->IsFalling())
			{
				m_unitMovement->SetMovementMode(MovementMode::Falling);
			}
		}
		else if (m_unitMovement->IsFalling())
		{
			// Landed: transition back to walking
			m_unitMovement->SetMovementMode(MovementMode::Walking);
		}

		UpdateCollider();
	}

	void GameUnitC::EnqueueRemoteMovement(const MovementInfo &movementInfo)
	{
		m_remoteMovementRenderer.OnAuthoritativeUpdate(movementInfo, false);
	}

	void GameUnitC::UpdateRemoteMovement(const float deltaTime)
	{
		RemoteMovementState state;
		if (!m_remoteMovementRenderer.Sample(
			deltaTime,
			GetSpeed(movement_type::Run),
			GetSpeed(movement_type::Backwards),
			GetSpeed(movement_type::Walk),
			GetSpeed(movement_type::Turn),
			state))
		{
			// Not yet initialized, nothing to do
			return;
		}

		// Apply facing
		GetSceneNode()->SetDerivedOrientation(Quaternion(state.facing, Vector3::UnitY));

		// Apply lateral movement through SafeMoveNode so the capsule respects
		// world geometry (walls, obstacles). This prevents remote players from
		// visually penetrating walls they are colliding against locally.
		// Only do the sweep when there is actual movement — zero-delta sweeps
		// are a no-op but waste cycles.
		if (!state.desiredDelta.IsZero())
		{
			// state.position is the pre-collision start point computed by the
			// renderer (m_scenePos − desiredDelta).  Setting the scene node here
			// ensures the capsule sweep begins from the smooth-correction position
			// rather than wherever the scene node happened to be last frame.
			GetSceneNode()->SetDerivedPosition(state.position);

			// Apply lateral movement through a capsule sweep so the remote
			// player character respects world geometry (walls, obstacles).
			m_unitMovement->RemotePlayerMoveCollide(state.desiredDelta);

			// Feed the collision-resolved position back into the renderer's
			// scene-tracking state (m_scenePos) so the smooth correction loop
			// stays in sync with the physical capsule position.
			const Vector3 resolvedPos = GetSceneNode()->GetDerivedPosition();
			m_remoteMovementRenderer.SetRenderedPos(resolvedPos);
		}
		else
		{
			// No movement — place the scene node at the renderer's correction pos.
			GetSceneNode()->SetDerivedPosition(state.position);
		}

		// Update movement info flags so animation system works correctly
		m_movementInfo.movementFlags = state.movementFlags;
		m_movementInfo.position = GetSceneNode()->GetDerivedPosition();  // use resolved position
		m_movementInfo.facing = state.facing;

		// Handle falling/jumping state for animations
		if (state.isFalling)
		{
			m_unitMovement->SetVelocity(state.velocity);
			if (!m_unitMovement->IsFalling())
			{
				m_unitMovement->SetMovementMode(MovementMode::Falling);
			}
		}
		else
		{
			// Ensure walking mode is set so animation system knows we're on ground
			if (!m_unitMovement->IsMovingOnGround())
			{
				m_unitMovement->SetMovementMode(MovementMode::Walking);
			}

			// Snap the scene node to terrain height so remote players don't fall
			// through the ground when the server navmesh height differs slightly
			// from the client's collision geometry.
			// Feed the corrected Y back into the renderer so dead reckoning
			// continues from the snapped height — otherwise it overwrites the
			// correction next frame with the original below-ground Y.
			// The authoritative Y anchors the ground search: since SetRenderedY
			// overwrites the dead-reckoning target, a snap onto the wrong surface
			// (terrain running underneath a raised building floor) would otherwise
			// re-capture the unit below the floor every frame with no way back up.
			if (m_unitMovement->CorrectGroundHeight(5.0f, m_remoteMovementRenderer.GetAuthoritativeY()))
			{
				m_remoteMovementRenderer.SetRenderedY(GetSceneNode()->GetDerivedPosition().y);
			}
		}

		// Feed the input vector from interpolated movement flags so the animation
		// system detects that the remote player is moving (run vs idle).
		const Quaternion orientation(state.facing, Vector3::UnitY);
		const Vector3 forward = orientation * Vector3::UnitX;
		const Vector3 right = orientation * Vector3::UnitZ;

		const float remoteLateralSpeed = IsWalkModeEnabled() ? GetSpeed(movement_type::Walk) : GetSpeed(movement_type::Run);
		if (state.movementFlags & movement_flags::Forward)
		{
			AddInputVector(forward * remoteLateralSpeed);
		}
		if (state.movementFlags & movement_flags::Backward)
		{
			AddInputVector(-forward * GetSpeed(movement_type::Backwards));
		}
		if (state.movementFlags & movement_flags::StrafeLeft)
		{
			AddInputVector(-right * remoteLateralSpeed);
		}
		if (state.movementFlags & movement_flags::StrafeRight)
		{
			AddInputVector(right * remoteLateralSpeed);
		}

		UpdateCollider();
	}

	void GameUnitC::InitializeFieldMap()
	{
		m_fieldMap.Initialize(object_fields::UnitFieldCount);
	}

	void GameUnitC::SetStealthHidden(const bool hidden)
	{
		if (m_stealthHidden == hidden)
		{
			return;
		}

		m_stealthHidden = hidden;

		// Hide the whole node hierarchy (entity, name plate, quest giver icon, attachments)
		// but keep the object alive and in memory.
		if (m_sceneNode)
		{
			m_sceneNode->SetVisible(!hidden, true);
		}
	}

	void GameUnitC::SetQuestGiverStatus(const QuestgiverStatus status)
	{
		m_questGiverStatus = status;
		if (status == questgiver_status::None)
		{
			if (m_questGiverEntity)
			{
				m_scene.DestroyEntity(*m_questGiverEntity);
				m_questGiverEntity = nullptr;
			}

			if (m_questGiverNode)
			{
				m_scene.DestroySceneNode(*m_questGiverNode);
				m_questGiverNode = nullptr;
			}

			return;
		}

		const String exclamationMesh = "Models/QuestExclamationMark.hmsh";
		const String rewardMesh = "Models/QuestCompleteMark.hmsh";

		switch (status)
		{
		case questgiver_status::Unavailable:
			SetQuestGiverMesh(exclamationMesh);
			m_questGiverEntity->SetMaterial(MaterialManager::Get().Load("Models/QuestInactive_Inst.hmi"));
			break;
		case questgiver_status::Available:
			SetQuestGiverMesh(exclamationMesh);
			m_questGiverEntity->SetMaterial(MaterialManager::Get().Load("Models/QuestMaterialBase.hmat"));
			break;
		case questgiver_status::AvailableRep:
			SetQuestGiverMesh(exclamationMesh);
			m_questGiverEntity->SetMaterial(MaterialManager::Get().Load("Models/QuestRepeatable_Inst.hmat"));
			break;
		case questgiver_status::Incomplete:
			SetQuestGiverMesh(rewardMesh);
			m_questGiverEntity->SetMaterial(MaterialManager::Get().Load("Models/QuestInactive_Inst.hmi"));
			break;
		case questgiver_status::Reward:
		case questgiver_status::RewardNoDot:
			SetQuestGiverMesh(rewardMesh);
			m_questGiverEntity->SetMaterial(MaterialManager::Get().Load("Models/QuestMaterialBase.hmat"));
			break;
		case questgiver_status::RewardRep:
			SetQuestGiverMesh(rewardMesh);
			m_questGiverEntity->SetMaterial(MaterialManager::Get().Load("Models/QuestRepeatable_Inst.hmat"));
			break;
		}
	}

	bool GameUnitC::OnAuraUpdate(io::Reader &reader)
	{
		uint32 visibleAuraCount = 0;
		if (!(reader >> io::read<uint32>(visibleAuraCount)))
		{
			return false;
		}

		// Track old auras for diffing (one entry per spell id).
		std::unordered_map<uint32, uint64> oldAuraCasterIds;
		for (const auto &aura : m_auras)
		{
			if (const auto *spell = aura->GetSpell())
			{
				oldAuraCasterIds.try_emplace(spell->id(), aura->GetCasterId());
			}
		}

		m_auras.clear();

		std::unordered_map<uint32, uint64> newAuraCasterIds;
		for (uint32 i = 0; i < visibleAuraCount; ++i)
		{
			uint32 spellId, duration;
			uint64 casterId;
			uint8 auraTypeCount;

			if (!(reader >> io::read<uint32>(spellId) >> io::read<uint32>(duration) >> io::read_packed_guid(casterId) >> io::read<uint8>(auraTypeCount)))
			{
				ELOG("Failed to read aura data for unit " << log_hex_digit(GetGuid()));
				return false;
			}

			std::vector<int32> basePoints;
			basePoints.resize(auraTypeCount);
			if (!(reader >> io::read_range(basePoints)))
			{
				ELOG("Failed to read aura base points");
				return false;
			}

			const proto_client::SpellEntry *spell = m_project.spells.getById(spellId);
			if (!spell)
			{
				ELOG("Failed to find spell for aura!");
				continue;
			}

			uint8 stackCount = 1;
			if (!(reader >> io::read<uint8>(stackCount)))
			{
				ELOG("Failed to read aura stack count");
				return false;
			}

			// Add aura
			m_auras.push_back(std::make_unique<GameAuraC>(*this, *spell, casterId, duration, stackCount));
			newAuraCasterIds.try_emplace(spellId, casterId);
		}

		// Trigger visualization events for added/removed auras
		for (const auto &[spellId, casterId] : newAuraCasterIds)
		{
			if (oldAuraCasterIds.contains(spellId))
			{
				continue;
			}

			// Newly applied aura
			if (const auto *spell = m_project.spells.getById(spellId))
			{
				GameUnitC *caster = nullptr;
				if (const auto casterUnit = ObjectMgr::Get<GameUnitC>(casterId))
				{
					caster = casterUnit.get();
				}

				NotifyAuraVisualizationApplied(*spell, caster, this);
			}
		}
		for (const auto &[spellId, casterId] : oldAuraCasterIds)
		{
			if (newAuraCasterIds.contains(spellId))
			{
				continue;
			}

			// Removed aura
			if (const auto *spell = m_project.spells.getById(spellId))
			{
				GameUnitC *caster = nullptr;
				if (const auto casterUnit = ObjectMgr::Get<GameUnitC>(casterId))
				{
					caster = casterUnit.get();
				}

				NotifyAuraVisualizationRemoved(*spell, caster, this);
			}
		}

		return reader;
	}

	const proto_client::ModelDataEntry *GameUnitC::GetDisplayModel() const
	{
		const uint32 displayId = Get<uint32>(object_fields::DisplayId);
		if (!displayId)
		{
			return nullptr;
		}

		return m_project.models.getById(displayId);
	}

	void GameUnitC::SetupSceneObjects()
	{
		GameObjectC::SetupSceneObjects();

		// These need to be set before!
		ASSERT(ObjectMgr::GetUnitNameFont());
		ASSERT(ObjectMgr::GetUnitNameFontMaterial());

		// Attach text component
		m_nameComponentNode = m_sceneNode->CreateChildSceneNode(Vector3::UnitY * 2.0f);
		m_nameComponent = std::make_unique<WorldTextComponent>(ObjectMgr::GetUnitNameFont(), ObjectMgr::GetUnitNameFontMaterial(), "");
		m_nameComponentNode->AttachObject(*m_nameComponent);
		m_nameComponent->SetVisible(ObjectMgr::GetSelectedObjectGuid() == GetGuid());

		static uint64 s_counter = 0;
		m_normalDebugObject = m_scene.CreateManualRenderObject("UnitDebug_" + std::to_string(s_counter++));
		m_sceneNode->AttachObject(*m_normalDebugObject);

		RefreshUnitName();

		// Setup object display
		OnDisplayIdChanged();
	}

	void GameUnitC::OnEntryChanged()
	{
		const int32 entryId = Get<int32>(object_fields::Entry);
		if (entryId != -1)
		{
			m_netDriver.GetCreatureData(entryId, std::static_pointer_cast<GameUnitC>(shared_from_this()));
		}
	}

	void GameUnitC::ConnectAnimationNotifySignals()
	{
		// Disconnect any existing connections
		m_animNotifyConnections.disconnect();

		if (!m_entity || !m_entity->GetSkeleton())
		{
			return;
		}

		AnimationStateSet* animStateSet = m_entity->GetAllAnimationStates();
		if (!animStateSet)
		{
			return;
		}

		Skeleton* skeleton = m_entity->GetSkeleton().get();
		if (!skeleton)
		{
			return;
		}

		// Capture weak_ptr to prevent circular reference
		std::weak_ptr<GameUnitC> weakSelf = std::static_pointer_cast<GameUnitC>(shared_from_this());

		// Iterate through all animations and subscribe to their notify signals
		const uint16 numAnims = skeleton->GetNumAnimations();
		for (uint16 i = 0; i < numAnims; ++i)
		{
			Animation* anim = skeleton->GetAnimation(i);
			if (!anim)
			{
				continue;
			}

			AnimationState* animState = animStateSet->GetAnimationState(anim->GetName());
			if (!animState)
			{
				continue;
			}

			// Subscribe to the notify signal
			m_animNotifyConnections += animState->notifyTriggered.connect(
				[weakSelf](const AnimationNotify& notify, const String& animName, const AnimationState& state)
				{
					if (const auto self = weakSelf.lock())
					{
						// Flush deferred damage display at the weapon-connects frame.
						if (notify.GetType() == AnimationNotifyType::SwingHit)
						{
							self->m_animationController->NotifyActionHit();
						}

						// Play SoundEntry-based sound notifies at the unit's position. Like footsteps,
						// skip barely-blended animations so fading transitions don't spam sounds.
						if (notify.GetType() == AnimationNotifyType::PlaySound && state.GetWeight() >= 0.35f && s_soundEntryPlayer)
						{
							const auto& soundNotify = static_cast<const PlaySoundNotify&>(notify);
							if (soundNotify.GetSoundEntryId() != 0)
							{
								s_soundEntryPlayer->PlayEntry(soundNotify.GetSoundEntryId(), self->GetPosition());
							}
						}

						// Broadcast through our signal
						self->animationNotifyTriggered(*self, notify, animName, state);
					}
				});
		}
	}

	void GameUnitC::OnScaleChanged() const
	{
		if (!m_sceneNode)
		{
			return;
		}

		const float scale = Get<float>(object_fields::Scale);
		m_sceneNode->SetScale(Vector3(scale, scale, scale));
	}

	void GameUnitC::OnFactionTemplateChanged()
	{
		m_faction = nullptr;

		const uint32 factionTemplateId = Get<uint32>(object_fields::FactionTemplate);
		m_factionTemplate = m_project.factionTemplates.getById(factionTemplateId);
		ASSERT(m_factionTemplate);

		if (m_factionTemplate)
		{
			m_faction = m_project.factions.getById(m_factionTemplate->faction());
		}
	}

	void GameUnitC::SetQuestGiverMesh(const String &meshName)
	{
		if (!m_questGiverEntity)
		{
			m_questGiverEntity = m_scene.CreateEntity(GetSceneNode()->GetName() + "_QuestStatus", meshName);
			ASSERT(m_questGiverEntity);
		}
		else
		{
			m_questGiverEntity->SetMesh(MeshManager::Get().Load(meshName));
		}

		if (!m_questGiverNode)
		{
			// Ideal size is a unit with a size of 2 units in height, but if the unit is bigger we want to offset the icon position as well as scale it up
			// so that for very big models it's not just that tiny icon floating in the sky above some giant head or something of that

			auto offset = GetDefaultQuestGiverOffset();
			float scale = 1.0f;
			if (m_entity)
			{
				offset.y = m_entity->GetBoundingBox().GetExtents().y * 2.2f;
				scale = offset.y / 2.0f;
			}

			// Hack to ensure the offset is set correctly initialized
			if (m_questOffset.y <= 0.0f)
			{
				m_questOffset = offset;
			}

			m_questGiverNode = m_sceneNode->CreateChildSceneNode(m_questOffset);
			ASSERT(m_questGiverNode);

			m_questGiverNode->SetScale(Vector3::UnitScale * scale);
			m_questGiverNode->AttachObject(*m_questGiverEntity);

			if (m_nameComponent)
			{
				SetUnitNameVisible(m_nameComponent->IsVisible());
			}
		}
	}

	void GameUnitC::RefreshUnitName()
	{
		// Ensure name component is updated to display the correct name
		if (m_nameComponent)
		{
			std::ostringstream strm;
			strm << GetName();

			if (!m_creatureInfo.subname.empty())
			{
				strm << "\n<" << m_creatureInfo.subname << ">";
			}

			m_nameComponent->SetText(strm.str());

			// Set the text color based on the unit's relationship to the player
			Color textColor = Color::White; // Default color

			// Check if this is a party member
			bool isPartyMember = false;
			if (IsPlayer())
			{
				// Check if this player is in the active player's party
				const auto activePlayer = ObjectMgr::GetActivePlayer();
				if (activePlayer && activePlayer->GetGuid() != GetGuid())
				{
					// TODO: Check if this player is in the active player's party
					// For now, we'll just check if it's a friendly player
					if (activePlayer->IsFriendlyTo(*this))
					{
						isPartyMember = true;
					}
				}
			}

			if (isPartyMember || (IsPlayer() && IsFriendly()))
			{
				// Blue for party members and friendly players
				textColor.Set(0.0f, 0.5f, 1.0f, 1.0f);
			}
			else if (IsHostile())
			{
				// Red for hostile units
				textColor.Set(1.0f, 0.0f, 0.0f, 1.0f);
			}
			else if (IsFriendly())
			{
				// Green for friendly units
				textColor.Set(0.0f, 1.0f, 0.0f, 1.0f);
			}
			else
			{
				// Yellow for neutral units
				textColor.Set(1.0f, 1.0f, 0.0f, 1.0f);
			}

			m_nameComponent->SetFontColor(textColor);
		}
	}

	void GameUnitC::SetServerControlledMovement(const bool controlled)
	{
		if (m_serverControlledMovement == controlled)
		{
			// Refresh the failsafe timer if control is re-asserted (e.g. a second charge
			// starting while the first one is still active).
			if (controlled)
			{
				m_serverControlStartTime = GetAsyncTimeMs();
			}

			return;
		}

		m_serverControlledMovement = controlled;
		m_serverControlStartTime = GetAsyncTimeMs();

		if (controlled)
		{
			// Stop all local movement immediately without sending any packets: the server
			// takes over movement control and the ack packet carries the cleaned state.
			m_movementInfo.movementFlags &= ~(movement_flags::Moving | movement_flags::Strafing |
				movement_flags::Turning | movement_flags::PositionChanging);
			m_positionLocked = false;
		}
	}

	void GameUnitC::StartMove(const bool forward)
	{
		if (forward)
		{
			m_movementInfo.movementFlags |= movement_flags::Forward;
			m_movementInfo.movementFlags &= ~movement_flags::Backward;
		}
		else
		{
			m_movementInfo.movementFlags |= movement_flags::Backward;
			m_movementInfo.movementFlags &= ~movement_flags::Forward;
		}

		UpdateMovementInfo();

		// If position was locked from a previous stop packet, use that position
		// in the packet to maintain consistency with what the server expects.
		// Don't snap the scene node - that would cause visual teleporting.
		if (m_positionLocked)
		{
			m_movementInfo.position = m_syncedPosition;
			m_positionLocked = false;
		}

		m_lastHeartbeat = m_movementInfo.timestamp;

		{
			const Vector3& p = m_movementInfo.position;
			MOVEMENT_EVENT(forward ? "MOVE_START_FWD" : "MOVE_START_BWD",
				"pos=(" << p.x << "," << p.y << "," << p.z << ")"
				<< " flags=0x" << std::hex << m_movementInfo.movementFlags << std::dec
				<< " facing=" << m_movementInfo.facing.GetValueRadians());
		}

		QueueMovementEvent(forward ? movement_event_type::StartMoveForward : movement_event_type::StartMoveBackward, m_movementInfo.timestamp, m_movementInfo);
	}

	void GameUnitC::StartStrafe(const bool left)
	{
		if (left)
		{
			m_movementInfo.movementFlags |= movement_flags::StrafeLeft;
			m_movementInfo.movementFlags &= ~movement_flags::StrafeRight;
		}
		else
		{
			m_movementInfo.movementFlags |= movement_flags::StrafeRight;
			m_movementInfo.movementFlags &= ~movement_flags::StrafeLeft;
		}

		UpdateMovementInfo();

		// If position was locked from a previous stop packet, use that position
		// in the packet to maintain consistency with what the server expects.
		// Don't snap the scene node - that would cause visual teleporting.
		if (m_positionLocked)
		{
			m_movementInfo.position = m_syncedPosition;
			m_positionLocked = false;
		}

		m_lastHeartbeat = m_movementInfo.timestamp;

		{
			const Vector3& p = m_movementInfo.position;
			MOVEMENT_EVENT(left ? "STRAFE_START_L" : "STRAFE_START_R",
				"pos=(" << p.x << "," << p.y << "," << p.z << ")"
				<< " flags=0x" << std::hex << m_movementInfo.movementFlags << std::dec
				<< " facing=" << m_movementInfo.facing.GetValueRadians());
		}

		QueueMovementEvent(left ? movement_event_type::StartStrafeLeft : movement_event_type::StartStrafeRight, m_movementInfo.timestamp, m_movementInfo);
	}

	void GameUnitC::StopMove()
	{
		m_movementInfo.movementFlags &= ~(movement_flags::Forward | movement_flags::Backward);

		// Stop movement
		UpdateMovementInfo();
		m_lastHeartbeat = m_movementInfo.timestamp;

		{
			const Vector3& p = m_movementInfo.position;
			MOVEMENT_EVENT("MOVE_STOP",
				"pos=(" << p.x << "," << p.y << "," << p.z << ")"
				<< " flags=0x" << std::hex << m_movementInfo.movementFlags << std::dec
				<< " facing=" << m_movementInfo.facing.GetValueRadians());
		}

		QueueMovementEvent(movement_event_type::StopMove, m_movementInfo.timestamp, m_movementInfo);

		// Lock the position so the next start packet uses the same position.
		// This prevents drift from physics adjustments between stop and start.
		// Only lock when the character is now fully stationary: if it is still strafing
		// (or falling), the position keeps changing and the locked position would be
		// stale by the time the next start packet sends it, which the server's speed
		// check would read as a backward teleport plus an impossibly fast catch-up.
		if (!m_movementInfo.IsChangingPosition())
		{
			LockPositionForSync();
		}
	}

	void GameUnitC::StopStrafe()
	{
		m_movementInfo.movementFlags &= ~movement_flags::Strafing;

		// Stop movement
		UpdateMovementInfo();
		m_lastHeartbeat = m_movementInfo.timestamp;

		{
			const Vector3& p = m_movementInfo.position;
			MOVEMENT_EVENT("STRAFE_STOP",
				"pos=(" << p.x << "," << p.y << "," << p.z << ")"
				<< " flags=0x" << std::hex << m_movementInfo.movementFlags << std::dec
				<< " facing=" << m_movementInfo.facing.GetValueRadians());
		}

		QueueMovementEvent(movement_event_type::StopStrafe, m_movementInfo.timestamp, m_movementInfo);

		// Lock the position so the next start packet uses the same position.
		// This prevents drift from physics adjustments between stop and start.
		// Only lock when the character is now fully stationary: stopping a strafe while
		// still running forward keeps the position changing, and the locked position
		// would be stale by the time the next start packet sends it — the server's
		// speed check would read that as a teleport and record a violation.
		if (!m_movementInfo.IsChangingPosition())
		{
			LockPositionForSync();
		}
	}

	void GameUnitC::ToggleWalkMode()
	{
		// While the server controls our movement, the walk mode packet would be suppressed;
		// toggling the flag locally anyway would desync it from the server's state.
		if (m_serverControlledMovement)
		{
			return;
		}

		const bool nowWalking = (m_movementInfo.movementFlags & movement_flags::WalkMode) == 0;

		if (nowWalking)
		{
			m_movementInfo.movementFlags |= movement_flags::WalkMode;
		}
		else
		{
			m_movementInfo.movementFlags &= ~movement_flags::WalkMode;
		}

		UpdateMovementInfo();

		QueueMovementEvent(nowWalking ? movement_event_type::StartWalk : movement_event_type::StopWalk,
			m_movementInfo.timestamp, m_movementInfo);
	}

	void GameUnitC::StartTurn(const bool left)
	{
		if (left)
		{
			m_movementInfo.movementFlags |= movement_flags::TurnLeft;
			m_movementInfo.movementFlags &= ~movement_flags::TurnRight;
		}
		else
		{
			m_movementInfo.movementFlags |= movement_flags::TurnRight;
			m_movementInfo.movementFlags &= ~movement_flags::TurnLeft;
		}

		UpdateMovementInfo();

		// If position was locked from a previous stop packet, use that position
		// in the packet to maintain consistency with what the server expects.
		// Don't snap the scene node - that would cause visual teleporting.
		if (m_positionLocked)
		{
			m_movementInfo.position = m_syncedPosition;
			m_positionLocked = false;
		}

		m_lastHeartbeat = m_movementInfo.timestamp;

		{
			const Vector3& p = m_movementInfo.position;
			MOVEMENT_EVENT(left ? "TURN_START_L" : "TURN_START_R",
				"pos=(" << p.x << "," << p.y << "," << p.z << ")"
				<< " flags=0x" << std::hex << m_movementInfo.movementFlags << std::dec
				<< " facing=" << m_movementInfo.facing.GetValueRadians());
		}

		QueueMovementEvent(left ? movement_event_type::StartTurnLeft : movement_event_type::StartTurnRight, m_movementInfo.timestamp, m_movementInfo);
	}

	void GameUnitC::StopTurn()
	{
		m_movementInfo.movementFlags &= ~movement_flags::Turning;

		UpdateMovementInfo();

		// If position was locked from a previous stop packet, use that position
		// in the packet to maintain consistency with what the server expects.
		if (m_positionLocked)
		{
			m_movementInfo.position = m_syncedPosition;
			// Don't clear the lock - turning doesn't actually change position,
			// so we want the next movement start to still use the locked position
		}

		m_lastHeartbeat = m_movementInfo.timestamp;

		{
			const Vector3& p = m_movementInfo.position;
			MOVEMENT_EVENT("TURN_STOP",
				"pos=(" << p.x << "," << p.y << "," << p.z << ")"
				<< " flags=0x" << std::hex << m_movementInfo.movementFlags << std::dec
				<< " facing=" << m_movementInfo.facing.GetValueRadians());
		}

		QueueMovementEvent(movement_event_type::StopTurn, m_movementInfo.timestamp, m_movementInfo);
	}

	void GameUnitC::SetFacing(const Radian &facing)
	{
		m_movementInfo.facing = facing;

		if (m_sceneNode)
		{
			m_sceneNode->SetOrientation(Quaternion(facing, Vector3::UnitY));

			UpdateMovementInfo();

			// If position was locked from a previous stop packet, use that position
			// in the packet to maintain consistency with what the server expects.
			// Don't snap the scene node - that would cause visual teleporting.
			// Also don't re-lock here - SetFacing doesn't stop movement, so we shouldn't
			// keep overwriting the position with old values.
			if (m_positionLocked)
			{
				m_movementInfo.position = m_syncedPosition;
				// Note: We don't clear m_positionLocked here because SetFacing
				// can be called many times in rapid succession. The lock is only
				// cleared when actual movement starts (StartMove, StartStrafe, etc.)
			}

			// Do NOT update m_lastHeartbeat here. SetFacing fires on every mouse-move
			// frame while right-mouse rotating. Resetting the heartbeat timer here
			// suppresses position heartbeats while the player is simultaneously moving,
			// causing other clients to see the character walking in place and then
			// snapping forward. Heartbeat timing is only the responsibility of
			// movement-start/stop and the heartbeat check in Update().
			QueueMovementEvent(movement_event_type::SetFacing, m_movementInfo.timestamp, m_movementInfo);
		}
	}

	void GameUnitC::SetFacingLocal(const Radian &facing)
	{
		m_movementInfo.facing = facing;

		if (m_sceneNode)
		{
			m_sceneNode->SetOrientation(Quaternion(facing, Vector3::UnitY));

			// Keep m_movementInfo position in sync so the next SetFacing or Start*
			// packet uses the correct current state. No network event is queued.
			UpdateMovementInfo();

			if (m_positionLocked)
			{
				m_movementInfo.position = m_syncedPosition;
			}
		}
	}

	void GameUnitC::ApplyRemoteFacing(const Radian &facing)
	{
		if (m_sceneNode)
		{
			m_sceneNode->SetOrientation(Quaternion(facing, Vector3::UnitY));
		}
		m_movementInfo.facing = facing;

		// Build a minimal MovementInfo for a facing-only update
		MovementInfo info;
		info.facing = facing;
		info.position = m_movementInfo.position;
		info.movementFlags = m_movementInfo.movementFlags;
		m_remoteMovementRenderer.OnAuthoritativeUpdate(info, true);
	}

	void GameUnitC::Jump()
	{
		m_pressedJump = true;
		m_jumpKeyHoldTime = 0.0f;
	}

	void GameUnitC::StopJumping()
	{
		m_pressedJump = false;
		ResetJumpState();
	}

	void GameUnitC::ClearJumpInput(const float deltaTime)
	{
		if (m_pressedJump)
		{
			m_jumpKeyHoldTime += deltaTime;

			if (m_jumpKeyHoldTime >= GetJumpMaxHoldTime())
			{
				m_pressedJump = false;
			}
		}
		else
		{
			m_jumpForceTimeRemaining = 0.0f;
			m_wasJumping = false;
		}
	}

	void GameUnitC::OnJumped()
	{
		const bool wasFalling = (m_movementInfo.movementFlags & movement_flags::Falling) != 0;

		m_lastHeartbeat = GetAsyncTimeMs();
		UpdateMovementInfo();
		m_movementInfo.movementFlags |= movement_flags::Falling;

		// If position was locked from a previous stop/land packet, use that position
		// in the packet to maintain consistency with what the server expects.
		// Don't snap the scene node - that would cause visual teleporting.
		if (m_positionLocked)
		{
			m_movementInfo.position = m_syncedPosition;
			m_positionLocked = false;
		}

		if (!wasFalling)
		{
			m_netDriver.OnMoveEvent(*this, MovementEvent(movement_event_type::Fall, m_movementInfo.timestamp, m_movementInfo));
		}
	}

	void GameUnitC::CheckJumpInput()
	{
		m_jumpCurrentCountPreJump = m_jumpCurrentCount;
		if (!m_unitMovement)
		{
			return;
		}

		if (!m_pressedJump)
		{
			return;
		}

		const bool bDidJump = CanJump() && m_unitMovement->DoJump();
		if (bDidJump)
		{
			// Transition from not (actively) jumping to jumping.
			if (!m_wasJumping)
			{
				m_jumpCurrentCount++;
				m_jumpForceTimeRemaining = GetJumpMaxHoldTime();
				OnJumped();
			}
		}

		m_wasJumping = bDidJump;
	}

	bool GameUnitC::CanJump() const
	{
		return JumpIsAllowed();
	}

	bool GameUnitC::JumpIsAllowed() const
	{
		bool jumpIsAllowed = m_unitMovement->CanAttemptJump();
		if (jumpIsAllowed)
		{
			// Ensure JumpHoldTime and JumpCount are valid.
			if (!m_wasJumping || GetJumpMaxHoldTime() <= 0.0f)
			{
				jumpIsAllowed = m_jumpCurrentCount < m_jumpMaxCount;
			}
			else
			{
				// Only consider JumpKeyHoldTime as long as:
				// A) The jump limit hasn't been met OR
				// B) The jump limit has been met AND we were already jumping
				const bool bJumpKeyHeld = (m_pressedJump && m_jumpKeyHoldTime < GetJumpMaxHoldTime());
				jumpIsAllowed = bJumpKeyHeld &&
								((m_jumpCurrentCount < m_jumpMaxCount) || (m_wasJumping && m_jumpCurrentCount == m_jumpMaxCount));
			}
		}

		return jumpIsAllowed;
	}

	void GameUnitC::SetMovementPath(const std::vector<Vector3> &points, GameTime moveTime, const std::optional<Radian> &targetRotation, const UnitMovementMode movementMode)
	{
		// Clear any existing animation-based movement
		m_movementAnimationTime = 0.0f;

		// Clear any existing path
		m_movementPath.clear();
		m_currentPathIndex = 0;
		m_targetRotation.reset();
		m_pathCompleted = false;

		if (points.empty())
		{
			return;
		}

		// Check if the new path start is close to our current position
		const Vector3 currentPosition = m_sceneNode->GetDerivedPosition();
		const Vector3 newPathStart = points[0];
		const float distanceToNewStart = (newPathStart - currentPosition).GetLength();

		// Always start from our current client position for smooth transitions.
		// The speed-based interpolation in UpdatePathMovement will naturally
		// catch up to the correct path position, eliminating visual snapping.
		Vector3 actualStartPosition = currentPosition;

		// Store the path points and target rotation
		m_movementPath = points;
		m_targetRotation = targetRotation;
		m_currentPathIndex = 0;
		m_pathCompleted = false;
		m_pathStartTime = GetAsyncTimeMs();
		m_pathStartPosition = actualStartPosition; // Use tolerance-checked start position
		m_pathMoveSpeed = GetSpeed(movement_type::Run);
		SetUnitMovementModeOnFlags(m_movementInfo.movementFlags, movementMode);

		// Initialize path gravity variables
		m_pathVerticalVelocity = 0.0f;
		m_pathOnGround = true;

		// Calculate path segment lengths and total length for time-based movement
		m_pathSegmentLengths.clear();
		m_pathTotalLength = 0.0f;

		Vector3 currentPos = m_pathStartPosition; // Start from actual starting position

		for (size_t i = 0; i < m_movementPath.size(); ++i)
		{
			Vector3 segmentEnd = m_movementPath[i];
			float segmentLength = (segmentEnd - currentPos).GetLength();
			m_pathSegmentLengths.push_back(segmentLength);
			m_pathTotalLength += segmentLength;
			currentPos = segmentEnd;
		}

		// Only follow the announced duration while it implies a plausible speed; see
		// DerivePathMoveSpeed for why an unchecked duration can teleport or freeze the unit.
		bool durationRejected = false;
		m_pathMoveSpeed = DerivePathMoveSpeed(
			m_pathTotalLength, moveTime, GetSpeed(movement_type::Run), &durationRejected);
		if (durationRejected)
		{
			WLOG("Received implausible movement path duration of " << moveTime << "ms for a path of "
				<< m_pathTotalLength << " units, falling back to run speed");
		}

		// All units use time-based direct positioning while following a path - including the
		// locally controlled player (charge). Clear ALL movement flags so the physics input
		// system doesn't fight the direct positioning (which caused the character to overshoot
		// waypoints and spin around its yaw axis), and so no heartbeats are triggered.
		m_movementInfo.movementFlags &= ~movement_flags::PositionChanging;
		m_movementInfo.movementFlags &= ~movement_flags::Forward;
		m_movementInfo.movementFlags &= ~movement_flags::Backward;
		m_movementInfo.movementFlags &= ~movement_flags::StrafeLeft;
		m_movementInfo.movementFlags &= ~movement_flags::StrafeRight;
		m_movementInfo.movementFlags &= ~movement_flags::TurnLeft;
		m_movementInfo.movementFlags &= ~movement_flags::TurnRight;
	}

	void GameUnitC::CompleteMovementPath(const bool reachedDestination)
	{
		// Apply target rotation if specified (but don't teleport to exact position)
		if (reachedDestination && m_targetRotation.has_value())
		{
			SetFacing(m_targetRotation.value());
		}

		// Clear movement flags to stop animations
		m_movementInfo.movementFlags &= ~movement_flags::Forward;
		m_movementInfo.movementFlags &= ~movement_flags::Backward;
		m_movementInfo.movementFlags &= ~movement_flags::StrafeLeft;
		m_movementInfo.movementFlags &= ~movement_flags::StrafeRight;
		m_movementInfo.movementFlags &= ~movement_flags::PositionChanging;

		// The animation controller picks up the cleared movement flags on its next update
		// and blends back to idle (or keeps a locked spell cast animation) automatically.

		// Complete the path
		m_pathCompleted = true;
		m_movementPath.clear();
		m_pathSegmentLengths.clear();
		m_currentPathIndex = 0;

		// Reset gravity variables
		m_pathVerticalVelocity = 0.0f;
		m_pathOnGround = true;

		// Hand movement control back to the local player if the server had taken it (charge)
		if (IsControlledByLocalPlayer() && m_serverControlledMovement)
		{
			SetServerControlledMovement(false);

			// Notify the server that we finished the movement path. Only done when the
			// destination was actually reached: if the path was cut short (e.g. by a stop
			// packet after an interrupt), the accompanying ack packet already resyncs the
			// position and a MoveEnded would fail the server's destination check.
			if (reachedDestination)
			{
				movementEnded(*this, m_movementInfo);
			}
		}
	}

	void GameUnitC::UpdatePathMovement(const float deltaTime)
	{
		if (m_movementPath.empty() || m_pathCompleted)
		{
			return;
		}

		// Zero-length path means "stop here" (e.g. a CreatureMove stop packet whose
		// destination is exactly the client's current position). Complete immediately
		// so the animation controller can run and animations reset properly.
		if (m_pathTotalLength <= 0.0f)
		{
			CompleteMovementPath(false);
			return;
		}

		// Calculate how much time has passed since path started
		const GameTime currentTime = GetAsyncTimeMs();
		const float elapsedTime = static_cast<float>(currentTime - m_pathStartTime) / 1000.0f; // Convert to seconds

		// Calculate how far along the path we should be based on movement speed
		const float pathMoveSpeed = m_pathMoveSpeed > 0.0f ? m_pathMoveSpeed : GetSpeed(movement_type::Run);
		const float targetDistance = pathMoveSpeed * elapsedTime;

		// Check if we're close enough to the final destination. Only the horizontal (XZ)
		// distance is considered: the destination height comes from the server's nav mesh,
		// which can legitimately differ from the client's detailed ground geometry by a few
		// units. Including that difference here could prevent the path from ever completing,
		// leaving movement flags and server control stuck until relog.
		const Vector3 currentPosition = m_sceneNode->GetDerivedPosition();
		const Vector3 finalDestination = m_movementPath.back();
		const float horizontalDistanceToDestination =
			::sqrtf((finalDestination.x - currentPosition.x) * (finalDestination.x - currentPosition.x) +
				(finalDestination.z - currentPosition.z) * (finalDestination.z - currentPosition.z));

		// Keep this radius small: on completion the unit is snapped onto the exact
		// destination, so anything larger than roughly one frame of travel shows up as a
		// visible teleport. The speed-based interpolation below always lands the node
		// exactly on the destination on its own (positions are set directly, nothing can
		// block the horizontal approach), so a tight radius does not endanger completion —
		// the unit simply glides the remaining distance at movement speed first.
		constexpr float arrivalThreshold = 0.15f; // Distance-based completion only

		// The vertical tolerance mirrors the server's nav-mesh-vs-ground tolerance (3.1). It
		// prevents premature completion on stacked geometry (e.g. a path ending directly above
		// on a staircase) while still completing despite legitimate nav mesh height error.
		constexpr float arrivalHeightTolerance = 3.5f;
		const float finalSegmentLength = m_pathSegmentLengths.empty() ? 0.0f : m_pathSegmentLengths.back();
		const bool nearDestination = HasReachedPathDestination(
			targetDistance,
			m_pathTotalLength,
			finalSegmentLength,
			horizontalDistanceToDestination,
			::fabsf(finalDestination.y - currentPosition.y),
			arrivalThreshold,
			arrivalHeightTolerance);

		// Failsafe: if we overshot the expected path length by a large margin without getting
		// close to the destination (e.g. blocked by client-side collision), snap to the
		// destination and finish. A path must never remain active forever.
		const bool forceComplete = targetDistance > m_pathTotalLength + 5.0f;

		// Complete path ONLY when we're actually close to destination (no time-based teleportation)
		if (nearDestination || forceComplete)
		{
			// Snap horizontally onto the exact destination so that our reported end position
			// matches the server's expected movement target (the height is kept and follows
			// the client's ground geometry).
			m_sceneNode->SetPosition(Vector3(finalDestination.x, currentPosition.y, finalDestination.z));
			if (m_unitMovement)
			{
				m_unitMovement->CorrectGroundHeight();
			}

			CompleteMovementPath(true);
			return;
		}

		// Continue moving along path even if we're past the calculated time
		// This allows units to finish paths naturally without teleportation
		Vector3 targetPosition;

		if (targetDistance >= m_pathTotalLength)
		{
			// We're past the calculated end time, but move towards final destination naturally
			targetPosition = finalDestination;
		}
		else
		{
			// Find which segment we should be on and calculate position along it
			targetPosition = CalculatePositionAlongPath(targetDistance);
		}

		// Move towards target position smoothly using speed-based interpolation
		const Vector3 unitCurrentPosition = m_sceneNode->GetDerivedPosition();
		Vector3 direction = targetPosition - unitCurrentPosition;
		const float distanceFromTarget = direction.GetLength();

		// Use actual movement speed scaled by deltaTime for smooth, framerate-independent movement.
		// Add a catch-up factor when falling behind the computed path position to avoid persistent lag.
		const float baseMove = pathMoveSpeed * deltaTime;
		const float catchUpFactor = std::min(distanceFromTarget / std::max(baseMove, 0.01f), 3.0f);
		const float maxMoveThisFrame = baseMove * std::max(catchUpFactor, 1.0f);

		if (distanceFromTarget > 0.01f)
		{
			Vector3 newPosition;

			if (distanceFromTarget <= maxMoveThisFrame)
			{
				// Close enough - move directly to target position
				newPosition = targetPosition;
			}
			else
			{
				// Move towards target at appropriate speed with catch-up
				direction.Normalize();
				newPosition = unitCurrentPosition + direction * maxMoveThisFrame;
			}

			m_sceneNode->SetPosition(newPosition);

			// Correct the height to match the client's ground geometry
			// This is needed because the server's navmesh may not match the client's detailed collision
			if (m_unitMovement)
			{
				m_unitMovement->CorrectGroundHeight();
			}

			// The animation controller drives the run/walk animation from the path-moving
			// state in its per-frame update.
		}

		// Update facing direction towards movement direction
		if (!m_movementPath.empty())
		{
			const Vector3 currentPos = m_sceneNode->GetDerivedPosition();

			// Find the next waypoint we're moving towards
			size_t nextWaypointIndex = 0;
			float accumulatedDistance = 0.0f;

			for (size_t i = 0; i < m_pathSegmentLengths.size(); ++i)
			{
				if (targetDistance <= accumulatedDistance + m_pathSegmentLengths[i])
				{
					nextWaypointIndex = i;
					break;
				}
				accumulatedDistance += m_pathSegmentLengths[i];
				nextWaypointIndex = i + 1;
			}

			// Update current path index for other systems
			m_currentPathIndex = nextWaypointIndex;

			if (nextWaypointIndex < m_movementPath.size())
			{
				const Vector3 nextWaypoint = m_movementPath[nextWaypointIndex];

				// The yaw is computed from the horizontal (XZ) components only, so the guard
				// has to check the horizontal distance as well: a mostly vertical offset (nav
				// mesh height vs. client ground height) would otherwise pass the check and
				// produce erratic facing values from near-zero horizontal deltas.
				const float horizontalDistance =
					::sqrtf((nextWaypoint.x - currentPos.x) * (nextWaypoint.x - currentPos.x) +
						(nextWaypoint.z - currentPos.z) * (nextWaypoint.z - currentPos.z));

				if (horizontalDistance > 0.05f)
				{
					const Radian targetYaw = GetAngle(currentPos.x, currentPos.z, nextWaypoint.x, nextWaypoint.z);
					SetFacing(targetYaw);
				}
			}
		}
	}

	Vector3 GameUnitC::CalculatePositionAlongPath(float distance)
	{
		if (m_movementPath.empty() || m_pathSegmentLengths.empty())
		{
			return m_sceneNode->GetDerivedPosition();
		}

		return SamplePathPosition(m_pathStartPosition, m_movementPath, m_pathSegmentLengths, distance);
	}

	bool GameUnitC::IsControlledByLocalPlayer() const
	{
		const auto activePlayer = ObjectMgr::GetActivePlayer();
		return activePlayer && activePlayer.get() == this;
	}

	static void SetQueryMask(SceneNode *node, const uint32 mask)
	{
		for (uint32 i = 0; i < node->GetNumAttachedObjects(); ++i)
		{
			node->GetAttachedObject(i)->SetQueryFlags(mask);
		}

		for (uint32 i = 0; i < node->GetNumChildren(); ++i)
		{
			SetQueryMask(static_cast<SceneNode *>(node->GetChild(i)), mask);
		}
	}

	void GameUnitC::SetQueryMask(const uint32 mask)
	{
		mmo::SetQueryMask(m_sceneNode, mask);
	}

	bool GameUnitC::CanBeLooted() const
	{
		// Check if lootable flag is set!
		return (Get<uint32>(object_fields::Flags) & unit_flags::Lootable) != 0;
	}

	void GameUnitC::NotifySpellCastStarted()
	{
		// Legacy method - now handled by SpellVisualizationService
	}

	void GameUnitC::NotifySpellCastCancelled()
	{
		// Legacy method - now handled by SpellVisualizationService
	}

	void GameUnitC::NotifySpellCastSucceeded()
	{
		// Legacy method - now handled by SpellVisualizationService
	}

	bool GameUnitC::IsFriendly() const
	{
		if (ObjectMgr::GetActivePlayerGuid() == GetGuid())
		{
			return true;
		}

		if (ObjectMgr::GetActivePlayerGuid() == 0)
		{
			return false;
		}

		const auto player = ObjectMgr::GetActivePlayer();
		if (!player)
		{
			return false;
		}

		return IsFriendlyTo(*player);
	}

	bool GameUnitC::IsHostile() const
	{
		if (ObjectMgr::GetActivePlayerGuid() == GetGuid())
		{
			return true;
		}

		if (ObjectMgr::GetActivePlayerGuid() == 0)
		{
			return false;
		}

		const auto player = ObjectMgr::GetActivePlayer();
		if (!player)
		{
			return false;
		}

		return IsHostileTo(*player);
	}

	int32 GameUnitC::GetPower(const int32 powerType) const
	{
		if (powerType < 0 || powerType >= power_type::Health)
		{
			return 0;
		}

		return Get<int32>(object_fields::Mana + powerType);
	}

	int32 GameUnitC::GetMaxPower(const int32 powerType) const
	{
		if (powerType < 0 || powerType >= power_type::Health)
		{
			return 0;
		}

		return Get<int32>(object_fields::MaxMana + powerType);
	}

	int32 GameUnitC::GetStat(int32 statId) const
	{
		if (statId < 0 || statId >= 5)
		{
			return 0;
		}

		return Get<int32>(object_fields::StatStamina + statId);
	}

	int32 GameUnitC::GetPosStat(int32 statId) const
	{
		if (statId < 0 || statId >= 5)
		{
			return 0;
		}

		return Get<int32>(object_fields::PosStatStamina + statId);
	}

	int32 GameUnitC::GetNegStat(int32 statId) const
	{
		if (statId < 0 || statId >= 5)
		{
			return 0;
		}

		return Get<int32>(object_fields::NegStatStamina + statId);
	}

	float GameUnitC::GetArmorReductionFactor() const
	{
		float armor = static_cast<float>(GetArmor());
		if (armor < 0.0f)
		{
			armor = 0.0f;
		}

		// If factor is 0.6, damage is reduced by 60%
		const float factor = armor / (armor + 400.0f + GetLevel() * 85.0f);
		return Clamp(factor, 0.0f, 0.75f);
	}

	int32 GameUnitC::GetHealthFromStat(const int32 statId) const
	{
		int32 stat = 0;

		if (IsPlayer())
		{
			// 20 is the base value
			const int32 statValue = GetStat(statId) - 20;

			const auto *classEntry = AsPlayer().GetClass();
			if (classEntry)
			{
				for (const auto &source : classEntry->healthstatsources())
				{
					if (source.statid() == statId)
					{
						stat += static_cast<int32>(source.factor() * static_cast<float>(statValue));
					}
				}
			}
		}

		return stat;
	}

	int32 GameUnitC::GetManaFromStat(int32 statId) const
	{
		int32 stat = 0;

		if (IsPlayer())
		{
			// 20 is the base value
			const int32 statValue = GetStat(statId) - 20;

			const auto *classEntry = AsPlayer().GetClass();
			if (classEntry)
			{
				for (const auto &source : classEntry->manastatsources())
				{
					if (source.statid() == statId)
					{
						stat += static_cast<int32>(source.factor() * static_cast<float>(statValue));
					}
				}
			}
		}

		return stat;
	}

	int32 GameUnitC::GetAttackPowerFromStat(int32 statId) const
	{
		int32 stat = 0;

		if (IsPlayer())
		{
			// 20 is the base value
			const int32 statValue = GetStat(statId);

			const auto *classEntry = AsPlayer().GetClass();
			if (classEntry)
			{
				for (const auto &source : classEntry->attackpowerstatsources())
				{
					if (source.statid() == statId)
					{
						stat += static_cast<int32>(source.factor() * static_cast<float>(statValue));
					}
				}
			}
		}

		return stat;
	}

	uint8 GameUnitC::GetAttributeCost(uint32 attribute) const
	{
		return 0;
	}

	GameAuraC *GameUnitC::GetAura(uint32 index) const
	{
		if (index < m_auras.size())
		{
			return m_auras[index].get();
		}

		return nullptr;
	}

	bool GameUnitC::HasAura(uint32 spellId) const
	{
		for (const auto& aura : m_auras)
		{
			const auto* spell = aura->GetSpell();
			if (spell && spell->id() == spellId && !aura->IsExpired())
			{
				return true;
			}
		}

		return false;
	}

	void GameUnitC::SetTargetUnit(const std::shared_ptr<GameUnitC> &targetUnit)
	{
		if (m_targetUnit.expired() && !targetUnit)
		{
			return;
		}

		// Check if the target unit actually changed
		{
			const auto prevUnit = m_targetUnit.lock();
			if (targetUnit && prevUnit && prevUnit->GetGuid() == targetUnit->GetGuid())
			{
				return;
			}
		}

		m_targetUnit = targetUnit;

		if (GetGuid() == ObjectMgr::GetActivePlayerGuid())
		{
			ObjectMgr::SetSelectedObjectGuid(targetUnit ? targetUnit->GetGuid() : 0);
			m_netDriver.SetSelectedTarget(targetUnit ? targetUnit->GetGuid() : 0);
		}
	}

	void GameUnitC::SetInitialSpells(const std::vector<const proto_client::SpellEntry *> &spells)
	{
		m_spells = spells;

		m_spellBookSpells.clear();
		for (const auto *spell : m_spells)
		{
			if ((spell->attributes(0) & static_cast<uint32>(spell_attributes::HiddenClientSide)) == 0)
			{
				m_spellBookSpells.push_back(spell);
			}
		}
	}

	void GameUnitC::LearnSpell(const proto_client::SpellEntry *spell)
	{
		const auto it = std::find_if(m_spells.begin(), m_spells.end(), [spell](const proto_client::SpellEntry *entry)
									 { return entry->id() == spell->id(); });
		if (it == m_spells.end())
		{
			m_spells.push_back(spell);

			// Visible?
			if ((spell->attributes(0) & static_cast<uint32>(spell_attributes::HiddenClientSide)) == 0)
			{
				m_spellBookSpells.push_back(spell);
			}
		}
	}

	void GameUnitC::UnlearnSpell(const uint32 spellId)
	{
		std::erase_if(m_spells, [spellId](const proto_client::SpellEntry *entry)
					  { return entry->id() == spellId; });
		std::erase_if(m_spellBookSpells, [spellId](const proto_client::SpellEntry *entry)
					  { return entry->id() == spellId; });
	}

	bool GameUnitC::HasSpell(uint32 spellId) const
	{
		return std::find_if(m_spells.begin(), m_spells.end(), [spellId](const proto_client::SpellEntry *entry)
							{ return entry->id() == spellId; }) != m_spells.end();
	}

	const proto_client::SpellEntry *GameUnitC::GetSpell(uint32 index) const
	{
		if (index < m_spells.size())
		{
			return m_spells[index];
		}

		return nullptr;
	}

	const proto_client::SpellEntry *GameUnitC::GetVisibleSpell(uint32 index) const
	{
		if (index < m_spellBookSpells.size())
		{
			return m_spellBookSpells[index];
		}

		return nullptr;
	}

	void GameUnitC::Attack(GameUnitC &victim)
	{
		// Don't do anything if the victim is already the current target
		if (IsAttacking(victim))
		{
			return;
		}

		// We cant attack ourselves
		if (&victim == this)
		{
			return;
		}

		// Ensure that we are targeting the victim right now
		// TODO

		// Send attack
		m_victim = victim.GetGuid();
		m_netDriver.SendAttackStart(victim.GetGuid(), GetAsyncTimeMs());
	}

	void GameUnitC::StopAttack()
	{
		if (!IsAttacking())
		{
			return;
		}

		// Send stop attack
		NotifyAttackStopped();
		m_netDriver.SendAttackStop(GetAsyncTimeMs());
	}

	void GameUnitC::NotifyAttackStopped()
	{
		m_victim = 0;
	}

	void GameUnitC::SetCreatureInfo(const CreatureInfo &creatureInfo)
	{
		m_creatureInfo = creatureInfo;
		RefreshUnitName();
	}

	const String &GameUnitC::GetName() const
	{
		if (m_creatureInfo.name.empty())
		{
			return GameObjectC::GetName();
		}

		return m_creatureInfo.name;
	}

	void GameUnitC::SetLockedLoopAnimation(AnimationState* state)
	{
		m_animationController->SetSpellLoopAnimation(state);
	}

	AnimationState* GameUnitC::ResolveEmoteAnimation(const uint32 emoteId) const
	{
		if (emoteId == 0 || !m_entity)
		{
			return nullptr;
		}

		const proto_client::EmoteEntry* emote = m_project.emotes.getById(emoteId);
		if (!emote || emote->animation().empty())
		{
			return nullptr;
		}

		if (!m_entity->HasAnimationState(emote->animation()))
		{
			return nullptr;
		}

		return m_entity->GetAnimationState(emote->animation());
	}

	const proto_client::EmoteEntry* GameUnitC::ResolvePoseEmoteEntry(const unit_stand_state::Type standState) const
	{
		if (!m_entity)
		{
			return nullptr;
		}

		// Prefer the selected pose variant when one is set and its clip exists on this mesh.
		uint32 variantField = 0;
		switch (standState)
		{
		case unit_stand_state::Sit:
			variantField = object_fields::SitPoseEmote;
			break;
		case unit_stand_state::Sleep:
			variantField = object_fields::SleepPoseEmote;
			break;
		case unit_stand_state::Kneel:
			break;
		default:
			return nullptr;
		}

		if (variantField != 0)
		{
			if (const uint32 variantId = Get<uint32>(variantField); variantId != 0)
			{
				const proto_client::EmoteEntry* variant = m_project.emotes.getById(variantId);
				if (variant && !variant->animation().empty() && m_entity->HasAnimationState(variant->animation()))
				{
					return variant;
				}
			}
		}

		// Fall back to the Pose catalog entry for this stand state - it carries the default
		// loop clip plus the optional enter/exit transition clips.
		for (const auto& entry : m_project.emotes.getTemplates().entry())
		{
			if (entry.emotetype() == emote_type::Pose && entry.standstate() == static_cast<uint32>(standState) &&
				!entry.animation().empty() && m_entity->HasAnimationState(entry.animation()))
			{
				return &entry;
			}
		}

		return nullptr;
	}

	AnimationState* GameUnitC::ResolvePoseTransitionClip(const proto_client::EmoteEntry& entry, const bool exit) const
	{
		const std::string& clipName = exit ? entry.animationend() : entry.animationstart();
		if (clipName.empty() || !m_entity || !m_entity->HasAnimationState(clipName))
		{
			return nullptr;
		}

		return m_entity->GetAnimationState(clipName);
	}

	void GameUnitC::PlayEmote(const uint32 emoteId)
	{
		// Pose stand states (sit, sleep, kneel) own the body animation: a one-shot emote
		// would tear the pose loop apart and snap back right after, which looks broken.
		// The emote's chat line arrives as a separate chat packet and still fires.
		const unit_stand_state::Type standState = GetStandState();
		if (standState == unit_stand_state::Sit || standState == unit_stand_state::Sleep ||
			standState == unit_stand_state::Kneel)
		{
			return;
		}

		AnimationState* state = ResolveEmoteAnimation(emoteId);
		if (!state)
		{
			return;
		}

		state->SetLoop(false);
		state->SetPlayRate(1.0f);
		PlayOneShotAnimation(state);
	}

	void GameUnitC::RefreshPoseAnimation(const bool withTransition)
	{
		m_animationController->ResetIdleTimer();

		const unit_stand_state::Type standState = GetStandState();
		if (standState == unit_stand_state::Sit || standState == unit_stand_state::Sleep ||
			standState == unit_stand_state::Kneel)
		{
			const proto_client::EmoteEntry* entry = ResolvePoseEmoteEntry(standState);

			AnimationState* poseState = nullptr;
			if (entry)
			{
				poseState = m_entity->GetAnimationState(entry->animation());
			}
			else if (m_entity)
			{
				// Last-resort convention clip for catalogs without a matching Pose entry.
				// Loop only - transition clips require an emote entry.
				const char* defaultClip = standState == unit_stand_state::Sit ? "Sit"
					: (standState == unit_stand_state::Sleep ? "Sleep" : "Kneel");
				if (m_entity->HasAnimationState(defaultClip))
				{
					poseState = m_entity->GetAnimationState(defaultClip);
				}
			}

			if (poseState)
			{
				// A stand-state change means entering the pose; the same stand state means a
				// /pose variant swap, which snaps to the new loop without a transition.
				const bool enteringNewPose = m_poseStandState != standState;

				poseState->SetLoop(true);
				poseState->SetPlayRate(1.0f);
				m_activePoseEmoteId = entry ? entry->id() : 0;
				m_poseStandState = standState;
				m_animationController->SetPoseLoop(*poseState);

				if (withTransition && enteringNewPose && entry && IsAlive())
				{
					if (AnimationState* startState = ResolvePoseTransitionClip(*entry, false))
					{
						startState->SetLoop(false);
						startState->SetPlayRate(1.0f);
						m_animationController->PlayPoseTransition(startState);
					}
				}
				return;
			}
		}

		// Standing (or no usable pose clip): release the pose lock. Only the pose lock is
		// cleared here - a looping spell-cast animation set through the same locked-loop slot
		// must survive stand-state changes.
		m_animationController->ClearPoseLoop();

		// A voluntary stand-up (still stationary) plays the pose's exit transition clip.
		// Movement cancels skip it so controls stay responsive; a transition that movement
		// catches mid-play is fast-forwarded by the animation controller.
		if (withTransition && m_poseStandState != unit_stand_state::Stand &&
			m_activePoseEmoteId != 0 && !m_movementInfo.IsChangingPosition() && IsAlive())
		{
			if (const proto_client::EmoteEntry* entry = m_project.emotes.getById(m_activePoseEmoteId))
			{
				if (AnimationState* endState = ResolvePoseTransitionClip(*entry, true))
				{
					endState->SetLoop(false);
					endState->SetPlayRate(1.0f);
					m_animationController->PlayPoseTransition(endState);
				}
			}
		}

		m_activePoseEmoteId = 0;
		m_poseStandState = unit_stand_state::Stand;
	}

	void GameUnitC::RefreshMoodAnimation()
	{
		// The face overlay layer masks the clip to "face_" bones and layers it over
		// whatever the body is doing (nullptr clears the layer).
		m_animationController->SetMoodClip(ResolveEmoteAnimation(Get<uint32>(object_fields::MoodEmote)));
	}

	bool GameUnitC::PlayOneShotAnimation(AnimationState* animState, const bool suppressIfBusy)
	{
		return m_animationController->PlayAction(animState, suppressIfBusy);
	}

	bool GameUnitC::IsPlayingOneShotAnimation() const
	{
		return m_animationController->IsActionPlaying();
	}

	void GameUnitC::QueueSwingHitCallback(std::function<void()> callback)
	{
		m_animationController->QueueActionHitCallback(std::move(callback));
	}

	void GameUnitC::CancelOneShotAnimation()
	{
		m_animationController->CancelAction();
	}

	bool GameUnitC::NotifyAttackSwingEvent(const bool offhand)
	{
		// Off-hand swings are suppressed when the main-hand animation is still fresh to avoid
		// the jarring visual of hard-cutting a recently-started clip.
		return m_animationController->PlayAttackSwing(offhand);
	}

	void GameUnitC::SetWeaponAttackAnimations(const std::vector<String>& animNames)
	{
		m_animationController->SetAttackClips(animNames);
	}

	void GameUnitC::SetOffhandWeaponAttackAnimations(const std::vector<String>& animNames)
	{
		m_animationController->SetOffhandAttackClips(animNames);
	}

	void GameUnitC::SetWeaponReadyAnimation(const String& animName)
	{
		m_animationController->SetCombatReadyClip(animName);
	}

	void GameUnitC::NotifyHitEvent()
	{
		m_animationController->PlayHit();
	}

	void GameUnitC::AddProficiency(const uint32 proficiencyId)
	{
		if (proficiencyId > 0)
		{
			m_proficiencies.insert(proficiencyId);
		}
	}

	void GameUnitC::RemoveProficiency(const uint32 proficiencyId)
	{
		m_proficiencies.erase(proficiencyId);
	}

	bool GameUnitC::HasProficiency(const uint32 proficiencyId) const
	{
		// Proficiency ID 0 means no proficiency required
		if (proficiencyId == 0)
		{
			return true;
		}

		return m_proficiencies.contains(proficiencyId);
	}

	bool GameUnitC::IsFriendlyTo(const GameUnitC &other) const
	{
		if (m_factionTemplate == nullptr || other.m_factionTemplate == nullptr)
		{
			return false;
		}

		if (m_faction == nullptr || other.m_faction == nullptr)
		{
			return false;
		}

		// Same faction template should always be friendly
		if (m_factionTemplate == other.m_factionTemplate)
		{
			return true;
		}

		if (m_faction == other.m_faction)
		{
			return true;
		}

		return std::find_if(m_factionTemplate->friends().begin(), m_factionTemplate->friends().end(), [&other](uint32 factionId)
							{ return factionId == other.GetFaction()->id(); }) != m_factionTemplate->friends().end();
	}

	bool GameUnitC::IsHostileTo(const GameUnitC &other) const
	{
		if (m_factionTemplate == nullptr || other.m_factionTemplate == nullptr)
		{
			return false;
		}

		if (m_faction == nullptr || other.m_faction == nullptr)
		{
			return false;
		}

		// Same faction template should always never be hostile
		if (m_factionTemplate == other.m_factionTemplate)
		{
			return false;
		}

		if (m_faction == other.m_faction)
		{
			return false;
		}

		return std::find_if(m_factionTemplate->enemies().begin(), m_factionTemplate->enemies().end(), [&other](uint32 factionId)
							{ return factionId == other.GetFaction()->id(); }) != m_factionTemplate->enemies().end();
	}

	bool GameUnitC::HasStealthAura() const
	{
		for (const auto& aura : m_auras)
		{
			if (!aura || aura->IsExpired())
			{
				continue;
			}

			const proto_client::SpellEntry* spell = aura->GetSpell();
			if (!spell)
			{
				continue;
			}

			for (const auto& effect : spell->effects())
			{
				if (effect.aura() == aura_type::ModStealth)
				{
					return true;
				}
			}
		}

		return false;
	}

	void GameUnitC::UpdateSpecialIdleClip()
	{
		String clipName;
		if (const uint32 emoteId = Get<uint32>(object_fields::IdlePoseEmote); emoteId != 0)
		{
			if (const proto_client::EmoteEntry* emote = m_project.emotes.getById(emoteId))
			{
				clipName = emote->animation();
			}
		}

		m_animationController->SetSpecialIdleClip(clipName);
	}

	void GameUnitC::OnDisplayIdChanged()
	{
		const uint32 displayId = Get<uint32>(object_fields::DisplayId);
		const proto_client::ModelDataEntry *modelEntry = ObjectMgr::GetModelData(displayId);

		// Always drop animation clip pointers first so no stale pointer survives a mesh
		// change, even when there is no valid model entry and we return early.
		m_animationController->NotifyMeshChanged();
		m_customizationDefinition = nullptr;

		if (m_entity)
			m_entity->SetVisible(modelEntry != nullptr);
		if (!modelEntry)
		{
			return;
		}

		String meshFile = modelEntry->filename();
		if (modelEntry->flags() & model_data_flags::IsCustomizable)
		{
			// Check if the model is a mesh file or a .char file
			m_customizationDefinition = AvatarDefinitionManager::Get().Load(modelEntry->filename());
			if (!m_customizationDefinition)
			{
				ELOG("Failed to find customizable avatar definition for " << modelEntry->filename());
				return;
			}

			meshFile = m_customizationDefinition->GetBaseMesh();

			if (!IsPlayer())
			{
				m_configuration.chosenOptionPerGroup.clear();
				m_configuration.scalarValues.clear();
				for (const auto &kvp : modelEntry->customizationproperties())
				{
					m_configuration.chosenOptionPerGroup[kvp.first] = kvp.second;
				}
			}
		}

		// Update or create entity
		if (!m_entity)
		{
			m_entity = m_scene.CreateEntity(std::to_string(GetGuid()), meshFile);
			m_entity->SetUserObject(this);
			m_entity->SetQueryFlags(0x00000002);
			m_entityOffsetNode->AttachObject(*m_entity);
		}
		else
		{
			// SetMesh destroys all SubEntity objects, so any raw SubEntity* pointers in
			// m_tintMaterialStates would become dangling. Clear the stale state first.
			m_tintMaterialStates.clear();
			// Just update the mesh
			m_entity->SetMesh(MeshManager::Get().Load(meshFile));
			// Re-create material instances for the new sub-entities if tints are still active.
			if (!m_spellTints.empty())
			{
				EnsureMaterialInstances();
				UpdateTintOnMaterials();
			}
		}

		if (m_customizationDefinition)
		{
			m_configuration.Apply(*this, *m_customizationDefinition);
		}

		// Bind the model's animation profile (0 = built-in default clip names) and
		// re-resolve all animation clip bindings against the new mesh.
		m_animationController->SetProfileId(modelEntry->animation_profile());
		m_animationController->NotifyMeshChanged();
		UpdateSpecialIdleClip();

		if (m_entity)
		{
			m_nameComponentNode->SetPosition(Vector3::UnitY * (m_entity->GetBoundingRadius()));
		}

		// Connect to animation notify signals for all animations
		ConnectAnimationNotifySignals();

		// Re-apply the replicated pose and mood on the new mesh (also runs on initial spawn;
		// both are no-ops when the unit is standing with a neutral mood). No transition clips:
		// a unit that is already posing must snap straight into the loop.
		RefreshPoseAnimation(false);
		RefreshMoodAnimation();

		OnScaleChanged();
	}

	void GameUnitC::UpdateCollider()
	{
		constexpr float radius = 0.25f;

		float halfHeight = 0.65f;
		if (m_entity && m_entity->GetMesh())
		{
			halfHeight = std::max(0.0f, m_entity->GetBoundingBox().GetExtents().y - radius);
		}

		m_collider.Update(
			GetPosition() + Vector3(0.0f, radius, 0.0f),
			GetPosition() + Vector3(0.0f, radius + halfHeight * 2.0f, 0.0f), radius);
	}

	Vector3 GameUnitC::GetDefaultQuestGiverOffset()
	{
		// Ideal size is a unit with a size of 2 units in height, but if the unit is bigger we want to offset the icon position as well as scale it up
		// so that for very big models its not just that tiny icon floating in the sky above some giant head or something of that
		float height = 2.0f;
		float scale = 1.0f;
		if (m_entity)
		{
			height = m_entity->GetBoundingBox().GetExtents().y * 2.2f;
			scale = height / 2.0f;
		}

		return Vector3::UnitY * height;
	}

	void GameUnitC::LockPositionForSync()
	{
		m_syncedPosition = m_sceneNode->GetPosition();
		m_positionLocked = true;
	}

	void GameUnitC::GetSyncedPositionForPacket(Vector3& outPosition)
	{
		if (m_positionLocked)
		{
			outPosition = m_syncedPosition;
			m_positionLocked = false;
			
			// Also snap the scene node to the synced position to keep physics in sync
			m_sceneNode->SetPosition(m_syncedPosition);
		}
		else
		{
			outPosition = m_sceneNode->GetPosition();
		}
	}

	void GameUnitC::UpdateMovementInfo()
	{
		m_movementInfo.timestamp = GetAsyncTimeMs();
		m_movementInfo.position = m_sceneNode->GetPosition();
		m_movementInfo.facing = m_sceneNode->GetOrientation().GetYaw();

		// Ensure falling flags and properties are set
		if (m_unitMovement->IsFalling())
		{
			m_movementInfo.movementFlags |= movement_flags::Falling;
			m_movementInfo.jumpVelocity = m_unitMovement->GetVelocity();
		}
		else
		{
			m_movementInfo.movementFlags &= ~movement_flags::Falling;
			m_movementInfo.fallTime = 0;
			m_movementInfo.jumpVelocity = Vector3::Zero;
		}
	}

	void GameUnitC::ResetJumpState()
	{
		m_pressedJump = false;
		m_wasJumping = false;
		m_jumpKeyHoldTime = 0.0f;
		m_jumpForceTimeRemaining = 0.0f;

		if (m_unitMovement && !m_unitMovement->IsFalling())
		{
			m_jumpCurrentCount = 0;
			m_jumpCurrentCountPreJump = 0;
		}
	}

	void GameUnitC::Apply(const VisibilitySetPropertyGroup &group, const AvatarConfiguration &configuration)
	{
		// First, hide all sub entities with the given visibility set tag
		if (!group.subEntityTag.empty())
		{
			for (uint16 i = 0; i < m_entity->GetNumSubEntities(); ++i)
			{
				ASSERT(m_entity->GetMesh()->GetSubMeshCount() == m_entity->GetNumSubEntities());

				SubMesh &subMesh = m_entity->GetMesh()->GetSubMesh(i);
				if (subMesh.HasTag(group.subEntityTag))
				{
					SubEntity *subEntity = m_entity->GetSubEntity(i);
					ASSERT(subEntity);
					subEntity->SetVisible(false);
				}
			}
		}

		const auto it = configuration.chosenOptionPerGroup.find(group.GetId());

		// Determine which value to use: the configured one, or fall back to the
		// first available option so that tagged sub-entities are not left hidden
		// when the configuration is incomplete.
		uint32 chosenValue = 0;
		if (it != configuration.chosenOptionPerGroup.end())
		{
			chosenValue = it->second;
		}
		else if (!group.possibleValues.empty())
		{
			chosenValue = group.possibleValues.front().valueId;
		}
		else
		{
			// No possible values defined — nothing to show
			return;
		}

		// Now make each referenced sub entity visible
		for (const auto &value : group.possibleValues)
		{
			if (value.valueId == chosenValue)
			{
				for (const auto &subEntityName : value.visibleSubEntities)
				{
					if (SubEntity *subEntity = m_entity->GetSubEntity(subEntityName))
					{
						subEntity->SetVisible(true);
					}
				}
			}
		}
	}

	void GameUnitC::Apply(const MaterialOverridePropertyGroup &group, const AvatarConfiguration &configuration)
	{
		const auto it = configuration.chosenOptionPerGroup.find(group.GetId());
		if (it == configuration.chosenOptionPerGroup.end())
		{
			// Nothing to do here because we have no value set
			return;
		}

		// Now make each referenced sub entity visible
		for (const auto &value : group.possibleValues)
		{
			if (value.valueId == it->second)
			{
				for (const auto &pair : value.subEntityToMaterial)
				{
					if (SubEntity *subEntity = m_entity->GetSubEntity(pair.first))
					{
						MaterialPtr material = MaterialManager::Get().Load(pair.second);
						if (material)
						{
							subEntity->SetMaterial(material);
						}
					}
				}
			}
		}
	}

	void GameUnitC::Apply(const ScalarParameterPropertyGroup &group, const AvatarConfiguration &configuration)
	{
	}

	void GameUnitC::SetCollisionVisibility(bool show)
	{
		if (show)
		{
			if (!m_capsuleDebugObject)
			{
				CreateCapsuleDebugVisualization();
			}
			if (m_capsuleDebugObject)
			{
				m_capsuleDebugObject->SetVisible(true);
			}
		}
		else
		{
			if (m_capsuleDebugObject)
			{
				m_capsuleDebugObject->SetVisible(false);
			}
		}
	}

	void GameUnitC::CreateCapsuleDebugVisualization()
	{
		if (m_capsuleDebugObject || !m_sceneNode)
		{
			return;
		}

		// Get the unit's collision capsule
		const Capsule &capsule = GetCollider();

		// Create manual render object
		static uint64 s_counter = 0;
		m_capsuleDebugObject = m_scene.CreateManualRenderObject(m_entity->GetName() + "_DEBUGCAPSULE_" + std::to_string(s_counter++));
		m_capsuleDebugObject->SetCastShadows(false);
		m_capsuleDebugObject->SetQueryFlags(0);

		{
			// Set up material for wireframe rendering
			auto lineOp = m_capsuleDebugObject->AddLineListOperation(MaterialManager::Get().Load("Models/Engine/ColorDebug.hmat"));

			// Capsule parameters
			const float radius = capsule.GetRadius();
			const Vector3 bottomHemisphereCenter = capsule.GetPointA(); // Center of bottom hemisphere
			const Vector3 topHemisphereCenter = capsule.GetPointB();	// Center of top hemisphere

			// Number of segments for circles and hemispheres
			constexpr int segments = 8;
			constexpr int hemisphereRings = 4;
			const float angleStep = (2.0f * Pi) / segments;
			const float ringStep = (Pi * 0.5f) / hemisphereRings;

			// Draw the cylindrical body (vertical lines)
			for (int i = 0; i < segments; ++i)
			{
				const float angle = i * angleStep;
				const Vector3 topPoint = topHemisphereCenter + Vector3(std::cos(angle) * radius, 0, std::sin(angle) * radius);
				const Vector3 bottomPoint = bottomHemisphereCenter + Vector3(std::cos(angle) * radius, 0, std::sin(angle) * radius);

				lineOp->AddLine(topPoint, bottomPoint).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
			}

			// Draw horizontal circles at top and bottom of cylinder
			for (int i = 0; i < segments; ++i)
			{
				const float angle1 = i * angleStep;
				const float angle2 = ((i + 1) % segments) * angleStep;

				// Top circle (at top hemisphere center level)
				const Vector3 topP1 = topHemisphereCenter + Vector3(std::cos(angle1) * radius, 0, std::sin(angle1) * radius);
				const Vector3 topP2 = topHemisphereCenter + Vector3(std::cos(angle2) * radius, 0, std::sin(angle2) * radius);

				lineOp->AddLine(topP1, topP2).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));

				// Bottom circle (at bottom hemisphere center level)
				const Vector3 bottomP1 = bottomHemisphereCenter + Vector3(std::cos(angle1) * radius, 0, std::sin(angle1) * radius);
				const Vector3 bottomP2 = bottomHemisphereCenter + Vector3(std::cos(angle2) * radius, 0, std::sin(angle2) * radius);

				lineOp->AddLine(bottomP1, bottomP2).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
			}

			// Draw top hemisphere
			for (int ring = 0; ring < hemisphereRings; ++ring)
			{
				const float ringAngle1 = ring * ringStep;
				const float ringAngle2 = (ring + 1) * ringStep;

				const float y1 = std::sin(ringAngle1) * radius;
				const float y2 = std::sin(ringAngle2) * radius;
				const float ringRadius1 = std::cos(ringAngle1) * radius;
				const float ringRadius2 = std::cos(ringAngle2) * radius;

				for (int i = 0; i < segments; ++i)
				{
					const float angle = i * angleStep;
					const float nextAngle = ((i + 1) % segments) * angleStep;

					// Horizontal lines for this ring
					const Vector3 p1 = topHemisphereCenter + Vector3(std::cos(angle) * ringRadius1, y1, std::sin(angle) * ringRadius1);
					const Vector3 p2 = topHemisphereCenter + Vector3(std::cos(nextAngle) * ringRadius1, y1, std::sin(nextAngle) * ringRadius1);

					lineOp->AddLine(p1, p2).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));

					// Vertical lines connecting rings
					if (ring < hemisphereRings - 1)
					{
						const Vector3 p3 = topHemisphereCenter + Vector3(std::cos(angle) * ringRadius2, y2, std::sin(angle) * ringRadius2);

						lineOp->AddLine(p1, p3).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
					}
				}
			}

			// Draw bottom hemisphere
			for (int ring = 0; ring < hemisphereRings; ++ring)
			{
				const float ringAngle1 = ring * ringStep;
				const float ringAngle2 = (ring + 1) * ringStep;

				const float y1 = -std::sin(ringAngle1) * radius;
				const float y2 = -std::sin(ringAngle2) * radius;
				const float ringRadius1 = std::cos(ringAngle1) * radius;
				const float ringRadius2 = std::cos(ringAngle2) * radius;

				for (int i = 0; i < segments; ++i)
				{
					const float angle = i * angleStep;
					const float nextAngle = ((i + 1) % segments) * angleStep;

					// Horizontal lines for this ring
					const Vector3 p1 = bottomHemisphereCenter + Vector3(std::cos(angle) * ringRadius1, y1, std::sin(angle) * ringRadius1);
					const Vector3 p2 = bottomHemisphereCenter + Vector3(std::cos(nextAngle) * ringRadius1, y1, std::sin(nextAngle) * ringRadius1);

					lineOp->AddLine(p1, p2).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));

					// Vertical lines connecting rings
					if (ring < hemisphereRings - 1)
					{
						const Vector3 p3 = bottomHemisphereCenter + Vector3(std::cos(angle) * ringRadius2, y2, std::sin(angle) * ringRadius2);

						lineOp->AddLine(p1, p3).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
					}
				}
			}

			// Draw meridian lines (vertical arcs from top to bottom through the hemispheres)
			for (int i = 0; i < segments; i += 4) // Only draw every 4th meridian to avoid clutter
			{
				const float angle = i * angleStep;

				// Top hemisphere meridian
				for (int ring = 0; ring < hemisphereRings; ++ring)
				{
					const float ringAngle1 = ring * ringStep;
					const float ringAngle2 = (ring + 1) * ringStep;

					const Vector3 p1 = topHemisphereCenter + Vector3(
																 std::cos(angle) * std::cos(ringAngle1) * radius,
																 std::sin(ringAngle1) * radius,
																 std::sin(angle) * std::cos(ringAngle1) * radius);
					const Vector3 p2 = topHemisphereCenter + Vector3(
																 std::cos(angle) * std::cos(ringAngle2) * radius,
																 std::sin(ringAngle2) * radius,
																 std::sin(angle) * std::cos(ringAngle2) * radius);

					lineOp->AddLine(p1, p2).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
				}

				// Bottom hemisphere meridian
				for (int ring = 0; ring < hemisphereRings; ++ring)
				{
					const float ringAngle1 = ring * ringStep;
					const float ringAngle2 = (ring + 1) * ringStep;

					const Vector3 p1 = bottomHemisphereCenter + Vector3(
																	std::cos(angle) * std::cos(ringAngle1) * radius,
																	-std::sin(ringAngle1) * radius,
																	std::sin(angle) * std::cos(ringAngle1) * radius);
					const Vector3 p2 = bottomHemisphereCenter + Vector3(
																	std::cos(angle) * std::cos(ringAngle2) * radius,
																	-std::sin(ringAngle2) * radius,
																	std::sin(angle) * std::cos(ringAngle2) * radius);

					lineOp->AddLine(p1, p2).SetColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
				}
			}
		}

		// Attach to the unit's scene node
		m_sceneNode->AttachObject(*m_capsuleDebugObject);
		m_capsuleDebugObject->SetVisible(true);
	}

	void GameUnitC::DestroyCapsuleDebugVisualization()
	{
		if (!m_capsuleDebugObject)
		{
			return;
		}

		m_scene.DestroyManualRenderObject(*m_capsuleDebugObject);
		m_capsuleDebugObject = nullptr;
	}

	void GameUnitC::SetSelectionHighlight(const bool isTarget)
	{
		if (m_isSelectionTarget == isTarget)
		{
			return;
		}

		m_isSelectionTarget = isTarget;
		RefreshSelectionRing();
	}

	void GameUnitC::SetHoverHighlight(const bool isHovered)
	{
		if (m_isSelectionHovered == isHovered)
		{
			return;
		}

		m_isSelectionHovered = isHovered;
		RefreshSelectionRing();
	}

	void GameUnitC::RefreshSelectionRing()
	{
		if (!m_sceneNode)
		{
			return;
		}

		// The target ring takes precedence over the hover ring when a unit is both.
		const int8 desiredStyle = m_isSelectionTarget ? 1 : (m_isSelectionHovered ? 0 : -1);
		if (desiredStyle == m_selectionRingStyle)
		{
			return;
		}
		m_selectionRingStyle = desiredStyle;

		// Rebuild from scratch: tear down any existing ring first.
		if (m_selectionRing)
		{
			m_scene.DestroyManualRenderObject(*m_selectionRing);
			m_selectionRing = nullptr;
		}

		if (desiredStyle < 0)
		{
			return;
		}

		// Reaction-based color matching the nameplate scheme: friendly green, hostile red,
		// otherwise neutral yellow.
		Color ringColor(0.95f, 0.85f, 0.15f, 1.0f);
		if (IsFriendly())
		{
			ringColor = Color(0.15f, 0.9f, 0.2f, 1.0f);
		}
		else if (IsHostile())
		{
			ringColor = Color(0.95f, 0.15f, 0.15f, 1.0f);
		}

		// Footprint radius from the collision capsule, clamped so tiny colliders still get a
		// visible ring. Lifted slightly above the ground to avoid z-fighting with terrain.
		const float colliderRadius = GetCollider().GetRadius();
		const float baseRadius = colliderRadius < 0.3f ? 0.3f : colliderRadius;
		constexpr float groundOffset = 0.12f;
		constexpr int segments = 32;

		static uint64 s_counter = 0;
		m_selectionRing = m_scene.CreateManualRenderObject("UnitSelRing_" + std::to_string(s_counter++));
		m_selectionRing->SetCastShadows(false);
		m_selectionRing->SetQueryFlags(0);

		{
			auto lineOp = m_selectionRing->AddLineListOperation(MaterialManager::Get().Load("Models/Engine/ColorDebug.hmat"));

			const float angleStep = (2.0f * Pi) / static_cast<float>(segments);
			auto appendCircle = [&](const float radius, const Color& color)
			{
				for (int i = 0; i < segments; ++i)
				{
					const float a1 = i * angleStep;
					const float a2 = ((i + 1) % segments) * angleStep;
					const Vector3 p1(std::cos(a1) * radius, groundOffset, std::sin(a1) * radius);
					const Vector3 p2(std::cos(a2) * radius, groundOffset, std::sin(a2) * radius);
					lineOp->AddLine(p1, p2).SetColor(color.GetABGR());
				}
			};

			if (desiredStyle == 1)
			{
				// Target: bold, reaction-colored double ring.
				appendCircle(baseRadius * 1.15f, ringColor);
				appendCircle(baseRadius * 1.32f, ringColor);
			}
			else
			{
				// Hover: single, dimmed ring.
				const Color dim(ringColor.GetRed() * 0.55f, ringColor.GetGreen() * 0.55f, ringColor.GetBlue() * 0.55f, 1.0f);
				appendCircle(baseRadius * 1.22f, dim);
			}
		}

		m_sceneNode->AttachObject(*m_selectionRing);
		m_selectionRing->SetVisible(true);
	}

	void GameUnitC::SetUnitNameVisible(const bool show)
	{
		m_questOffset = GetDefaultQuestGiverOffset();
		if (show)
		{
			m_questOffset.y += (m_nameComponent->GetBoundingBox().GetSize().y + 0.1f);
		}

		if (m_nameComponent)
		{
			m_nameComponent->SetVisible(show);
		}
	}

	const proto_client::SpellEntry *GameUnitC::GetOpenSpell(const GameWorldObjectC* target) const
	{
		const uint32 objectLockTypeId = target ? target->Get<uint32>(object_fields::LockEntry) : 0u;

		for (const auto *spell : m_spells)
		{
			for (const auto &effect : spell->effects())
			{
				if (effect.type() == spell_effects::OpenLock)
				{
					// Symmetric lock matching (mirrors the server's HandleOpenLock): the spell's
					// lock type in miscvaluea must equal the object's lock type, with 0 meaning
					// "generic open spell" / "unlocked object". A lock-specific spell (e.g. the
					// instant door opener) must never be picked for unlocked chests.
					if (static_cast<uint32>(effect.miscvaluea()) == objectLockTypeId)
					{
						return spell;
					}
				}
			}
		}

		return nullptr;
	}

	void GameUnitC::OnLanded()
	{
		// Play the landing animation and re-arm the jump start clip.
		m_animationController->OnLanded();

		m_movementInfo.position = m_sceneNode->GetDerivedPosition();
		m_movementInfo.facing = GetSceneNode()->GetOrientation().GetYaw();
		m_movementInfo.movementFlags &= ~movement_flags::Falling;
		m_movementInfo.timestamp = GetAsyncTimeMs();
	}

	void GameUnitC::OnStartFalling()
	{
		const bool wasFalling = (m_movementInfo.movementFlags & movement_flags::Falling) != 0;

		UpdateMovementInfo();
		m_movementInfo.movementFlags |= movement_flags::Falling;

		if (!wasFalling)
		{
			m_netDriver.OnMoveEvent(*this, MovementEvent(movement_event_type::Fall, m_movementInfo.timestamp, m_movementInfo));
		}
	}

	void GameUnitC::OnStartSwimming()
	{
		const bool wasSwimming = (m_movementInfo.movementFlags & movement_flags::Swimming) != 0;

		UpdateMovementInfo();
		m_movementInfo.movementFlags |= movement_flags::Swimming;
		// Swimming and falling are mutually exclusive movement states.
		m_movementInfo.movementFlags &= ~movement_flags::Falling;

		if (!wasSwimming)
		{
			m_netDriver.OnMoveEvent(*this, MovementEvent(movement_event_type::StartSwim, m_movementInfo.timestamp, m_movementInfo));
		}
	}

	void GameUnitC::OnStopSwimming()
	{
		const bool wasSwimming = (m_movementInfo.movementFlags & movement_flags::Swimming) != 0;

		UpdateMovementInfo();
		m_movementInfo.movementFlags &= ~movement_flags::Swimming;
		// Pitch and the swim-up flag are only meaningful while swimming; reset them on exit.
		m_movementInfo.movementFlags &= ~movement_flags::Ascending;
		m_movementInfo.pitch = Radian(0.0f);

		if (wasSwimming)
		{
			m_netDriver.OnMoveEvent(*this, MovementEvent(movement_event_type::StopSwim, m_movementInfo.timestamp, m_movementInfo));
		}
	}

	bool GameUnitC::QueryWaterAt(const float x, const float z, float& outSurfaceY) const
	{
		return m_netDriver.QueryWaterAt(x, z, outSurfaceY);
	}

	void GameUnitC::UpdateSwimMeshPitch(const float deltaTime)
	{
		if (!m_entityOffsetNode || !IsPlayer())
		{
			return;
		}

		// The mesh's base alignment (see GameObjectC::SetupSceneObjects) is a 90° yaw about Y.
		Quaternion baseOffset;
		baseOffset.FromAngleAxis(Degree(90.0f), Vector3::UnitY);

		// The mesh only shows the dive angle while actively swimming forward/backward. When idle in
		// the water the control pitch is preserved (so the player can pre-aim a dive), but the mesh
		// is displayed level; it tilts once movement starts and returns to level when it stops.
		const bool swimmingAndMoving = m_movementInfo.IsSwimming() &&
			(m_movementInfo.movementFlags & (movement_flags::Forward | movement_flags::Backward)) != 0;
		const float targetPitch = swimmingAndMoving ? m_movementInfo.pitch.GetValueRadians() : 0.0f;

		// Smoothly interpolate the displayed pitch toward the target. Exponential blending keeps the
		// transition frame-rate independent and visually smooth.
		constexpr float pitchBlendRate = 8.0f;
		const float blend = 1.0f - std::exp(-pitchBlendRate * std::max(deltaTime, 0.0f));
		m_swimMeshPitch += (targetPitch - m_swimMeshPitch) * blend;

		if (std::abs(m_swimMeshPitch) > 0.001f)
		{
			// Pitch the mesh about the body's local right axis (Z) — the same axis the swim physics
			// uses for the dive direction, so the mesh visibly points where the character swims.
			m_entityOffsetNode->SetOrientation(Quaternion(Radian(m_swimMeshPitch), Vector3::UnitZ) * baseOffset);
		}
		else if (!m_entityOffsetNode->GetOrientation().Equals(baseOffset, Radian(0.001f)))
		{
			m_swimMeshPitch = 0.0f;
			m_entityOffsetNode->SetOrientation(baseOffset);
		}
	}

	void GameUnitC::AddSpellTint(uint32 spellId, const Vector4& tintColor)
	{
		// Add or update the tint for this spell
		m_spellTints[spellId] = tintColor;
		
		// Ensure we have MaterialInstances for tinting (creates them if first tint)
		if (m_tintMaterialStates.empty())
		{
			EnsureMaterialInstances();
		}
		
		// Update all SubEntity materials with the new blended tint
		UpdateTintOnMaterials();
	}

	void GameUnitC::RemoveSpellTint(uint32 spellId)
	{
		// Remove the tint for this spell
		auto it = m_spellTints.find(spellId);
		if (it != m_spellTints.end())
		{
			m_spellTints.erase(it);
			
			// If no more tints, restore original shared materials
			if (m_spellTints.empty())
			{
				RestoreOriginalMaterials();
			}
			else
			{
				// Update all SubEntity materials with the recalculated tint
				UpdateTintOnMaterials();
			}
		}
	}

	Vector4 GameUnitC::GetBlendedTint() const
	{
		if (m_spellTints.empty())
		{
			// Default black (no emissive glow)
			return Vector4(0.0f, 0.0f, 0.0f, 1.0f);
		}
		
		// Start with black (no emissive glow)
		Vector4 result(0.0f, 0.0f, 0.0f, 1.0f);
		
		// Blend all active tints using additive blending (emissive colors add up)
		for (const auto& pair : m_spellTints)
		{
			const Vector4& tint = pair.second;
			
			// Add the tint color scaled by its alpha (alpha controls tint strength)
			result.x += tint.x * tint.w;
			result.y += tint.y * tint.w;
			result.z += tint.z * tint.w;
		}

		if (m_spellTints.size() > 1)
		{
			result /= static_cast<float>(m_spellTints.size()); // Average the tints
		}
		
		
		// Clamp to [0, 1] range
		result.x = std::min(result.x, 1.0f);
		result.y = std::min(result.y, 1.0f);
		result.z = std::min(result.z, 1.0f);
		
		return result;
	}

	void GameUnitC::EnsureMaterialInstances()
	{
		if (!m_entity)
		{
			return;
		}
		
		// Create MaterialInstances for all visible SubEntities
		const uint32 subEntityCount = m_entity->GetNumSubEntities();
		for (uint32 i = 0; i < subEntityCount; ++i)
		{
			SubEntity* subEntity = m_entity->GetSubEntity(static_cast<uint16>(i));
			if (!subEntity || !subEntity->IsVisible())
			{
				continue;
			}
			
			MaterialPtr currentMaterial = subEntity->GetMaterial();
			if (!currentMaterial)
			{
				continue;
			}
			
			// Create a MaterialInstance for this unit's SubEntity
			const std::string instanceName = std::string(m_entity->GetName()) + "_TintInstance_" + std::to_string(i);
			auto tintInstance = std::make_shared<MaterialInstance>(instanceName, currentMaterial);
			
			// Store original material and new instance
			SubEntityMaterialState state;
			state.originalMaterial = currentMaterial;
			state.tintInstance = tintInstance;
			m_tintMaterialStates[subEntity] = state;
			
			// Apply the MaterialInstance to the SubEntity (cast to MaterialPtr)
			MaterialPtr materialPtr = std::static_pointer_cast<MaterialInterface>(tintInstance);
			subEntity->SetMaterial(materialPtr);
		}
	}

	void GameUnitC::RestoreOriginalMaterials()
	{
		if (!m_entity)
		{
			return;
		}
		
		// Restore original shared materials for all SubEntities
		for (const auto& pair : m_tintMaterialStates)
		{
			SubEntity* subEntity = pair.first;
			const SubEntityMaterialState& state = pair.second;
			
			if (subEntity && state.originalMaterial)
			{
				subEntity->SetMaterial(state.originalMaterial);
			}
		}
		
		// Clear the material state tracking
		m_tintMaterialStates.clear();
	}

	void GameUnitC::UpdateTintOnMaterials()
	{
		if (!m_entity)
		{
			return;
		}
		
		const Vector4 blendedTint = GetBlendedTint();
		
		// Update tint parameter on all MaterialInstances
		for (const auto& pair : m_tintMaterialStates)
		{
			const SubEntityMaterialState& state = pair.second;
			
			if (state.tintInstance)
			{
				// Set the tint parameter on this unit's MaterialInstance
				state.tintInstance->SetVectorParameter("Tint", blendedTint);
			}
		}
	}

	void GameUnitC::SetSpellMod(const uint8 type, const uint8 effectIndex, const uint8 op, const int32 value)
	{
		const uint32 key = (static_cast<uint32>(type) << 16) | (static_cast<uint32>(effectIndex) << 8) | static_cast<uint32>(op);
		if (value == 0)
		{
			m_spellMods.erase(key);
		}
		else
		{
			m_spellMods[key] = value;
		}
	}

	int32 GameUnitC::GetSpellModFlatForFlags(const uint8 op, const uint64 familyFlags) const
	{
		if (familyFlags == 0)
		{
			return 0;
		}

		int32 total = 0;
		for (uint8 eff = 0; eff < 64; ++eff)
		{
			if (!(familyFlags & (static_cast<uint64>(1) << eff)))
			{
				continue;
			}

			// type 0 = Flat
			const uint32 key = (static_cast<uint32>(0) << 16) | (static_cast<uint32>(eff) << 8) | static_cast<uint32>(op);
			const auto it = m_spellMods.find(key);
			if (it != m_spellMods.end())
			{
				total += it->second;
			}
		}
		return total;
	}

	int32 GameUnitC::GetSpellModPctForFlags(const uint8 op, const uint64 familyFlags) const
	{
		if (familyFlags == 0)
		{
			return 0;
		}

		int32 total = 0;
		for (uint8 eff = 0; eff < 64; ++eff)
		{
			if (!(familyFlags & (static_cast<uint64>(1) << eff)))
			{
				continue;
			}

			// type 1 = Pct
			const uint32 key = (static_cast<uint32>(1) << 16) | (static_cast<uint32>(eff) << 8) | static_cast<uint32>(op);
			const auto it = m_spellMods.find(key);
			if (it != m_spellMods.end())
			{
				total += it->second;
			}
		}
		return total;
	}

}
