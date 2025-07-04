#include "game_unit_c.h"

#include <algorithm>

#include "net_client.h"
#include "object_mgr.h"
#include "base/clock.h"
#include "client_data/project.h"
#include "frame_ui/font_mgr.h"
#include "game/spell.h"
#include "game/character_customization/avatar_definition_mgr.h"
#include "log/default_log_levels.h"
#include "math/collision.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/scene.h"
#include "shared/client_data/proto_client/factions.pb.h"
#include "shared/client_data/proto_client/faction_templates.pb.h"
#include "shared/client_data/proto_client/model_data.pb.h"

namespace mmo
{
	GameAuraC::GameAuraC(GameUnitC& owner, const proto_client::SpellEntry& spell, const uint64 caster, const GameTime expiration)
		: m_spell(&spell)
		, m_expiration(0)
		, m_casterId(caster)
		, m_targetId(owner.GetGuid())
	{
		if (expiration > 0)
		{
			m_expiration = GetAsyncTimeMs() + expiration;
		}

		m_onOwnerRemoved = owner.removed.connect([this]() { removed(); });
	}

	GameAuraC::~GameAuraC()
	{
		removed();
	}

	bool GameAuraC::IsExpired() const
	{
		if (!CanExpire())
		{
			return false;
		}

		return GetAsyncTimeMs() >= m_expiration;
	}

	GameUnitC::~GameUnitC()
	{
		// Ensure quest giver status is removed
		SetQuestgiverStatus(questgiver_status::None);
	}

	void GameUnitC::Deserialize(io::Reader& reader, const bool complete)
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

		m_fieldMap.MarkAllAsUnchanged();

		reader
			>> io::read<float>(m_unitSpeed[movement_type::Walk])
			>> io::read<float>(m_unitSpeed[movement_type::Run])
			>> io::read<float>(m_unitSpeed[movement_type::Backwards])
			>> io::read<float>(m_unitSpeed[movement_type::Swim])
			>> io::read<float>(m_unitSpeed[movement_type::SwimBackwards])
			>> io::read<float>(m_unitSpeed[movement_type::Flight])
			>> io::read<float>(m_unitSpeed[movement_type::FlightBackwards])
			>> io::read<float>(m_unitSpeed[movement_type::Turn]);

		ASSERT(GetGuid() > 0);
		if (complete)
		{
			SetupSceneObjects();
		}

		if (updateFlags & object_update_flags::HasMovementInfo)
		{
			m_sceneNode->SetDerivedPosition(m_movementInfo.position);
			m_sceneNode->SetDerivedOrientation(Quaternion(m_movementInfo.facing, Vector3::UnitY));
		}
	}

	void GameUnitC::Update(const float deltaTime)
	{
		GameObjectC::Update(deltaTime);

		UpdateQuestGiverAndNameVisuals(deltaTime);

		const bool isDead = (GetHealth() <= 0) || GetStandState() == unit_stand_state::Dead;
		
		if (m_movementAnimation)
		{
			UpdateMovementAnimation(deltaTime, isDead);
		}
		else if (!isDead)
		{
			UpdateNormalMovement(deltaTime);
		}

		UpdateAnimationStates(deltaTime, isDead);
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
			Camera* cam = m_scene.GetCamera((uint32)0);
			if (cam)
			{
				m_questGiverNode->SetFixedYawAxis(true);
				m_questGiverNode->LookAt(cam->GetDerivedPosition(), TransformSpace::World, Vector3::NegativeUnitZ);
			}
		}

		// Update name component to face camera
		if (m_nameComponent && m_nameComponent->IsVisible())
		{
			Camera* cam = m_scene.GetCamera((uint32)0);
			if (cam)
			{
				m_nameComponentNode->SetFixedYawAxis(true);
				m_nameComponentNode->LookAt(cam->GetDerivedPosition(), TransformSpace::World, Vector3::UnitZ);
			}
		}
	}
	
	void GameUnitC::UpdateMovementAnimation(const float deltaTime, const bool isDead)
	{
		bool animationFinished = false;
		if (!isDead)
		{
			SetTargetAnimState(m_casting ? m_castingState : m_runAnimState);
		}

		m_movementAnimationTime += deltaTime;
		if (m_movementAnimationTime >= m_movementAnimation->GetDuration())
		{
			m_movementAnimationTime = m_movementAnimation->GetDuration();
			animationFinished = true;
		}

		m_sceneNode->SetPosition(m_movementStart);
		m_sceneNode->SetOrientation(m_movementStartRot);
		m_movementAnimation->Apply(m_movementAnimationTime);

		// Continuously adjust height to terrain to prevent floating
		AdjustHeightToTerrain(deltaTime);

		// Update movement info
		m_movementInfo.position = m_sceneNode->GetDerivedPosition();
		m_movementInfo.movementFlags = 0;

		if (animationFinished)
		{
			FinishMovementAnimation(isDead);
		}
	}
	
	void GameUnitC::FinishMovementAnimation(const bool isDead)
	{
		if (!isDead)
		{
			const bool isAttacking = (Get<uint32>(object_fields::Flags) & unit_flags::Attacking) != 0;
			AnimationState* idleAnim = isAttacking ? m_readyAnimState : m_idleAnimState;
			SetTargetAnimState(m_casting ? m_castingState : idleAnim);
		}
		
		// Final height adjustment at the end of path
		Vector3 endPosition = m_movementEnd;
		float groundHeight = 0.0f;
		if (GetCollisionProvider().GetHeightAt(endPosition + Vector3::UnitY * 0.5f, 3.5f, groundHeight))
		{
			endPosition.y = groundHeight;
		}

		m_sceneNode->SetDerivedPosition(endPosition);

		// Reset movement info
		m_movementInfo.position = endPosition;
		m_movementInfo.timestamp = GetAsyncTimeMs();
		m_movementInfo.movementFlags = 0;

		movementEnded(*this, m_movementInfo);

		// End animation
		m_movementAnimation.reset();
		m_movementAnimationTime = 0.0f;
	}
	
	void GameUnitC::AdjustHeightToTerrain(const float deltaTime)
	{
		float groundHeight = 0.0f;
		float rayHeight = 0.5f; // Higher ray starting point to detect both terrain and geometry
		const bool hasGroundHeight = GetCollisionProvider().GetHeightAt(m_sceneNode->GetDerivedPosition() + Vector3::UnitY * rayHeight, 3.5f, groundHeight);
		
		// If ground found and we're slightly above it (floating), adjust position
		if (hasGroundHeight && (m_sceneNode->GetDerivedPosition().y > groundHeight + 0.05f || 
								m_sceneNode->GetDerivedPosition().y < groundHeight - 0.01f))
		{
			// Use a smooth adjustment when not too far from ground
			float heightDiff = groundHeight - m_sceneNode->GetDerivedPosition().y;
			if (std::abs(heightDiff) < 1.0f)
			{
				// Faster adjustment when farther from ground, but not instant to avoid popping
				float adjustSpeed = std::min(5.0f * deltaTime, 1.0f) * std::min(std::abs(heightDiff) * 2.0f, 1.0f);
				Vector3 newPos = m_sceneNode->GetDerivedPosition();
				newPos.y += heightDiff * adjustSpeed;
				m_sceneNode->SetPosition(newPos);
			}
			else
			{
				// If too far from ground (likely a significant terrain change), snap directly
				m_sceneNode->SetPosition(Vector3(m_sceneNode->GetDerivedPosition().x, groundHeight, m_sceneNode->GetDerivedPosition().z));
			}
		}
	}
	
	void GameUnitC::UpdateNormalMovement(const float deltaTime)
	{
		//ApplyLocalMovement(deltaTime);

		// Update target tracking for NPCs
		UpdateTargetTracking();

		// Update animation state based on movement
		UpdateMovementBasedAnimation();
	}
	
	void GameUnitC::UpdateTargetTracking()
	{
		// Check if we should track a target
		if (auto target = m_targetUnit.lock(); !IsPlayer() && target)
		{
			// Only rotate around the Y axis (yaw) to face the target
			// Calculate the direction vector to the target in the horizontal plane
			Vector3 targetPos = target->GetSceneNode()->GetDerivedPosition();
			Vector3 myPos = GetSceneNode()->GetDerivedPosition();
			
			// Project positions to the XZ plane (ignore Y component)
			targetPos.y = myPos.y;
			
			// Calculate the angle to face the target
			Radian yawToTarget = GetAngle(myPos.x, myPos.z, targetPos.x, targetPos.z);
			
			// Set orientation using only the yaw component
			GetSceneNode()->SetOrientation(Quaternion(yawToTarget, Vector3::UnitY));
		}
	}
	
	void GameUnitC::UpdateMovementBasedAnimation()
	{
		// Handle jumping animations first
		if (m_movementInfo.movementFlags & movement_flags::Falling)
		{
			// If the jumping velocity is positive, we're in the jump up phase
			if (m_movementInfo.jumpVelocity > 0.0f)
			{
				// If we have a jump start animation, use it. Otherwise, use falling
				if (m_jumpStartState && !m_jumpStartState->HasEnded())
				{
					SetTargetAnimState(m_jumpStartState);
				}
				else if (m_fallingState)
				{
					SetTargetAnimState(m_fallingState);
				}
			}
			// If velocity is negative, we're falling
			else if (m_fallingState)
			{
				SetTargetAnimState(m_fallingState);
			}
			return;
		}
		
		// Regular movement animations
		if (m_movementInfo.IsMoving())
		{
			SetTargetAnimState(m_casting ? m_castingState : m_runAnimState);
		}
		else
		{
			const bool isAttacking = (Get<uint32>(object_fields::Flags) & unit_flags::Attacking) != 0;
			AnimationState* idleAnim = isAttacking ? m_readyAnimState : m_idleAnimState;

			SetTargetAnimState(m_casting ? m_castingState : idleAnim);
		}
	}
	
	void GameUnitC::UpdateAnimationStates(const float deltaTime, const bool isDead)
	{
		// Handle one-shot animations
		if (m_oneShotState)
		{
			UpdateOneShotAnimation(deltaTime);
		}

		// Control animation state visibility based on one-shot state
		if (m_currentState != nullptr)
		{
			m_currentState->SetEnabled(m_oneShotState == nullptr || m_oneShotState->HasEnded());
		}
		if (m_targetState != nullptr)
		{
			m_targetState->SetEnabled(m_oneShotState == nullptr || m_oneShotState->HasEnded());
		}

		// Always force dead state
		if (isDead)
		{
			if (m_oneShotState && m_oneShotState->IsEnabled())
			{
				m_oneShotState->SetTimePosition(m_oneShotState->GetLength());
			}

			SetTargetAnimState(m_deathState);
		}

		// Handle animation transitions
		UpdateAnimationTransitions(deltaTime);

		// Update animation time positions
		AdvanceAnimationTimes(deltaTime);
	}
	
	void GameUnitC::UpdateOneShotAnimation(const float deltaTime)
	{
		// Hide regular animations while one-shot is playing
		if (m_currentState) m_currentState->SetWeight(0.0f);
		if (m_targetState) m_targetState->SetWeight(0.0f);

		// Handle transition back after one-shot animation has ended
		if (m_oneShotState->HasEnded())
		{
			// Transition back to current state
			m_oneShotState->SetWeight(m_oneShotState->GetWeight() - deltaTime * 4.0f);
			if (m_targetState)
			{
				m_targetState->SetWeight(1.0f - m_oneShotState->GetWeight());
			}
			else if (m_currentState)
			{
				m_currentState->SetWeight(1.0f - m_oneShotState->GetWeight());
			}

			// Once transition is complete, disable one-shot animation
			if (m_oneShotState->GetWeight() <= 0.0f)
			{
				if (m_targetState)
				{
					m_targetState->SetWeight(1.0f);
				}
				else if (m_currentState)
				{
					m_currentState->SetWeight(1.0f);
				}

				m_oneShotState->SetEnabled(false);
				m_oneShotState->SetWeight(0.0f);
				m_oneShotState = nullptr;
			}
		}
	}
	
	void GameUnitC::UpdateAnimationTransitions(const float deltaTime)
	{
		// Skip transitions if one-shot animation is playing
		if (m_oneShotState)
		{
			return;
		}

		// Handle transition to target state
		if (m_targetState != m_currentState)
		{
			// If we have a target but no current state, just set target as current
			if (m_targetState && !m_currentState)
			{
				m_currentState = m_targetState;
				m_targetState = nullptr;

				m_currentState->SetWeight(1.0f);
				m_currentState->SetEnabled(true);
			}
		}

		// Handle crossfade between current and target states
		if (m_currentState && m_targetState)
		{
			m_targetState->SetWeight(m_targetState->GetWeight() + deltaTime * 4.0f);
			m_currentState->SetWeight(1.0f - m_targetState->GetWeight());

			// Once transition is complete, make target the new current state
			if (m_targetState->GetWeight() >= 1.0f)
			{
				m_currentState->SetWeight(0.0f);
				m_currentState->SetEnabled(false);

				m_currentState = m_targetState;
				m_targetState = nullptr;
			}
		}
	}
	
	void GameUnitC::AdvanceAnimationTimes(const float deltaTime)
	{
		// Update animation states
		if (m_currentState && m_currentState->IsEnabled())
		{
			m_currentState->AddTime(deltaTime);
		}
		if (m_targetState && m_targetState->IsEnabled())
		{
			m_targetState->AddTime(deltaTime);
		}
		if (m_oneShotState && m_oneShotState->IsEnabled())
		{
			m_oneShotState->AddTime(deltaTime);
		}
	}

	void GameUnitC::ApplyLocalMovement(const float deltaTime)
	{
		auto* playerNode = GetSceneNode();

		// Define max movement per step to prevent tunneling
		constexpr float maxStepDistance = 0.1f;
		
		// Store initial position to calculate total movement at the end
		Vector3 initialFramePosition = m_movementInfo.position;

		// Handle turning regardless of falling state
		if (m_movementInfo.IsTurning())
		{
			if (m_movementInfo.movementFlags & movement_flags::TurnLeft)
			{
				playerNode->Yaw(Radian(GetSpeed(movement_type::Turn)) * deltaTime, TransformSpace::World);
			}
			else if (m_movementInfo.movementFlags & movement_flags::TurnRight)
			{
				playerNode->Yaw(Radian(-GetSpeed(movement_type::Turn)) * deltaTime, TransformSpace::World);
			}
			m_movementInfo.facing = GetSceneNode()->GetDerivedOrientation().GetYaw();
		}

		// Adjust for collision and gravity
		float groundHeight = 0.0f;
		const bool hasGroundHeight = GetCollisionProvider().GetHeightAt(m_movementInfo.position + Vector3::UnitY * 0.25f, 1.0f, groundHeight);

		if (m_movementInfo.movementFlags & movement_flags::Falling)
		{
			// Apply gravity to vertical movement with more stable terminal velocity
			constexpr float gravity = 19.291105f;
			constexpr float terminalVelocity = -55.0f; // Max falling speed
			m_movementInfo.jumpVelocity = std::max(m_movementInfo.jumpVelocity - gravity * deltaTime, terminalVelocity);

			// Calculate the total movement for this frame
			Vector3 totalMovement = Vector3(0.0f, m_movementInfo.jumpVelocity * deltaTime, 0.0f);

			// Apply horizontal movement based on jump direction
			if (m_movementInfo.jumpXZSpeed > 0.0f)
			{
				// Use jumpSinAngle and jumpCosAngle to determine the direction
				const float sinAngle = m_movementInfo.jumpSinAngle;
				const float cosAngle = m_movementInfo.jumpCosAngle;

				// Apply some air drag to horizontal movement
				m_movementInfo.jumpXZSpeed = std::max(0.0f, m_movementInfo.jumpXZSpeed - 0.5f * deltaTime);

				// Calculate horizontal movement
				totalMovement.x = cosAngle * m_movementInfo.jumpXZSpeed * deltaTime;
				totalMovement.z = sinAngle * m_movementInfo.jumpXZSpeed * deltaTime;
			}

			// Calculate the number of steps needed based on total movement (more steps for faster movement)
			float totalDistance = totalMovement.GetLength();
			int steps = std::max(1, static_cast<int>(totalDistance / maxStepDistance));
			Vector3 stepMovement = totalMovement / static_cast<float>(steps);

			// Apply movement in steps to avoid tunneling
			bool landed = false;
			for (int i = 0; i < steps && !landed; ++i)
			{
				// Apply a single step of movement
				m_movementInfo.position += stepMovement;

				// Check for collision after each step
				HandleCollision();

				// Check if we've landed
				float currentGroundHeight = 0.0f;
				bool foundGround = GetCollisionProvider().GetHeightAt(m_movementInfo.position + Vector3::UnitY * 0.25f, 1.0f, currentGroundHeight);

				if (foundGround && m_movementInfo.position.y <= currentGroundHeight && m_movementInfo.jumpVelocity <= 0.0f)
				{
					// Check the slope of the terrain we've landed on
					Vector3 groundNormal;
					bool hasNormal = GetCollisionProvider().GetGroundNormalAt(m_movementInfo.position + Vector3::UnitY * 0.55f, 1.0f, groundNormal);

					if (hasNormal)
					{
						// Calculate slope angle
						float slopeAngleDot = groundNormal.Dot(Vector3::UnitY);
						float slopeAngleRadians = std::acos(Clamp(slopeAngleDot, -1.0f, 1.0f));
						float maxSlopeRadians = 50.0f * 0.0174533f; // Convert max walkable slope to radians

						// If we've landed on a steep slope, keep the falling flag and continue sliding
						if (slopeAngleRadians > maxSlopeRadians)
						{
							// Ensure position is at ground level
							m_movementInfo.position.y = currentGroundHeight;

							// Make sure the normal is pointing upward for consistent calculation
							if (groundNormal.y < 0.0f)
							{
								groundNormal = -groundNormal;
							}

							// Calculate downhill direction
							// The downhill direction is perpendicular to the UP vector, in the direction
							// of the steepest descent along the slope
							Vector3 slopeRight = Vector3::UnitY.Cross(groundNormal);
							slopeRight.Normalize();

							// Then get the true downhill direction (perpendicular to both the normal and the right vector)
							Vector3 downhillDir = groundNormal.Cross(slopeRight);

							// If the slope is nearly horizontal, we don't need to slide
							if (!downhillDir.IsNearlyEqual(Vector3::Zero, 0.001f))
							{
								// Normalize the downhill direction
								downhillDir.Normalize();

								// We need to ensure it points downhill, not uphill
								if (downhillDir.y > 0.0f)
								{
									downhillDir = -downhillDir;
								}

								// Calculate steepness factor (0 = flat, 1 = vertical)
								float steepnessFactor = 1.0f - slopeAngleDot;  // Higher value = steeper slope

								// Physics-based sliding with acceleration based on gravity and slope angle
								// The steeper the slope, the faster the acceleration
								constexpr float baseSlideSpeed = 3.0f;
								constexpr float maxSlideSpeed = 15.0f;  
								constexpr float slideAcceleration = 5.0f; 

								// Calculate acceleration based on slope steepness
								float accelerationFactor = slideAcceleration * steepnessFactor * steepnessFactor;

								// Get current slide speed or initialize it
								float currentSpeed = m_movementInfo.jumpXZSpeed;
								currentSpeed = std::max(currentSpeed, baseSlideSpeed);

								// Apply acceleration to current speed with smoother transition
								float targetSpeed = currentSpeed + accelerationFactor * deltaTime;
								currentSpeed = std::min(targetSpeed, maxSlideSpeed);

								// Set movement for continuing to slide down the slope
								m_movementInfo.jumpXZSpeed = currentSpeed;
								m_movementInfo.jumpSinAngle = downhillDir.z;
								m_movementInfo.jumpCosAngle = downhillDir.x;

								// Update position
								playerNode->SetPosition(m_movementInfo.position);
								UpdateCollider();
								continue; // Skip stopping the fall
							}
						}
					}

					// Normal landing on walkable ground
					m_movementInfo.position.y = currentGroundHeight + 0.001f; // Slight offset to avoid z-fighting
					m_movementInfo.movementFlags &= ~movement_flags::Falling;
					m_movementInfo.jumpVelocity = 0.0f;
					m_movementInfo.jumpXZSpeed = 0.0f;
					m_movementInfo.jumpSinAngle = 0.0f;
					m_movementInfo.jumpCosAngle = 0.0f;
					playerNode->SetPosition(m_movementInfo.position);
					m_netDriver.OnMoveFallLand(*this);
					PlayLandAnimation(); // Play landing animation when touching ground
					landed = true;
					break;
				}
			}

			// Update player node position
			playerNode->SetPosition(m_movementInfo.position);
			UpdateCollider();
		}
		// Normal ground movement
		else if (m_movementInfo.IsMoving())
		{
			Vector3 movementVector;

			if (m_movementInfo.movementFlags & movement_flags::Forward)
			{
				movementVector.x += 1.0f;
			}
			if (m_movementInfo.movementFlags & movement_flags::Backward)
			{
				movementVector.x -= 1.0f;
			}
			if (m_movementInfo.movementFlags & movement_flags::StrafeLeft)
			{
				movementVector.z -= 1.0f;
			}
			if (m_movementInfo.movementFlags & movement_flags::StrafeRight)
			{
				movementVector.z += 1.0f;
			}

			if (movementVector.IsNearlyEqual(Vector3::Zero))
			{
				return;
			}

			const MovementType movementType = (movementVector.x < 0.0f) ? movement_type::Backwards : movement_type::Run;
			movementVector.Normalize();

			// Calculate total movement for this frame
			Vector3 totalMovement = movementVector * GetSpeed(movementType) * deltaTime;
			float totalDistance = totalMovement.GetLength();

			// Calculate number of steps to avoid tunneling
			int steps = std::max(1, static_cast<int>(totalDistance / maxStepDistance));
			Vector3 stepMovement = totalMovement / static_cast<float>(steps);

			// Track if movement was completely blocked to avoid micro-stuttering
			int successfulSteps = 0;

			// Apply movement in steps
			for (int i = 0; i < steps; ++i)
			{
				Vector3 initialStepPosition = m_movementInfo.position;
				Vector3 potentialPosition = m_movementInfo.position + playerNode->GetOrientation() * stepMovement;

				// Check the slope at the new position
				if (CanWalkOnSlope(potentialPosition, 50.0f))
				{
					// Apply the movement if slope is walkable
					m_movementInfo.position = potentialPosition;

					// Check for collisions and adjust position if needed
					HandleCollision();

					// If we made even slight progress, count it as a successful step
					if (!m_movementInfo.position.IsNearlyEqual(initialStepPosition, 0.0001f))
					{
						successfulSteps++;
					}
					
					// If we didn't make any progress (completely stuck against a wall), try sliding
					if (m_movementInfo.position.IsNearlyEqual(initialStepPosition, 0.0001f))
					{
						// Try sliding along the wall - find an alternative direction
						Vector3 alternateDirection = Vector3::Zero;
						
						// Try moving perpendicular to the blocked direction
						Vector3 worldStepDir = playerNode->GetOrientation() * stepMovement;
						Vector3 sideDir(worldStepDir.z, 0, -worldStepDir.x); // 90 degrees to the side
						
						// Try both sides
						Vector3 leftPosition = initialStepPosition + sideDir * 0.5f * stepMovement.GetLength();
						Vector3 rightPosition = initialStepPosition - sideDir * 0.5f * stepMovement.GetLength();
						
						// Check which side has more room
						bool canMoveLeft = CanWalkOnSlope(leftPosition, 50.0f);
						bool canMoveRight = CanWalkOnSlope(rightPosition, 50.0f);
						
						if (canMoveLeft && !canMoveRight) {
							m_movementInfo.position = leftPosition;
							HandleCollision();
						}
						else if (canMoveRight && !canMoveLeft) {
							m_movementInfo.position = rightPosition;
							HandleCollision();
						}
						else if (canMoveLeft && canMoveRight) {
							// Choose the side that has more clearance
							m_movementInfo.position = leftPosition;
							HandleCollision();
							Vector3 leftResult = m_movementInfo.position;
							
							m_movementInfo.position = rightPosition;
							HandleCollision();
							Vector3 rightResult = m_movementInfo.position;
							
							// Keep the position that moved the most
							if ((leftResult - initialStepPosition).GetLength() > 
								(rightResult - initialStepPosition).GetLength()) {
								m_movementInfo.position = leftResult;
							}
							else {
								m_movementInfo.position = rightResult;
							}
						}
						
						// If we still made no progress, stop trying further steps
						if (m_movementInfo.position.IsNearlyEqual(initialStepPosition, 0.0001f)) {
							break;
						}
					}
				}
				else
				{
					// Can't walk on this slope, try sliding along it
					HandleSlopeSliding(stepMovement);
					
					// If we made progress, count it
					if (!m_movementInfo.position.IsNearlyEqual(initialStepPosition, 0.0001f))
					{
						successfulSteps++;
					}
				}
			}

			// Update scene node position
			playerNode->SetPosition(m_movementInfo.position);
			UpdateCollider();

			// Check if we should start falling (walking off an edge)
			// Only do this check if we made some progress to avoid edge cases
			if (successfulSteps > 0)
			{
				bool noGroundBelow = !hasGroundHeight || groundHeight <= m_movementInfo.position.y - 0.25f;
				if (noGroundBelow)
				{
					const bool wasFalling = (m_movementInfo.movementFlags & movement_flags::Falling) != 0;

					// Start falling with minimal downward velocity
					m_movementInfo.movementFlags |= movement_flags::Falling;
					m_movementInfo.jumpVelocity = -0.01f;

					// Calculate horizontal movement direction based on current movement
					if (!movementVector.IsNearlyEqual(Vector3::Zero))
					{
						// Calculate the angle in world space
						const Radian facing = m_movementInfo.facing;
						const float sinFacing = std::sin(facing.GetValueRadians());
						const float cosFacing = std::cos(facing.GetValueRadians());

						// Store the sin and cos of the jump angle
						m_movementInfo.jumpSinAngle = movementVector.z * cosFacing - movementVector.x * sinFacing;
						m_movementInfo.jumpCosAngle = movementVector.x * cosFacing + movementVector.z * sinFacing;

						// Set jump speed to current movement speed
						m_movementInfo.jumpXZSpeed = GetSpeed(movementType);
					}

					if (!wasFalling)
					{
						m_netDriver.OnMoveFall(*this);
					}
				}
				else if (hasGroundHeight)
				{
					m_movementInfo.position.y = groundHeight + 0.001f;
					playerNode->SetPosition(m_movementInfo.position);
					UpdateCollider();
				}
			}
		}
	}

	bool GameUnitC::CanWalkOnSlope(const Vector3& position, float maxSlopeDegrees) const
	{
		Vector3 groundNormal;

		if (GetCollisionProvider().GetGroundNormalAt(position + Vector3::UnitY * 0.55f, 1.0f, groundNormal))
		{
			// Compute the angle between the ground normal and the up vector.
			// Dot product gives cos(angle) so acos(dot) is the angle in radians.
			float upDot = groundNormal.Dot(Vector3::UnitY);
			float angleRadians = std::acos(Clamp(upDot, -1.0f, 1.0f));
			// Convert maximum slope from degrees to radians.
			float maxSlopeRadians = maxSlopeDegrees * 0.0174533f;

			// Check if the slope is walkable based on steepness
			bool slopeIsTooSteep = angleRadians > maxSlopeRadians;

			// If the slope is walkable, we're good
			if (!slopeIsTooSteep) {
				return true;
			}

			// If the slope is too steep, check if we're moving downhill
			// Get the movement direction
			Vector3 movement = Vector3::Zero;
			if (m_movementInfo.movementFlags & movement_flags::Forward)
			{
				movement += GetSceneNode()->GetOrientation() * Vector3::UnitX;
			}
			if (m_movementInfo.movementFlags & movement_flags::Backward)
			{
				movement -= GetSceneNode()->GetOrientation() * Vector3::UnitX;
			}
			if (m_movementInfo.movementFlags & movement_flags::StrafeLeft)
			{
				movement -= GetSceneNode()->GetOrientation() * Vector3::UnitZ;
			}
			if (m_movementInfo.movementFlags & movement_flags::StrafeRight)
			{
				movement += GetSceneNode()->GetOrientation() * Vector3::UnitZ;
			}

			// If we're not moving, don't allow walking on too steep slopes
			if (movement.IsNearlyEqual(Vector3::Zero))
			{
				return false;
			}

			// Project ground normal onto horizontal plane
			Vector3 horizontalNormal = groundNormal;
			horizontalNormal.y = 0;

			// If the projected normal is nearly zero, we're on a flat surface or vertical wall
			if (horizontalNormal.IsNearlyEqual(Vector3::Zero, 0.001f))
			{
				return false; // Don't allow walking on extremely steep or vertical surfaces
			}

			horizontalNormal.Normalize();

			// Project movement vector onto the horizontal plane
			Vector3 horizontalMovement = movement;
			horizontalMovement.y = 0;
			horizontalMovement.Normalize();

			// Dot product between movement and normal indicates direction
			// Positive: moving uphill, Negative: moving downhill
			float movementDotNormal = horizontalMovement.Dot(horizontalNormal);

			// If we're moving downhill on a steep slope, allow it to trigger falling
			if (movementDotNormal < -0.1f && slopeIsTooSteep)
			{
				return false; // This will cause the player to start falling instead
			}

			// For all other cases (uphill on steep slopes), don't allow movement
			return false;
		}

		// If no ground normal could be determined, allow movement (will likely fall)
		return true;
	}

	void GameUnitC::ApplyMovementInfo(const MovementInfo& movementInfo)
	{
		m_movementInfo = movementInfo;

		// Instantly apply movement data for now
		GetSceneNode()->SetDerivedPosition(movementInfo.position);
		GetSceneNode()->SetDerivedOrientation(Quaternion(Radian(movementInfo.facing), Vector3::UnitY));

		UpdateCollider();
	}

	void GameUnitC::InitializeFieldMap()
	{
		m_fieldMap.Initialize(object_fields::UnitFieldCount);
	}

	void GameUnitC::SetQuestgiverStatus(const QuestgiverStatus status)
	{
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

	bool GameUnitC::OnAuraUpdate(io::Reader& reader)
	{
		uint32 visibleAuraCount = 0;
		if (!(reader >> io::read<uint32>(visibleAuraCount)))
		{
			return false;
		}

		m_auras.clear();

		for (uint32 i = 0; i < visibleAuraCount; ++i)
		{
			uint32 spellId, duration;
			uint64 casterId;
			uint8 auraTypeCount;

			if (!(reader >> io::read<uint32>(spellId)
				>> io::read<uint32>(duration)
				>> io::read_packed_guid(casterId)
				>> io::read<uint8>(auraTypeCount)))
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

			const proto_client::SpellEntry* spell = m_project.spells.getById(spellId);
			if (!spell)
			{
				ELOG("Failed to find spell for aura!");
				continue;
			}

			// Add aura
			m_auras.push_back(std::make_unique<GameAuraC>(*this, *spell, casterId, duration));
		}

		return reader;
	}

	const proto_client::ModelDataEntry* GameUnitC::GetDisplayModel() const
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

		RefreshUnitName();

		// Setup object display
		OnDisplayIdChanged();
	}

	bool GameUnitC::CanStepUp(const Vector3& collisionNormal, float penetrationDepth)
	{
		// Don't attempt to step up if we're falling
		if (m_movementInfo.movementFlags & movement_flags::Falling)
		{
			return false;
		}

		// Only attempt to step up if the obstacle is in front
		if (collisionNormal.y > 0.0f)
		{
			// The collision is with the ground or a slope, not a vertical obstacle
			return false;
		}

		// Only step up if the collision is truly horizontal (wall-like)
		// Calculate the angle between collision normal and horizontal plane
		float horizontalDot = Vector3(collisionNormal.x, 0.0f, collisionNormal.z).NormalizedCopy().Dot(collisionNormal);
		if (horizontalDot < 0.8f) // Allow some tolerance, but mostly horizontal obstacles
		{
			return false;
		}

		// Limit step up height
		if (penetrationDepth > 0.6f)
		{
			return false;
		}

		// TODO: Raycast up?

		// No obstacle above, can attempt to step up
		return true;
	}

	void GameUnitC::OnEntryChanged()
	{
		int32 entryId = Get<int32>(object_fields::Entry);
		if (entryId != -1)
		{
			m_netDriver.GetCreatureData(entryId, std::static_pointer_cast<GameUnitC>(shared_from_this()));
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

	void GameUnitC::SetQuestGiverMesh(const String& meshName)
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
			Color textColor = Color::White;  // Default color

			// Check if this is a party member
			bool isPartyMember = false;
			if (IsPlayer())
			{
				// Check if this player is in the active player's party
				auto activePlayer = ObjectMgr::GetActivePlayer();
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
	}

	void GameUnitC::StartStrafe(bool left)
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
	}

	void GameUnitC::StopMove()
	{
		m_movementInfo.movementFlags &= ~(movement_flags::Forward | movement_flags::Backward);
	}

	void GameUnitC::StopStrafe()
	{
		m_movementInfo.movementFlags &= ~movement_flags::Strafing;
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
	}

	void GameUnitC::StopTurn()
	{
		m_movementInfo.movementFlags &= ~movement_flags::Turning;
	}

	void GameUnitC::SetFacing(const Radian& facing)
	{
		m_movementInfo.facing = facing;

		if (m_sceneNode)
		{
			m_sceneNode->SetOrientation(Quaternion(facing, Vector3::UnitY));
		}
	}

	void GameUnitC::SetMovementPath(const std::vector<Vector3>& points, GameTime moveTime, std::optional<Radian> targetRotation)
	{
		m_movementAnimationTime = 0.0f;
		m_movementAnimation.reset();

		if (points.empty())
		{
			return;
		}

		if (moveTime == 0)
		{
			return;
		}

		std::vector<Vector3> positions;
		positions.reserve(points.size() + 1);
		std::vector<float> keyFrameTimes;
		keyFrameTimes.reserve(points.size() + 1);

		Vector3 prevPosition = m_sceneNode->GetDerivedPosition();
		m_movementStart = prevPosition;

		float groundHeight = 0.0f;
		if (GetCollisionProvider().GetHeightAt(m_movementStart + Vector3::UnitY * 0.25f, 3.0f, groundHeight))
		{
			m_movementStart.y = groundHeight;
			prevPosition.y = groundHeight;
		}

		Quaternion prevRotation = Quaternion::Identity;
		m_movementStartRot = Quaternion::Identity;
		m_sceneNode->SetOrientation(m_movementStartRot);

		// First point
		positions.emplace_back(0.0f, 0.0f, 0.0f);
		keyFrameTimes.push_back(0.0f);

		const float totalDuration = static_cast<float>(moveTime) / 1000.0f;
		float totalDistance = 0.0f;
		for (auto point : points)
		{
			if (GetCollisionProvider().GetHeightAt(point + Vector3::UnitY * 0.25f, 3.0f, groundHeight))
			{
				point.y = groundHeight;
			}

			Vector3 diff = point - prevPosition;
			const float distance = diff.GetLength();
			totalDistance += distance;

			const float duration = distance / m_unitSpeed[movement_type::Run];
			positions.push_back(point - m_movementStart);

			keyFrameTimes.push_back(totalDistance);
			prevPosition = point;
		}

		if (totalDistance <= 0.0f || totalDuration <= 0.0f)
		{
			return;
		}

		ASSERT(positions.size() == keyFrameTimes.size());

		for (auto& time : keyFrameTimes)
		{
			ASSERT(totalDistance != 0.0f);
			ASSERT(totalDuration != 0.0f);

			// Convert to percentage first
			time /= totalDistance;

			// Now multiply with total time in seconds
			time *= totalDuration;
		}

		m_movementAnimation = std::make_unique<Animation>("Movement", totalDuration);
		NodeAnimationTrack* track = m_movementAnimation->CreateNodeTrack(0, m_sceneNode);

		for (size_t i = 0; i < positions.size(); ++i)
		{
			const auto frame = track->CreateNodeKeyFrame(keyFrameTimes[i]);
			frame->SetTranslate(positions[i]);

			if (i < positions.size() - 1)
			{
				prevRotation = Quaternion(GetAngle(positions[i].x, positions[i].z, positions[i + 1].x, positions[i + 1].z), Vector3::UnitY);
			}

			frame->SetRotation(prevRotation);
		}

		m_movementEnd = prevPosition;

		if (targetRotation)
		{
			const auto frame = track->CreateNodeKeyFrame(keyFrameTimes[positions.size() - 1] + 0.1f);
			frame->SetTranslate(positions.back());
			frame->SetRotation(Quaternion(*targetRotation, Vector3::UnitY));
			m_movementAnimation->SetDuration(keyFrameTimes[positions.size() - 1] + 0.1f);
		}

		if (GetCollisionProvider().GetHeightAt(m_movementEnd + Vector3::UnitY * 0.25f, 3.0f, groundHeight))
		{
			m_movementEnd.y = groundHeight;
		}
	}

	static void SetQueryMask(SceneNode* node, const uint32 mask)
	{
		for (uint32 i = 0; i < node->GetNumAttachedObjects(); ++i)
		{
			node->GetAttachedObject(i)->SetQueryFlags(mask);
		}

		for (uint32 i = 0; i < node->GetNumChildren(); ++i)
		{
			SetQueryMask(static_cast<SceneNode*>(node->GetChild(i)), mask);
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
		m_casting = true;
	}

	void GameUnitC::NotifySpellCastCancelled()
	{
		// Interrupt the one shot state if there is any
		if (m_oneShotState)
		{
			m_oneShotState->SetEnabled(false);
			m_oneShotState->SetWeight(0.0f);
			m_oneShotState = nullptr;
		}

		m_casting = false;
	}

	void GameUnitC::NotifySpellCastSucceeded()
	{
		PlayOneShotAnimation(m_castReleaseState);
		m_casting = false;
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

	GameAuraC* GameUnitC::GetAura(uint32 index) const
	{
		if (index < m_auras.size())
		{
			return m_auras[index].get();
		}

		return nullptr;
	}

	void GameUnitC::SetTargetUnit(const std::shared_ptr<GameUnitC>& targetUnit)
	{
		if (m_targetUnit.expired() && !targetUnit)
		{
			return;
		}

		// Check if the target unit actually changed
		{
			auto prevUnit = m_targetUnit.lock();
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

	void GameUnitC::SetInitialSpells(const std::vector<const proto_client::SpellEntry*>& spells)
	{
		m_spells = spells;

		m_spellBookSpells.clear();
		for (const auto* spell : m_spells)
		{
			if ((spell->attributes(0) & static_cast<uint32>(spell_attributes::HiddenClientSide)) == 0)
			{
				m_spellBookSpells.push_back(spell);
			}
		}
	}

	void GameUnitC::LearnSpell(const proto_client::SpellEntry* spell)
	{
		const auto it = std::find_if(m_spells.begin(), m_spells.end(), [spell](const proto_client::SpellEntry* entry) { return entry->id() == spell->id(); });
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
		std::erase_if(m_spells, [spellId](const proto_client::SpellEntry* entry) { return entry->id() == spellId; });
		std::erase_if(m_spellBookSpells, [spellId](const proto_client::SpellEntry* entry) { return entry->id() == spellId; });
	}

	bool GameUnitC::HasSpell(uint32 spellId) const
	{
		return std::find_if(m_spells.begin(), m_spells.end(), [spellId](const proto_client::SpellEntry* entry) { return entry->id() == spellId; }) != m_spells.end();
	}

	const proto_client::SpellEntry* GameUnitC::GetSpell(uint32 index) const
	{
		if (index < m_spells.size())
		{
			return m_spells[index];
		}

		return nullptr;
	}

	const proto_client::SpellEntry* GameUnitC::GetVisibleSpell(uint32 index) const
	{
		if (index < m_spellBookSpells.size())
		{
			return m_spellBookSpells[index];
		}

		return nullptr;
	}

	void GameUnitC::Attack(GameUnitC& victim)
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

	void GameUnitC::SetCreatureInfo(const CreatureInfo& creatureInfo)
	{
		m_creatureInfo = creatureInfo;
		RefreshUnitName();
	}

	const String& GameUnitC::GetName() const
	{
		if (m_creatureInfo.name.empty())
		{
			return GameObjectC::GetName();
		}

		return m_creatureInfo.name;
	}

	void GameUnitC::SetTargetAnimState(AnimationState* newTargetState)
	{
		if (m_targetState == newTargetState)
		{
			// Nothing to do here, we are already there
			return;
		}

		if (m_currentState == newTargetState)
		{
			// Cancel any ongoing transition
			if (m_targetState)
			{
				m_targetState->SetWeight(0.0f);
				m_targetState->SetEnabled(false);
				m_targetState = nullptr;
			}

			// Ensure current state's weight is 1.0f
			m_currentState->SetWeight(1.0f);
			return;
		}

		// Reset any existing target state
		if (m_targetState)
		{
			m_targetState->SetWeight(0.0f);
			m_targetState->SetEnabled(false);
		}

		m_targetState = newTargetState;
		if (m_targetState)
		{
			m_targetState->SetWeight(m_currentState ? (1.0f - m_currentState->GetWeight()) : 0.0f);
			m_targetState->SetEnabled(true);
		}
	}

	void GameUnitC::PlayOneShotAnimation(AnimationState* animState)
	{
		if (!animState)
		{
			return;
		}

		if (animState->IsLoop())
		{
			WLOG("One shot animation has loop flag set to true, not playing!");
			return;
		}

		if (m_oneShotState != nullptr)
		{
			m_oneShotState->SetEnabled(false);
			m_oneShotState->SetWeight(0.0f);
		}

		m_oneShotState = animState;
		m_oneShotState->SetEnabled(true);
		m_oneShotState->SetWeight(1.0f);
		m_oneShotState->SetTimePosition(0.0f);
	}

	void GameUnitC::NotifyAttackSwingEvent()
	{
		PlayOneShotAnimation(m_unarmedAttackState);
	}

	void GameUnitC::NotifyHitEvent()
	{
		PlayOneShotAnimation(m_damageHitState);
	}

	void GameUnitC::SetWeaponProficiency(const uint32 mask)
	{
		m_weaponProficiency = mask;
	}

	void GameUnitC::SetArmorProficiency(const uint32 mask)
	{
		m_armorProficiency = mask;
	}

	bool GameUnitC::IsFriendlyTo(const GameUnitC& other) const
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

		return std::find_if(m_factionTemplate->friends().begin(), m_factionTemplate->friends().end(), [&other](uint32 factionId) { return factionId == other.GetFaction()->id(); }) != m_factionTemplate->friends().end();
	}

	bool GameUnitC::IsHostileTo(const GameUnitC& other) const
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

		return std::find_if(m_factionTemplate->enemies().begin(), m_factionTemplate->enemies().end(), [&other](uint32 factionId) { return factionId == other.GetFaction()->id(); }) != m_factionTemplate->enemies().end();
	}

	void GameUnitC::OnDisplayIdChanged()
	{
		const uint32 displayId = Get<uint32>(object_fields::DisplayId);
		const proto_client::ModelDataEntry* modelEntry = ObjectMgr::GetModelData(displayId);
		if (m_entity) m_entity->SetVisible(modelEntry != nullptr);
		if (!modelEntry)
		{
			return;
		}

		// Reset animation states
		m_idleAnimState = nullptr;
		m_runAnimState = nullptr;
		m_readyAnimState = nullptr;
		m_castingState = nullptr;
		m_castReleaseState = nullptr;
		m_unarmedAttackState = nullptr;
		m_deathState = nullptr;
		m_targetState = nullptr;
		m_currentState = nullptr;
		m_oneShotState = nullptr;
		m_jumpStartState = nullptr;
		m_fallingState = nullptr;
		m_landState = nullptr;
		m_customizationDefinition = nullptr;

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
				for (const auto& kvp : modelEntry->customizationproperties())
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
			// Just update the mesh
			m_entity->SetMesh(MeshManager::Get().Load(meshFile));
		}

		if (m_customizationDefinition)
		{
			m_configuration.Apply(*this, *m_customizationDefinition);
		}

		m_collider.radius = 0.5f;

		// Initialize animation states from new mesh
		if (m_entity->HasAnimationState("Idle"))
		{
			m_idleAnimState = m_entity->GetAnimationState("Idle");
		}

		if (m_entity->HasAnimationState("Run"))
		{
			m_runAnimState = m_entity->GetAnimationState("Run");
		}

		if (m_entity->HasAnimationState("UnarmedReady"))
		{
			m_readyAnimState = m_entity->GetAnimationState("UnarmedReady");
		}

		if (!m_readyAnimState)
		{
			m_readyAnimState = m_idleAnimState;
		}

		if (m_entity->HasAnimationState("CastLoop"))
		{
			m_castingState = m_entity->GetAnimationState("CastLoop");
		}

		if (m_entity->HasAnimationState("CastRelease"))
		{
			m_castReleaseState = m_entity->GetAnimationState("CastRelease");
			m_castReleaseState->SetLoop(false);
			m_castReleaseState->SetPlayRate(2.0f);
		}

		if (m_entity->HasAnimationState("UnarmedAttack01"))
		{
			m_unarmedAttackState = m_entity->GetAnimationState("UnarmedAttack01");
			m_unarmedAttackState->SetLoop(false);
		}

		if (m_entity->HasAnimationState("Death"))
		{
			m_deathState = m_entity->GetAnimationState("Death");
			m_deathState->SetLoop(false);
			m_deathState->SetTimePosition(0.0f);
		}

		if (m_entity->HasAnimationState("Hit"))
		{
			m_damageHitState = m_entity->GetAnimationState("Hit");
			m_damageHitState->SetLoop(false);
			m_damageHitState->SetTimePosition(0.0f);
		}

		// Initialize jump animation states
		if (m_entity->HasAnimationState("JumpStart"))
		{
			m_jumpStartState = m_entity->GetAnimationState("JumpStart");
			m_jumpStartState->SetLoop(false);
		}

		if (m_entity->HasAnimationState("Falling"))
		{
			m_fallingState = m_entity->GetAnimationState("Falling");
			m_fallingState->SetLoop(true);
		}

		if (m_entity->HasAnimationState("Land"))
		{
			m_landState = m_entity->GetAnimationState("Land");
			m_landState->SetLoop(false);
		}

		if (m_entity)
		{
			m_nameComponentNode->SetPosition(Vector3::UnitY * (m_entity->GetBoundingRadius()));
		}

		OnScaleChanged();
	}

	void GameUnitC::UpdateCollider()
	{
		constexpr float halfHeight = 0.5f;

		m_collider.pointA = GetPosition() + Vector3(0.0f, 1.0f, 0.0f);
		m_collider.pointB = GetPosition() + Vector3(0.0f, 1.0f + halfHeight * 2.0f, 0.0f);
		m_collider.radius = 0.25f;
	}

	void GameUnitC::PerformGroundCheck()
	{
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

	void GameUnitC::Apply(const VisibilitySetPropertyGroup& group, const AvatarConfiguration& configuration)
	{
		// First, hide all sub entities with the given visibility set tag
		if (!group.subEntityTag.empty())
		{
			for (uint16 i = 0; i < m_entity->GetNumSubEntities(); ++i)
			{
				ASSERT(m_entity->GetMesh()->GetSubMeshCount() == m_entity->GetNumSubEntities());

				SubMesh& subMesh = m_entity->GetMesh()->GetSubMesh(i);
				if (subMesh.HasTag(group.subEntityTag))
				{
					SubEntity* subEntity = m_entity->GetSubEntity(i);
					ASSERT(subEntity);
					subEntity->SetVisible(false);
				}
			}

		}

		const auto it = configuration.chosenOptionPerGroup.find(group.GetId());
		if (it == configuration.chosenOptionPerGroup.end())
		{
			// Nothing to do here because we have no value set
			return;
		}

		// Now make each referenced sub entity visible
		for (const auto& value : group.possibleValues)
		{
			if (value.valueId == it->second)
			{
				for (const auto& subEntityName : value.visibleSubEntities)
				{
					if (SubEntity* subEntity = m_entity->GetSubEntity(subEntityName))
					{
						subEntity->SetVisible(true);
					}
				}
			}
		}
	}

	void GameUnitC::Apply(const MaterialOverridePropertyGroup& group, const AvatarConfiguration& configuration)
	{
		const auto it = configuration.chosenOptionPerGroup.find(group.GetId());
		if (it == configuration.chosenOptionPerGroup.end())
		{
			// Nothing to do here because we have no value set
			return;
		}

		// Now make each referenced sub entity visible
		for (const auto& value : group.possibleValues)
		{
			if (value.valueId == it->second)
			{
				for (const auto& pair : value.subEntityToMaterial)
				{
					if (SubEntity* subEntity = m_entity->GetSubEntity(pair.first))
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

	void GameUnitC::Apply(const ScalarParameterPropertyGroup& group, const AvatarConfiguration& configuration)
	{

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

	const proto_client::SpellEntry* GameUnitC::GetOpenSpell() const
	{
		for (const auto* spell : m_spells)
		{
			for (const auto& effect : spell->effects())
			{
				if (effect.type() == spell_effects::OpenLock)
				{
					return spell;
				}
			}
		}

		return nullptr;
	}

	void GameUnitC::HandleCollision()
	{
		// Get collider boundaries
		const AABB colliderBounds = CapsuleToAABB(GetCollider());
		std::vector<const Entity*> potentialTrees;
		potentialTrees.reserve(8);
		GetCollisionProvider().GetCollisionTrees(colliderBounds, potentialTrees);

		// Track if we need to adjust position
		bool collisionDetected = false;
		Vector3 totalCorrection(0, 0, 0);
		float minPenetration = std::numeric_limits<float>::max();
		Vector3 mainCollisionNormal = Vector3::Zero;
		
		// Keep track of all collision points and normals for better sliding
		struct CollisionData {
			Vector3 point;
			Vector3 normal;
			float penetration;
		};
		std::vector<CollisionData> collisions;
		collisions.reserve(8);

		// Ray we'll use for AABB tree traversal - will be reset for each entity
		Ray tempRay;
		tempRay.hitDistance = std::numeric_limits<float>::max();

		// Perform terrain collision first - use triangles directly instead of trying to use terrain as entities
		const Capsule& capsule = GetCollider();
		// Create multiple test points around the capsule for terrain collision
		const Vector3 capsuleBottom = capsule.pointA;
		const Vector3 capsuleTop = capsule.pointB;
		const Vector3 capsuleDirection = (capsuleTop - capsuleBottom).NormalizedCopy();
		const float capsuleHeight = (capsuleTop - capsuleBottom).GetLength();
		
		// Check points at different heights along the capsule axis
		constexpr int numVerticalPoints = 5;
		for (int i = 0; i < numVerticalPoints; i++) {
			const float heightFraction = i / static_cast<float>(numVerticalPoints - 1);
			const Vector3 center = capsuleBottom + capsuleDirection * (capsuleHeight * heightFraction);
			
			// Check points around the capsule radius
			constexpr int numRadialPoints = 6;
			for (int j = 0; j < numRadialPoints; j++) {
				const float angle = j * (2.0f * Pi / numRadialPoints);
				// Get a vector perpendicular to capsule direction
				Vector3 radialDir = Vector3::UnitY;
				if (std::abs(capsuleDirection.Dot(radialDir)) > 0.9f) {
					radialDir = Vector3::UnitX; // Use X if Y is too close to capsule direction
				}
				// Generate perpendicular vectors
				Vector3 perpDir1 = capsuleDirection.Cross(radialDir).NormalizedCopy();
				Vector3 perpDir2 = capsuleDirection.Cross(perpDir1).NormalizedCopy();
				
				// Calculate offset vector for this point
				Vector3 offset = (perpDir1 * std::cos(angle) + perpDir2 * std::sin(angle)) * capsule.radius;
				Vector3 testPoint = center + offset;
				
				// Check for terrain collision (cast ray down)
				Ray terrainRay(testPoint + Vector3::UnitY * 1.0f, Vector3::UnitY * -1.0f);
				terrainRay.origin.y += 1.0f; // Start above to ensure we hit the terrain
				float groundHeight = 0.0f;
				Vector3 groundNormal;
				
				// Use terrain height and normal checks for collision detection
				if (GetCollisionProvider().GetHeightAt(testPoint, 2.0f, groundHeight) && 
					GetCollisionProvider().GetGroundNormalAt(testPoint, 2.0f, groundNormal)) {
					
					if (testPoint.y < groundHeight + 0.1f) { // Small threshold
						// Calculate penetration depth (how far inside terrain we are)
						float penetrationDepth = groundHeight - testPoint.y + 0.1f;
						
						// Only consider valid penetrations
						if (penetrationDepth > 0.0f && penetrationDepth < 1.0f) {
							// Ensure normal is pointing upward (flip if necessary)
							if (groundNormal.y < 0) {
								groundNormal = -groundNormal;
							}
							groundNormal.Normalize();
							
							Vector3 collisionPoint = testPoint;
							collisionPoint.y = groundHeight;
							
							// Save the collision
							collisions.push_back({ collisionPoint, groundNormal, penetrationDepth });
							collisionDetected = true;
							
							// Track minimum penetration for step up logic
							if (penetrationDepth < minPenetration) {
								minPenetration = penetrationDepth;
								mainCollisionNormal = groundNormal;
							}
						}
					}
				}
			}
		}

		// Now handle entity collisions as before
		// Iterate over potential collision entities
		for (const Entity* entity : potentialTrees)
		{
			const auto& tree = entity->GetMesh()->GetCollisionTree();
			if (tree.IsEmpty())
			{
				continue;
			}

			const auto matrix = entity->GetParentNodeFullTransform();

			// Use stack-based traversal of AABB tree instead of checking every triangle
			struct StackEntry {
				unsigned int nodeIndex;
				float distance;
			};

			// Max depth is typically log(n) where n is number of triangles, so 50 is very conservative
			StackEntry stack[50];
			unsigned int stackCount = 0;

			// Start with root node
			if (!tree.GetNodes().empty()) {
				stack[stackCount++] = { 0, 0.0f };
			}

			// Process nodes in tree
			while (stackCount > 0) {
				// Pop node from stack
				StackEntry& entry = stack[--stackCount];
				const auto& node = tree.GetNodes()[entry.nodeIndex];

				// Transform node bounds by entity matrix
				AABB nodeBounds = node.bounds;
				nodeBounds.Transform(matrix);

				// Skip if this node doesn't intersect with capsule
				if (!nodeBounds.Intersects(colliderBounds)) {
					continue;
				}

				if (node.numFaces > 0) {
					// Leaf node - check triangles
					for (uint32 i = 0; i < node.numFaces; i++) {
						uint32 faceIndex = node.startFace + i;
						uint32 indexBase = faceIndex * 3;

						const Vector3& v0 = matrix * tree.GetVertices()[tree.GetIndices()[indexBase]];
						const Vector3& v1 = matrix * tree.GetVertices()[tree.GetIndices()[indexBase + 1]];
						const Vector3& v2 = matrix * tree.GetVertices()[tree.GetIndices()[indexBase + 2]];

						Vector3 collisionPoint, collisionNormal;
						float penetrationDepth;

						if (CapsuleTriangleIntersection(GetCollider(), v0, v1, v2, collisionPoint, collisionNormal, penetrationDepth))
						{
							// Store the collision with the least penetration for step up logic
							if (penetrationDepth < minPenetration)
							{
								minPenetration = penetrationDepth;
								mainCollisionNormal = collisionNormal;
							}

							// Save all collisions for better resolution
							collisions.push_back({ collisionPoint, collisionNormal, penetrationDepth });
							collisionDetected = true;

							// If the collision normal is pointing up significantly, it's likely a ground collision
							// which we may want to handle differently (e.g., snap to ground)
							float upDot = collisionNormal.Dot(Vector3::UnitY);

							// When in falling state, handle collisions differently
							if (m_movementInfo.movementFlags & movement_flags::Falling)
							{
								// When falling, don't let the player slide up walls
								// First determine if this is a wall collision (mostly horizontal normal)
								bool isWallCollision = std::abs(upDot) < 0.5f; // Normal is mostly horizontal

								if (isWallCollision)
								{
									// For wall collisions when falling, only allow movement away from the wall
									// Calculate horizontal movement direction
									Vector3 horizontalMovement(m_movementInfo.jumpCosAngle, 0, m_movementInfo.jumpSinAngle);
									float moveIntoWall = horizontalMovement.Dot(Vector3(collisionNormal.x, 0, collisionNormal.z));

									// If moving into the wall, remove the velocity component in that direction
									if (moveIntoWall < 0)
									{
										// Project movement onto the wall plane
										Vector3 wallPlaneNormal(collisionNormal.x, 0, collisionNormal.z);
										wallPlaneNormal.Normalize();

										// Calculate correction to prevent sliding up walls
										Vector3 correction = collisionNormal * penetrationDepth;

										// Only apply horizontal correction for wall collisions
										correction.y = 0;

										// Scale down the correction to reduce bouncing
										correction *= 0.9f;

										totalCorrection += correction;

										// Adjust horizontal movement to slide along wall
										// Reset the horizontal velocity to preserve downward motion but prevent wall climbing
										float prevJumpXZSpeed = m_movementInfo.jumpXZSpeed;
										if (prevJumpXZSpeed > 0.1f) // Only if we have significant horizontal speed
										{
											// Reduce horizontal velocity when hitting walls while falling - less aggressive reduction
											m_movementInfo.jumpXZSpeed *= 0.9f;

											// Update direction to slide along the wall
											Vector3 slideDirection = horizontalMovement - wallPlaneNormal * moveIntoWall * 1.05f;

											if (!slideDirection.IsNearlyEqual(Vector3::Zero, 0.001f))
											{
												slideDirection.Normalize();
												m_movementInfo.jumpSinAngle = slideDirection.z;
												m_movementInfo.jumpCosAngle = slideDirection.x;
											}
										}
									}
									continue;
								}
							}

							// Handle regular ground collisions
							if (upDot > 0.7f)  // We're colliding with the ground
							{
								// Just adjust the height directly with slight offset to avoid z-fighting
								m_movementInfo.position.y = collisionPoint.y + 0.001f;
								continue;
							}

							// If collision is mostly horizontal, check if we can step up instead
							if (std::abs(upDot) < 0.3f && penetrationDepth < 0.6f && CanStepUp(collisionNormal, penetrationDepth))
							{
								// Try to step up over small obstacles, but don't overshoot
								float stepUpHeight = penetrationDepth + 0.05f;
								Vector3 stepUpOffset = Vector3::UnitY * stepUpHeight;
								m_movementInfo.position += stepUpOffset;

								// Don't add horizontal correction when stepping up
								continue;
							}

							// For normal collision response, store the correction but don't apply yet
							// We'll process all collisions together for smoother response
							Vector3 correction = collisionNormal * penetrationDepth * 0.95f; // Slightly reduced to prevent overshooting
						}
					}
				}
				else if (node.children > 0) {
					// Internal node - add children to stack
					const auto& leftChild = tree.GetNodes()[node.children];
					const auto& rightChild = tree.GetNodes()[node.children + 1];

					// Transform child bounds
					AABB leftBounds = leftChild.bounds;
					leftBounds.Transform(matrix);

					AABB rightBounds = rightChild.bounds;
					rightBounds.Transform(matrix);

					// If both children are valid, prioritize the closer one
					bool leftValid = leftBounds.Intersects(colliderBounds);
					bool rightValid = rightBounds.Intersects(colliderBounds);

					if (leftValid && rightValid) {
						// Put right child first (will be processed second)
						stack[stackCount++] = { node.children + 1, 0.0f };
						// Put left child second (will be processed first)
						stack[stackCount++] = { node.children, 0.0f };
					}
					else if (leftValid) {
						stack[stackCount++] = { node.children, 0.0f };
					}
					else if (rightValid) {
						stack[stackCount++] = { node.children + 1, 0.0f };
					}
				}
			}
		}

		// Process all collisions to create a better response
		if (!collisions.empty())
		{
			// If multiple collisions, compute a weighted average correction
			if (collisions.size() > 1) 
			{
				Vector3 averageNormal = Vector3::Zero;
				float totalWeight = 0.0f;
	            
				// First pass: compute weighted average normal
				for (const auto& collision : collisions) 
				{
					// Weight by inverse penetration (less penetration = higher weight)
					float weight = 1.0f / (collision.penetration + 0.1f);
					averageNormal += collision.normal * weight;
					totalWeight += weight;
				}
	            
				if (totalWeight > 0.0f) 
				{
					averageNormal /= totalWeight;
					averageNormal.Normalize();
	                
					// Find the maximum penetration in the average normal direction
					float maxPenetration = 0.0f;
					for (const auto& collision : collisions) 
					{
						float projection = collision.penetration * std::max(0.0f, collision.normal.Dot(averageNormal));
						maxPenetration = std::max(maxPenetration, projection);
					}
	                
					// Apply a single correction in the average direction
					Vector3 correction = averageNormal * maxPenetration * 0.9f; // 90% to avoid overshooting

					// Ensure correction is mostly horizontal for non-ground collisions
					if (averageNormal.Dot(Vector3::UnitY) < 0.7f) {
						if (std::abs(correction.y) > std::abs(correction.x) + std::abs(correction.z)) {
							correction.y *= 0.2f;  // Reduce vertical component
						}
					}
	                
					m_movementInfo.position += correction;
				}
			} 
			else if (collisionDetected) 
			{
				// For a single collision, apply the correction directly (already computed above)
				// but with a smoothing factor to prevent bouncing
				Vector3 correction = collisions[0].normal * collisions[0].penetration * 0.95f;
	            
				// Ensure correction is mostly horizontal for non-ground collisions
				float upDot = collisions[0].normal.Dot(Vector3::UnitY);
				if (upDot < 0.7f) {
					if (std::abs(correction.y) > std::abs(correction.x) + std::abs(correction.z)) {
						correction.y *= 0.2f;  // Reduce vertical component
					}
				}
	            
				m_movementInfo.position += correction;
			}
		}
	}

	void GameUnitC::HandleSlopeSliding(const Vector3& desiredMovement)
	{
		// Get the ground normal at the current position
		Vector3 groundNormal;
		if (!GetCollisionProvider().GetGroundNormalAt(m_movementInfo.position + Vector3::UnitY * 0.55f, 1.0f, groundNormal))
		{
			return;  // No ground detected, can't slide
		}

		// Normalize the ground normal
		groundNormal.Normalize();

		// Ensure the normal is pointing upward (flip if necessary)
		if (groundNormal.y < 0)
		{
			groundNormal = -groundNormal;
		}

		// Calculate slope angle to determine if we should start falling instead of sliding sideways
		float slopeAngleDot = groundNormal.Dot(Vector3::UnitY);
		float slopeAngleRadians = std::acos(Clamp(slopeAngleDot, -1.0f, 1.0f));
		float maxSlopeRadians = 50.0f * 0.0174533f; // Convert max walkable slope to radians

		// Calculate the direction the player wants to go
		Vector3 playerIntendedDirection = desiredMovement;
		playerIntendedDirection.Normalize();

		// Calculate the forward vector in world space
		Vector3 playerForward = GetSceneNode()->GetOrientation() * Vector3::UnitX;
		playerForward.y = 0;
		playerForward.Normalize();

		// Calculate dot product to determine if player is trying to move downhill
		Vector3 horizontalNormal = groundNormal;
		horizontalNormal.y = 0;

		// Skip if almost flat
		if (horizontalNormal.IsNearlyEqual(Vector3::Zero, 0.001f))
		{
			// On flat ground, just continue normal movement
			return;
		}

		horizontalNormal.Normalize();
		float movingDownhill = -playerForward.Dot(horizontalNormal); // Negative because we want downhill direction

		// If slope is very steep and player is moving mostly downhill, start falling instead of sliding sideways
		if (slopeAngleRadians > maxSlopeRadians && movingDownhill > 0.3f)
		{
			// Calculate horizontal movement direction based on current movement
			Vector3 movement = desiredMovement;
			movement.Normalize();

			// Calculate the angle in world space
			const Radian facing = m_movementInfo.facing;
			const float sinFacing = std::sin(facing.GetValueRadians());
			const float cosFacing = std::cos(facing.GetValueRadians());

			// Prepare movement info but don't set the flag yet
			// We need to notify the server first, then set the flag
			float jumpXZSpeed = GetSpeed(movement_type::Run);
			float jumpSinAngle = movement.z * cosFacing - movement.x * sinFacing;
			float jumpCosAngle = movement.x * cosFacing + movement.z * sinFacing;

			const bool wasFalling = (m_movementInfo.movementFlags & movement_flags::Falling) != 0;

			// Now set the falling flag and update movement parameters
			m_movementInfo.movementFlags |= movement_flags::Falling;
			m_movementInfo.jumpVelocity = -0.1f;
			m_movementInfo.jumpXZSpeed = jumpXZSpeed;
			m_movementInfo.jumpSinAngle = jumpSinAngle;
			m_movementInfo.jumpCosAngle = jumpCosAngle;

			// Notify the server about fall start
			// This will send the movement info in its current state
			if (!wasFalling)
			{
				m_netDriver.OnMoveFall(*this);
			}
			return;
		}

		// The formula for projecting a vector onto a plane with normal n is:
		// projection = v - (v·n)n
		float dot = desiredMovement.Dot(groundNormal);
		Vector3 projectedMovement = desiredMovement - groundNormal * dot;

		// If the projected movement is very small, don't bother sliding
		if (projectedMovement.IsNearlyEqual(Vector3::Zero, 0.01f))
		{
			return;
		}

		// Mix of original direction and projected direction to make movement feel more natural
		// More downhill = more following the original direction
		float blendFactor = Clamp(movingDownhill, 0.0f, 1.0f) * 0.7f;
		Vector3 blendedMovement = projectedMovement * (1.0f - blendFactor) + desiredMovement * blendFactor;

		// Scale the movement to preserve energy
		blendedMovement.Normalize();
		blendedMovement *= desiredMovement.GetLength() * 0.9f;  // 90% energy preservation

		// Apply the sliding movement
		Vector3 initialPosition = m_movementInfo.position;
		m_movementInfo.position += blendedMovement;

		// Check for collisions after sliding
		HandleCollision();

		// If we didn't make any progress, we're probably stuck
		if (m_movementInfo.position.IsNearlyEqual(initialPosition, 0.001f))
		{
			return;
		}
	}

	void GameUnitC::PlayLandAnimation()
	{
		// Play landing animation if available
		if (m_landState)
		{
			// Reset animation state to beginning
			m_landState->SetTimePosition(0.0f);
			PlayOneShotAnimation(m_landState);
		}

		// Also ensure jump start state is reset
		if (m_jumpStartState)
		{
			m_jumpStartState->SetTimePosition(0.0f);
		}
	}

}
