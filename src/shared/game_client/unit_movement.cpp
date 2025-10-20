#include "unit_movement.h"

#include "game_unit_c.h"
#include "log/default_log_levels.h"
#include "base/guarded_value.h"
#include "scene_graph/scene.h"
#include "math/capsule.h"
#include "math/aabb.h"

#include <cmath>
#include <cfloat>
#include <algorithm>

#include "debug_interface.h"

namespace mmo
{
	constexpr float MIN_TICK_TIME = 1e-6f;
	constexpr float MIN_FLOOR_DIST = 0.019f;
	constexpr float MAX_FLOOR_DIST = 0.024f;
	constexpr float BRAKE_TO_STOP_VELOCITY = 0.1f;
	constexpr float SWEEP_EDGE_REJECT_DISTANCE = 0.0015f;

	static const auto s_stringColor = Color(0.0f, 0.66f, 1.0f);

	void FindFloorResult::SetFromSweep(const CollisionHitResult& hit, const float sweepFloorDistance,
	                                   const bool isWalkableFloor)
	{
		bValidFloor = hit.IsValidBlockingHit();
		bWalkableFloor = isWalkableFloor;
		bLineTrace = false;
		FloorDistance = sweepFloorDistance;
		LineDist = 0.f;
		HitResult = hit;
	}

	void FindFloorResult::SetFromLineTrace(const CollisionHitResult& hit, const float sweepFloorDistance,
	                                       const float lineDistance, const bool isWalkableFloor)
	{
		// We require a sweep that hit if we are going to use a line result.
		if (HitResult.bBlockingHit && hit.bBlockingHit)
		{
			// Override most of the sweep result with the line result, but save some values
			CollisionHitResult OldHit(HitResult);
			HitResult = hit;

			// Restore some of the old values. We want the new normals and hit actor, however.
			HitResult.Time = OldHit.Time;
			HitResult.ImpactPoint = OldHit.ImpactPoint;
			HitResult.Location = OldHit.Location;
			HitResult.TraceStart = OldHit.TraceStart;
			HitResult.TraceEnd = OldHit.TraceEnd;

			bLineTrace = true;
			FloorDistance = sweepFloorDistance;
			LineDist = lineDistance;
			bWalkableFloor = isWalkableFloor;
		}
	}

	UnitMovement::UnitMovement(GameUnitC& owner)
		: m_movedUnit(owner)
		  , m_movementMode(MovementMode::Falling)
	{
		SetWalkableFloorY(0.71f);
	}

	void UnitMovement::Tick(const float deltaSeconds)
	{
		const Vector3 inputVector = m_movedUnit.ConsumeInputVector();

		if (m_movedUnit.IsPlayer())
		{
			ControlledCharacterMove(inputVector, deltaSeconds);
		}
	}

	float UnitMovement::ComputeAnalogInputModifier() const
	{
		const float maxAcceleration = GetMaxAcceleration();
		if (m_acceleration.GetSquaredLength() > 0.f && maxAcceleration > 1.e-8f)
		{
			const float ratio = m_acceleration.GetLength() / maxAcceleration;
			return (ratio < 0.0f) ? 0.0f : (ratio > 1.0f) ? 1.0f : ratio;
		}

		return 0.0f;
	}

	void UnitMovement::PerformMovement(const float deltaTime)
	{
		// Apply rotation if there is any
		GetUpdatedNode().Yaw(m_movedUnit.ConsumeRotation() * deltaTime, TransformSpace::World);

		ASSERT(!m_velocity.IsNAN());
		m_movedUnit.ClearJumpInput(deltaTime);

		// Change position
		RunSimulation(deltaTime, 0);

#if defined(_DEBUG) && false
		DEBUG_OUTPUT_STRING_EX("Position: " + GetUpdatedNode().GetPosition().ToString(), 0.f, s_stringColor);
#endif
	}

	void UnitMovement::RunSimulation(const float deltaTime, const int32 iterations)
	{
		if ((deltaTime < MIN_TICK_TIME) || (iterations >= m_maxSimulationIterations))
		{
			return;
		}

		const bool savedMovementInProgress = m_movementInProgress;
		m_movementInProgress = true;

		switch (m_movementMode)
		{
		case MovementMode::None:
			break;

		case MovementMode::Walking:
			HandleWalking(deltaTime, iterations);
			break;

		case MovementMode::Falling:
			HandleFalling(deltaTime, iterations);
			break;

		case MovementMode::Swimming:
			HandleSwimming(deltaTime, iterations);
			break;

		case MovementMode::Flying:
			HandleFlying(deltaTime, iterations);
			break;

		default:
			SetMovementMode(MovementMode::None);
			break;
		}

		m_movementInProgress = savedMovementInProgress;
	}

	void UnitMovement::SetMovementMode(const MovementMode newMovementMode)
	{
		if (m_movementMode == newMovementMode)
		{
			return;
		}

		const MovementMode prevMovementMode = m_movementMode;
		m_movementMode = newMovementMode;

		// React to changes in the movement mode.
		if (m_movementMode == MovementMode::Walking)
		{
			// Walking uses only XZ velocity, and must be on a walkable floor, with a Base.
			m_velocity = Vector3::VectorPlaneProject(m_velocity, -GetGravityDirection());

			// make sure we update our new floor/base on initial entry of the walking physics
			FindFloor(GetUpdatedNode().GetPosition(), m_currentFloor, nullptr);
			AdjustFloorHeight();
		}
		else
		{
			m_currentFloor.Clear();

			if (m_movementMode == MovementMode::Falling)
			{
				m_movedUnit.OnStartFalling();
			}

			if (m_movementMode == MovementMode::None)
			{
				m_movedUnit.ResetJumpState();
			}
		}

		m_movedUnit.OnMovementModeChanged(prevMovementMode, m_movementMode);
	}

	void UnitMovement::ControlledCharacterMove(const Vector3& inputVector, const float deltaTime)
	{
		m_movedUnit.CheckJumpInput();

		m_acceleration = ScaleInputAcceleration(ConstrainInputAcceleration(inputVector));
		m_analogInputModifier = ComputeAnalogInputModifier();

		PerformMovement(deltaTime);
	}

	Vector3 UnitMovement::ConstrainInputAcceleration(const Vector3& inputAcceleration) const
	{
		// walking or falling pawns ignore up/down sliding
		const float inputAccelerationDotGravityNormal = inputAcceleration.Dot(-GetGravityDirection());
		if (!(std::abs(inputAccelerationDotGravityNormal) < FLT_EPSILON) && (IsMovingOnGround() || IsFalling()))
		{
			return Vector3::VectorPlaneProject(inputAcceleration, -GetGravityDirection());
		}

		return inputAcceleration;
	}

	Vector3 UnitMovement::ScaleInputAcceleration(const Vector3& inputAcceleration) const
	{
		return inputAcceleration.GetClampedToMaxSize(1.0f) * GetMaxAcceleration();
	}

	void UnitMovement::CalcVelocity(const float deltaTime, float friction, const bool fluid, const float brakingDeceleration)
	{
		if (deltaTime < MIN_TICK_TIME)
		{
			return;
		}

		friction = std::max(0.f, friction);
		const float maxAccel = GetMaxAcceleration();
		const float maxSpeed = GetMaxSpeed();

		if (m_forceMaxAcceleration)
		{
			// Force acceleration at full speed.
			// In consideration order for direction: Acceleration, then Velocity, then Unit's rotation.
			if (m_acceleration.GetSquaredLength() > 1.e-8f)
			{
				m_acceleration = m_acceleration.NormalizedCopy() * maxAccel;
			}
			else
			{
				m_acceleration = (m_velocity.GetSquaredLength() < 1.e-8f
					                  ? (GetUpdatedNode().GetOrientation() * Vector3::UnitZ)
					                  : m_velocity.NormalizedCopy()) * maxAccel;
			}

			m_analogInputModifier = 1.f;
		}

		// Path following above didn't care about the analog modifier, but we do for everything else below, so get the fully modified value.
		// Use max of requested speed and max speed if we modified the speed in ApplyRequestedMove above.
		const float maxInputSpeed = std::max(maxSpeed * m_analogInputModifier, GetMinAnalogSpeed());

		// Apply braking or deceleration
		const bool zeroAcceleration = m_acceleration.IsZero();
		const bool velocityOverMax = IsExceedingMaxSpeed(maxSpeed);

		// Only apply braking if there is no acceleration, or we are over our max speed and need to slow down to it.
		if (zeroAcceleration || velocityOverMax)
		{
			const Vector3 oldVelocity = m_velocity;

			const float actualBrakingFriction = friction;
			ApplyVelocityBraking(deltaTime, actualBrakingFriction, brakingDeceleration);

			// Don't allow braking to lower us below max speed if we started above it.
			if (velocityOverMax && m_velocity.GetSquaredLength() < maxSpeed * maxSpeed && m_acceleration.Dot(oldVelocity) > 0.0f)
			{
				m_velocity = oldVelocity.NormalizedCopy() * maxSpeed;
			}
		}
		else if (!zeroAcceleration)
		{
			// Friction affects our ability to change direction
			const Vector3 accelDir = m_acceleration.NormalizedCopy();
			const float velSize = m_velocity.GetLength();
			m_velocity = m_velocity - (m_velocity - accelDir * velSize) * std::min(deltaTime * friction, 1.f);
		}

		// Apply fluid friction
		if (fluid)
		{
			m_velocity = m_velocity * (1.f - std::min(friction * deltaTime, 1.f));
		}

		// Apply input acceleration
		if (!zeroAcceleration)
		{
			const float NewMaxInputSpeed = IsExceedingMaxSpeed(maxInputSpeed) ? m_velocity.GetLength() : maxInputSpeed;
			m_velocity += m_acceleration * deltaTime;
			m_velocity = m_velocity.GetClampedToMaxSize(NewMaxInputSpeed);
		}
	}

	void UnitMovement::ApplyVelocityBraking(float deltaTime, float friction, float brakingDeceleration)
	{
		if (m_velocity.IsZero() || deltaTime < MIN_TICK_TIME)
		{
			return;
		}

		const float frictionFactor = std::max(0.f, m_brakingFrictionFactor);
		friction = std::max(0.f, friction * frictionFactor);
		brakingDeceleration = std::max(0.f, brakingDeceleration);
		const bool zeroFriction = (friction == 0.f);
		const bool zeroBraking = (brakingDeceleration == 0.f);

		if (zeroFriction && zeroBraking)
		{
			return;
		}

		const Vector3 oldVel = m_velocity;

		float remainingTime = deltaTime;
		const float maxTimeStep = (m_brakingSubStepTime < 1.0f / 75.0f)
			                          ? 1.0f / 75.0f
			                          : (m_brakingSubStepTime > 1.0f / 20.0f)
			                          ? 1.0f / 20.0f
			                          : m_brakingSubStepTime;

		// Decelerate to brake to a stop
		const Vector3 revAccel = (zeroBraking ? Vector3::Zero : (m_velocity.NormalizedCopy() * -brakingDeceleration));
		while (remainingTime >= MIN_TICK_TIME)
		{
			// Zero friction uses constant deceleration, so no need for iteration.
			const float dt = ((remainingTime > maxTimeStep && !zeroFriction)
				                  ? std::min(maxTimeStep, remainingTime * 0.5f)
				                  : remainingTime);
			remainingTime -= dt;

			// apply friction and braking
			m_velocity = m_velocity + (m_velocity * (-friction) + revAccel) * dt;

			// Don't reverse direction
			if ((m_velocity | oldVel) <= 0.f)
			{
				m_velocity = Vector3::Zero;
				return;
			}
		}

		// Clamp to zero if nearly zero, or if below min threshold and braking.
		const float velocitySizeSquared = m_velocity.GetSquaredLength();
		if (velocitySizeSquared <= 1.e-4f || (!zeroBraking && velocitySizeSquared <= BRAKE_TO_STOP_VELOCITY * BRAKE_TO_STOP_VELOCITY))
		{
			m_velocity = Vector3::Zero;
		}
	}

	bool UnitMovement::IsExceedingMaxSpeed(float maxSpeed) const
	{
		maxSpeed = std::max(0.f, maxSpeed);
		const float maxSpeedSquared = maxSpeed * maxSpeed;

		// Allow 1% error tolerance, to account for numeric imprecision.
		constexpr float overVelocityPercent = 1.01f;
		return (m_velocity.GetSquaredLength() > maxSpeedSquared * overVelocityPercent);
	}

	float UnitMovement::GetMaxSpeed() const
	{
		switch (m_movementMode)
		{
		case MovementMode::Walking:
			return m_movedUnit.GetSpeed(movement_type::Run);
		case MovementMode::Falling:
			return m_movedUnit.GetSpeed(movement_type::Run);
		case MovementMode::Swimming:
			return m_movedUnit.GetSpeed(movement_type::Swim);
		case MovementMode::Flying:
			return m_movedUnit.GetSpeed(movement_type::Flight);
		case MovementMode::None:
		default:
			return 0.f;
		}
	}

	float UnitMovement::GetMinAnalogSpeed() const
	{
		switch (m_movementMode)
		{
		case MovementMode::Walking:
		case MovementMode::Falling:
			return m_minAnalogWalkSpeed;
		default:
			return 0.f;
		}
	}

	float UnitMovement::GetMaxJumpHeight() const
	{
		return 0.0f;
	}

	float UnitMovement::GetMaxAcceleration() const
	{
		return m_maxAcceleration;
	}

	float UnitMovement::GetMaxBrakingDeceleration() const
	{
		switch (m_movementMode)
		{
		case MovementMode::Walking:
			return m_brakingDecelerationWalking;
		case MovementMode::Falling:
			return m_brakingDecelerationFalling;
		case MovementMode::Swimming:
			return m_brakingDecelerationSwimming;
		case MovementMode::Flying:
			return m_brakingDecelerationFlying;
		case MovementMode::None:
		default:
			return 0.f;
		}
	}

	Vector3 UnitMovement::GetCurrentAcceleration() const
	{
		return m_acceleration;
	}

	float UnitMovement::GetAnalogInputModifier() const
	{
		return m_analogInputModifier;
	}

	void UnitMovement::HandleWalking(const float deltaTime, int32 iterations)
	{
		if (deltaTime < MIN_TICK_TIME)
		{
			return;
		}

		m_justTeleported = false;
		bool checkedFall = false;
		bool triedLedgeMove = false;
		float remainingTime = deltaTime;

		const MovementMode startingMovementMode = m_movementMode;

		// Perform the move
		while ((remainingTime >= MIN_TICK_TIME) && (iterations < m_maxSimulationIterations))
		{
			iterations++;
			m_justTeleported = false;

			const float timeTick = GetSimulationTimeStep(remainingTime, iterations);
			remainingTime -= timeTick;

			const Vector3 oldLocation = GetUpdatedNode().GetPosition();

			// Ensure velocity is horizontal.
			MaintainHorizontalGroundVelocity();
			m_acceleration = Vector3::VectorPlaneProject(m_acceleration, -m_gravityDirection);

			// Apply acceleration
			const bool bSkipForLedgeMove = triedLedgeMove;
			if (!bSkipForLedgeMove)
			{
				CalcVelocity(timeTick, m_groundFriction, false, GetMaxBrakingDeceleration());
			}

			// Compute move parameters
			const Vector3 moveVelocity = m_velocity;
			const Vector3 delta = moveVelocity * timeTick;
			const bool zeroDelta = delta.IsNearlyEqual(Vector3::Zero, 1.e-4f);
			StepDownResult stepDownResult;

			if (zeroDelta)
			{
				remainingTime = 0.f;
			}
			else
			{
				MoveAlongFloor(moveVelocity, timeTick, &stepDownResult);

				if (IsSwimming()) // just entered water
				{
					// TODO
					//StartSwimming(OldLocation, OldVelocity, timeTick, remainingTime, Iterations);
					return;
				}

				if (m_movementMode != startingMovementMode)
				{
					// unit ended up in a different mode, probably due to the step-up-and-over flow
					// let's refund the estimated unused time (if any) and keep moving in the new mode
					const float desiredDist = delta.GetLength();
					if (desiredDist > 1.e-4f)
					{
						const float actualDist = ProjectToGravityFloor(GetUpdatedNode().GetPosition() - oldLocation).
							GetLength();
						remainingTime += timeTick * (1.f - std::min(1.f, actualDist / desiredDist));
					}

					RunSimulation(remainingTime, iterations);
					return;
				}
			}

			// Update floor.
			// StepUp might have already done it for us.
			if (stepDownResult.bComputedFloor)
			{
				m_currentFloor = stepDownResult.FloorResult;
			}
			else
			{
				FindFloor(GetUpdatedNode().GetPosition(), m_currentFloor, nullptr);
			}

			// Validate the floor check
			if (m_currentFloor.IsWalkableFloor())
			{
				AdjustFloorHeight();
			}
			else if (m_currentFloor.HitResult.bStartPenetrating && remainingTime <= 0.f)
			{
				// The floor check failed because it started in penetration
				// We do not want to try to move downward because the downward sweep failed, rather we'd like to try to pop out of the floor.
				CollisionHitResult hit(m_currentFloor.HitResult);
				hit.TraceEnd = hit.TraceStart + -GetGravityDirection() * MAX_FLOOR_DIST;
				const Vector3 requestedAdjustment = GetPenetrationAdjustment(hit);
				ResolvePenetration(requestedAdjustment, hit, GetUpdatedNode().GetOrientation());
				//bForceNextFloorCheck = true;
			}

			// check if just entered water
			if (IsSwimming())
			{
				// TODO
				//StartSwimming(OldLocation, Velocity, timeTick, remainingTime, Iterations);
				return;
			}

			// See if we need to start falling.
			if (!m_currentFloor.IsWalkableFloor() && !m_currentFloor.HitResult.bStartPenetrating)
			{
				const bool bMustJump = m_justTeleported || zeroDelta;
				if ((bMustJump || !checkedFall) && CheckFall(delta, oldLocation, remainingTime, timeTick,
				                                             iterations))
				{
					return;
				}
				checkedFall = true;
			}

			// Allow overlap events and such to change physics state and velocity
			if (IsMovingOnGround())
			{
				// Make velocity reflect actual move
				if (!m_justTeleported && timeTick >= MIN_TICK_TIME)
				{
					m_velocity = (GetUpdatedNode().GetPosition() - oldLocation) / timeTick;
					MaintainHorizontalGroundVelocity();
				}
			}

			// If we didn't move at all this iteration then abort (since future iterations will also be stuck).
			if (GetUpdatedNode().GetPosition() == oldLocation)
			{
				remainingTime = 0.f;
				break;
			}
		}

		if (IsMovingOnGround())
		{
			MaintainHorizontalGroundVelocity();
		}
	}

	bool UnitMovement::ResolvePenetration(const Vector3& ProposedAdjustment, const CollisionHitResult& Hit, const Quaternion& NewRotationQuat)
	{
		// SceneComponent can't be in penetration, so this function really only applies to PrimitiveComponent.
		const Vector3 Adjustment = ProposedAdjustment;
		if (!Adjustment.IsZero())
		{
			// We really want to make sure that precision differences or differences between the overlap test and sweep tests don't put us into another overlap,
			// so make the overlap test a bit more restrictive.
			const float OverlapInflation = 0.001f;

			bool bEncroached = OverlapTest(Hit.TraceStart + Adjustment, CollisionParams());
			if (!bEncroached)
			{
				// Move without sweeping.
				SafeMoveNode(Adjustment, NewRotationQuat, false, nullptr);
				m_justTeleported |= true;
			}
			else
			{
				// Try sweeping as far as possible...
				CollisionHitResult SweepOutHit(1.f);
				bool bMoved = SafeMoveNode(Adjustment, NewRotationQuat, true, &SweepOutHit);

				// Still stuck?
				if (!bMoved && SweepOutHit.bStartPenetrating)
				{
					// Combine two MTD results to get a new direction that gets out of multiple surfaces.
					const Vector3 SecondMTD = GetPenetrationAdjustment(SweepOutHit);
					const Vector3 CombinedMTD = Adjustment + SecondMTD;
					if (SecondMTD != Adjustment && !CombinedMTD.IsZero())
					{
						bMoved = SafeMoveNode(CombinedMTD, NewRotationQuat, true, nullptr);
					}
				}

				// Still stuck?
				if (!bMoved)
				{
					// Try moving the proposed adjustment plus the attempted move direction. This can sometimes get out of penetrations with multiple objects
					const Vector3 MoveDelta = Hit.TraceEnd - Hit.TraceStart;
					if (!MoveDelta.IsZero())
					{
						bMoved = SafeMoveNode(Adjustment + MoveDelta, NewRotationQuat, true, nullptr);
						
						// Finally, try the original move without MTD adjustments, but allowing depenetration along the MTD normal.
						// This was blocked because MOVECOMP_NeverIgnoreBlockingOverlaps was true for the original move to try a better depenetration normal, but we might be running in to other geometry in the attempt.
						// This won't necessarily get us all the way out of penetration, but can in some cases and does make progress in exiting the penetration.
						if (!bMoved && MoveDelta.Dot(Adjustment) > 0.f)
						{
							bMoved = SafeMoveNode(MoveDelta, NewRotationQuat, true, nullptr);
						}
					}
				}

				m_justTeleported |= bMoved;
			}
		}

		return m_justTeleported;
	}

	bool UnitMovement::CheckFall(const Vector3& delta, const Vector3& oldLocation, const float remainingTime, const float timeTick, const int32 iterations)
	{
		if (IsMovingOnGround())
		{
			// If still walking, then fall. If not, assume the user set a different mode they want to keep.
			StartFalling(iterations, remainingTime, timeTick, delta, oldLocation);
		}
		return true;
	}

	void UnitMovement::StartFalling(const int32 iterations, float remainingTime, const float timeTick, const Vector3& delta, const Vector3& subLoc)
	{
		// start falling 
		const float desiredDist = delta.GetLength();
		const float actualDist = ProjectToGravityFloor(GetUpdatedNode().GetPosition() - subLoc).GetLength();
		remainingTime = (desiredDist < 1.e-4f)
			                ? 0.f
			                : remainingTime + timeTick * (1.f - std::min(1.f, actualDist / desiredDist));

		if (IsMovingOnGround())
		{
			SetMovementMode(MovementMode::Falling);
		}

		RunSimulation(remainingTime, iterations);
	}

	Vector3 UnitMovement::GetPenetrationAdjustment(const CollisionHitResult& hit)
	{
		if (!hit.bStartPenetrating)
		{
			return Vector3::Zero;
		}

		constexpr float pullBackDistance = 0.00125f;
		const float penetrationDepth = (hit.PenetrationDepth > 0.f ? hit.PenetrationDepth : 0.125f);

		Vector3 result = hit.Normal * (penetrationDepth + pullBackDistance);
		result = result.GetClampedToMaxSize(1.0f);

		return result;
	}

	void UnitMovement::SetGravitySpaceY(Vector3& vector, const float y) const
	{
		vector = ProjectToGravityFloor(vector) - GetGravityDirection() * y;
	}

	Vector3 UnitMovement::ComputeGroundMovementDelta(const Vector3& delta, const CollisionHitResult& rampHit, const bool hitFromLineTrace) const
	{
		const Vector3 floorNormal = rampHit.ImpactNormal;
		const float floorNormalY = GetGravitySpaceY(floorNormal);
		const float contactNormalY = GetGravitySpaceY(rampHit.Normal);

		if (floorNormalY < (1.f - 1.e-4f) && floorNormalY > 1.e-4f && contactNormalY > 1.e-4f && !hitFromLineTrace &&
			IsWalkable(rampHit))
		{
			// Compute a vector that moves parallel to the surface, by projecting the horizontal movement direction onto the ramp.
			const float floorDotDelta = (floorNormal | delta);
			Vector3 rampMovement = delta;
			SetGravitySpaceY(rampMovement, -floorDotDelta / floorNormalY);
			if (m_maintainHorizontalGroundVelocity)
			{
				return rampMovement;
			}

			return rampMovement.NormalizedCopy() * delta.GetLength();
		}

		return delta;
	}

	float UnitMovement::SlideAlongSurface(const Vector3& delta, const float time, const Vector3& inNormal, CollisionHitResult& hit, const bool handleImpact)
	{
		if (!hit.bBlockingHit)
		{
			return 0.f;
		}

		Vector3 normal(inNormal);
		const float normalY = GetGravitySpaceY(normal);
		if (IsMovingOnGround())
		{
			// We don't want to be pushed up an unwalkable surface.
			if (normalY > 0.f)
			{
				if (!IsWalkable(hit))
				{
					normal = ProjectToGravityFloor(normal).NormalizedCopy();
				}
			}
			else if (normalY < -1.e-4f)
			{
				// Don't push down into the floor when the impact is on the upper portion of the capsule.
				if (m_currentFloor.FloorDistance < MIN_FLOOR_DIST && m_currentFloor.bValidFloor)
				{
					const Vector3 floorNormal = m_currentFloor.HitResult.Normal;
					const bool floorOpposedToMovement = (delta | floorNormal) < 0.f && (GetGravitySpaceY(floorNormal) <
						1.f - 0.00001f);
					if (floorOpposedToMovement)
					{
						normal = floorNormal;
					}

					normal = ProjectToGravityFloor(normal).NormalizedCopy();
				}
			}
		}

		const Vector3 oldHitNormal = normal;

		Vector3 slideDelta = ComputeSlideVector(delta, time, normal);
		if ((slideDelta | delta) > 0.f)
		{
			const Quaternion rotation = GetUpdatedNode().GetOrientation();
			SafeMoveNode(slideDelta, rotation, true, &hit);

			const float firstHitPercent = hit.Time;
			float percentTimeApplied = firstHitPercent;
			if (hit.IsValidBlockingHit())
			{
				// Notify first impact
				if (handleImpact)
				{
					HandleImpact(hit, firstHitPercent * time, slideDelta);
				}

				// Compute new slide normal when hitting multiple surfaces.
				TwoWallAdjust(slideDelta, hit, oldHitNormal);

				// Only proceed if the new direction is of significant length and not in reverse of original attempted move.
				if (!slideDelta.IsNearlyZero(1e-3f) && (slideDelta | delta) > 0.f)
				{
					// Perform second move
					SafeMoveNode(slideDelta, rotation, true, &hit);
					const float secondHitPercent = hit.Time * (1.f - firstHitPercent);
					percentTimeApplied += secondHitPercent;

					// Notify second impact
					if (handleImpact && hit.bBlockingHit)
					{
						HandleImpact(hit, secondHitPercent * time, slideDelta);
					}
				}
			}

			return Clamp(percentTimeApplied, 0.f, 1.f);
		}

		return 0.f;
	}

	void UnitMovement::TwoWallAdjust(Vector3& worldSpaceDelta, const CollisionHitResult& hit, const Vector3& oldHitNormal) const
	{
		const Vector3 inDelta = worldSpaceDelta;

		Vector3 delta = worldSpaceDelta;
		const Vector3 hitNormal = hit.Normal;

		if ((oldHitNormal | hitNormal) <= 0.f) //90 or less corner, so use cross product for direction
		{
			const Vector3 desiredDir = delta;
			Vector3 newDir = hitNormal.Cross(oldHitNormal);
			newDir = newDir.NormalizedCopy();
			delta = newDir * (delta | newDir) * (1.f - hit.Time);
			if ((desiredDir | delta) < 0.f)
			{
				delta = delta * -1.f;
			}
		}
		else //adjust to new wall
		{
			const Vector3 desiredDir = delta;
			delta = ComputeSlideVector(delta, 1.f - hit.Time, hitNormal);
			if ((delta | desiredDir) <= 0.f)
			{
				delta = Vector3::Zero;
			}
			else if (std::abs((hitNormal | oldHitNormal) - 1.f) < 1.e-4f)
			{
				// we hit the same wall again even after adjusting to move along it the first time
				// nudge away from it (this can happen due to precision issues)
				delta += hitNormal * 0.01f;
			}
		}

		worldSpaceDelta = delta;

		if (IsMovingOnGround())
		{
			// Allow slides up walkable surfaces, but not unwalkable ones (treat those as vertical barriers).
			const float worldSpaceDeltaY = GetGravitySpaceY(worldSpaceDelta);
			if (worldSpaceDeltaY > 0.f)
			{
				const float hitNormalY = GetGravitySpaceY(hit.Normal);
				if ((hitNormalY >= m_walkableFloorY || IsWalkable(hit)) && hitNormalY > 1.e-4f)
				{
					// Maintain horizontal velocity
					const float time = (1.f - hit.Time);
					const Vector3 scaledDelta = worldSpaceDelta.NormalizedCopy() * inDelta.GetLength();
					const float newDeltaY = (GetGravitySpaceY(scaledDelta) / hitNormalY);
					worldSpaceDelta = (ProjectToGravityFloor(inDelta) + -GetGravityDirection() * newDeltaY) * time;

					// Should never exceed MaxStepHeight in vertical component, so rescale if necessary.
					// This should be rare (Hit.Normal.y above would have been very small) but we'd rather lose horizontal velocity than go too high.
					const float deltaY = GetGravitySpaceY(worldSpaceDelta);
					if (deltaY > m_maxStepHeight)
					{
						const float rescale = m_maxStepHeight / deltaY;
						worldSpaceDelta *= rescale;
					}
				}
				else
				{
					worldSpaceDelta = ProjectToGravityFloor(worldSpaceDelta);
				}
			}
			else if (worldSpaceDeltaY < 0.f)
			{
				// Don't push down into the floor.
				if (m_currentFloor.FloorDistance < MIN_FLOOR_DIST && m_currentFloor.bValidFloor)
				{
					worldSpaceDelta = ProjectToGravityFloor(worldSpaceDelta);
				}
			}
		}
	}

	Vector3 UnitMovement::ComputeSlideVector(const Vector3& delta, const float time, const Vector3& normal) const
	{
		Vector3 result = Vector3::VectorPlaneProject(delta, normal) * time;

		if (IsFalling())
		{
			result = HandleSlopeBoosting(result, delta, time, normal);
		}

		return result;
	}

	Vector3 UnitMovement::HandleSlopeBoosting(const Vector3& slideResult, const Vector3& delta, const float time, const Vector3& normal) const
	{
		Vector3 result = slideResult;
		const float resultY = GetGravitySpaceY(result);
		if (resultY > 0.f)
		{
			// Don't move any higher than we originally intended.
			const float yLimit = GetGravitySpaceY(delta) * time;
			if (resultY - yLimit > 1.e-4f)
			{
				if (yLimit > 0.f)
				{
					// Rescale the entire vector (not just the Y component) otherwise we change the direction and likely head right back into the impact.
					const float UpPercent = yLimit / resultY;
					result *= UpPercent;
				}
				else
				{
					// We were heading down but were going to deflect upwards. Just make the deflection horizontal.
					result = Vector3::Zero;
				}

				// Make remaining portion of original result horizontal and parallel to impact normal.
				const Vector3 remainderXz = ProjectToGravityFloor(slideResult - result);
				const Vector3 normalXz = ProjectToGravityFloor(normal).NormalizedCopy();
				const Vector3 adjust = Vector3::VectorPlaneProject(remainderXz, normalXz);
				result += adjust;
			}
		}

		return result;
	}

	void UnitMovement::HandleImpact(const CollisionHitResult& impact, float timeSlice, const Vector3& moveDelta)
	{
		// TODO: Do something on impact
	}

	void UnitMovement::MoveAlongFloor(const Vector3& inVelocity, const float deltaSeconds, StepDownResult* outStepDownResult)
	{
		if (!m_currentFloor.IsWalkableFloor())
		{
			return;
		}

		// Move along the current floor
		const Vector3 delta = ProjectToGravityFloor(inVelocity) * deltaSeconds;

		CollisionHitResult hit(1.f);
		Vector3 rampVector = ComputeGroundMovementDelta(delta, m_currentFloor.HitResult, m_currentFloor.bLineTrace);
		SafeMoveNode(rampVector, GetUpdatedNode().GetOrientation(), true, &hit);
		float lastMoveTimeSlice = deltaSeconds;

		if (hit.bStartPenetrating)
		{
			// Allow this hit to be used as an impact we can deflect off, otherwise we do nothing the rest of the update and appear to hitch.
			HandleImpact(hit);
			SlideAlongSurface(delta, 1.f, hit.Normal, hit, true);

			if (hit.bStartPenetrating)
			{
				assert(false);
				//OnCharacterStuckInGeometry(&Hit);
			}
		}
		else if (hit.IsValidBlockingHit())
		{
			// We impacted something (most likely another ramp, but possibly a barrier).
			float percentTimeApplied = hit.Time;
			if ((hit.Time > 0.f) && (GetGravitySpaceY(hit.Normal) > 1.e-4f) && IsWalkable(hit))
			{
				// Another walkable ramp.
				const float initialPercentRemaining = 1.f - percentTimeApplied;
				rampVector = ComputeGroundMovementDelta(delta * initialPercentRemaining, hit, false);
				lastMoveTimeSlice = initialPercentRemaining * lastMoveTimeSlice;
				SafeMoveNode(rampVector, GetUpdatedNode().GetOrientation(), true, &hit);

				const float secondHitPercent = hit.Time * initialPercentRemaining;
				percentTimeApplied = Clamp(percentTimeApplied + secondHitPercent, 0.f, 1.f);
			}

			if (hit.IsValidBlockingHit())
			{
				if (CanStepUp(hit))
				{
					// hit a barrier, try to step up
					const Vector3 preStepUpLocation = GetUpdatedNode().GetPosition();
					if (!StepUp(GetGravityDirection(), delta * (1.f - percentTimeApplied), hit, outStepDownResult))
					{
						HandleImpact(hit, lastMoveTimeSlice, rampVector);
						SlideAlongSurface(delta, 1.f - percentTimeApplied, hit.Normal, hit, true);
					}
					else
					{
						if (!m_maintainHorizontalGroundVelocity)
						{
							// Don't recalculate velocity based on this height adjustment, if considering vertical adjustments. Only consider horizontal movement.
							m_justTeleported = true;
							const float stepUpTimeSlice = (1.f - percentTimeApplied) * deltaSeconds;
							if (stepUpTimeSlice >= 1.e-4f)
							{
								m_velocity = (GetUpdatedNode().GetPosition() - preStepUpLocation) / stepUpTimeSlice;
								m_velocity = ProjectToGravityFloor(m_velocity);
							}
						}
					}
				}
				else if (false)	// TODO: Only do this if character can not step up on hit component
				{
					HandleImpact(hit, lastMoveTimeSlice, rampVector);
					SlideAlongSurface(delta, 1.f - percentTimeApplied, hit.Normal, hit, true);
				}
			}
		}
	}

	void UnitMovement::MaintainHorizontalGroundVelocity()
	{
		if (GetGravitySpaceY(m_velocity) != 0.f)
		{
			if (m_maintainHorizontalGroundVelocity)
			{
				// Ramp movement already maintained the velocity, so we just want to remove the vertical component.
				m_velocity = ProjectToGravityFloor(m_velocity);
			}
			else
			{
				// Rescale velocity to be horizontal but maintain magnitude of last update.
				m_velocity = ProjectToGravityFloor(m_velocity).NormalizedCopy() * m_velocity.GetLength();
			}
		}
	}


	float UnitMovement::GetSimulationTimeStep(float remainingTime, const int32 iterations) const
	{
		if (remainingTime > m_maxSimulationTimeStep)
		{
			if (iterations < m_maxSimulationIterations)
			{
				// Subdivide moves to be no longer than MaxSimulationTimeStep seconds
				remainingTime = std::min(m_maxSimulationTimeStep, remainingTime * 0.5f);
			}
		}

		// no less than MIN_TICK_TIME (to avoid potential divide-by-zero during simulation).
		return std::max(MIN_TICK_TIME, remainingTime);
	}

	void UnitMovement::HandleSwimming(float deltaTime, int32 iterations)
	{
		// TODO
		SetMovementMode(MovementMode::Falling);
	}

	void UnitMovement::HandleFalling(const float deltaTime, int32 iterations)
	{
		if (deltaTime < MIN_TICK_TIME)
		{
			return;
		}

		const Vector3 fallAcceleration = ProjectToGravityFloor(GetFallingLateralAcceleration(deltaTime));
		const bool bHasLimitedAirControl = ShouldLimitAirControl(fallAcceleration);

		float remainingTime = deltaTime;
		while ((remainingTime >= MIN_TICK_TIME) && (iterations < m_maxSimulationIterations))
		{
			iterations++;
			float timeTick = GetSimulationTimeStep(remainingTime, iterations);
			remainingTime -= timeTick;

			const Quaternion pawnRotation = GetUpdatedNode().GetOrientation();
			m_justTeleported = false;

			const Vector3 oldVelocity = m_velocity;

			// Apply input
			const float maxDeceleration = GetMaxBrakingDeceleration();

			// Compute Velocity
			{
				// Acceleration = FallAcceleration for CalcVelocity(), but we restore it after using it.
				GuardValue restoreAcceleration(m_acceleration, fallAcceleration);

				m_velocity.y = 0.f;
				CalcVelocity(timeTick, m_fallingLateralFriction, false, maxDeceleration);
				m_velocity.y = oldVelocity.y;
			}

			// Compute current gravity
			const Vector3 gravity = -GetGravityDirection() * GetGravityY();
			float gravityTime = timeTick;

			// If jump is providing force, gravity may be affected.
			bool bEndingJumpForce = false;
			if (m_movedUnit.GetJumpForceTimeRemaining() > 0.0f)
			{
				// Consume some of the force time. Only the remaining time (if any) is affected by gravity when bApplyGravityWhileJumping=false.
				const float jumpForceTime = std::min(m_movedUnit.GetJumpForceTimeRemaining(), timeTick);
				gravityTime = m_applyGravityWhileJumping ? timeTick : std::max(0.0f, timeTick - jumpForceTime);

				// Update Character state
				m_movedUnit.SetJumpForceTimeRemining(m_movedUnit.GetJumpForceTimeRemaining() - jumpForceTime);
				if (m_movedUnit.GetJumpForceTimeRemaining() <= 0.0f)
				{
					m_movedUnit.ResetJumpState();
					bEndingJumpForce = true;
				}
			}

			// Apply gravity
			m_velocity = NewFallVelocity(m_velocity, gravity, gravityTime);

			// Compute change in position (using midpoint integration method).
			Vector3 adjusted = (oldVelocity + m_velocity) * 0.5f * timeTick;

			// Special handling if ending the jump force where we didn't apply gravity during the jump.
			if (bEndingJumpForce && !m_applyGravityWhileJumping)
			{
				// We had a portion of the time at constant speed then a portion with acceleration due to gravity.
				// Account for that here with a more correct change in position.
				const float nonGravityTime = std::max(0.f, timeTick - gravityTime);
				adjusted = (oldVelocity * nonGravityTime) + ((oldVelocity + m_velocity) * 0.5f * gravityTime);
			}

			// Move and check for collisions
			CollisionHitResult hit(1.f);
			SafeMoveNode(adjusted, pawnRotation, true, &hit);

			float lastMoveTimeSlice = timeTick;
			float subTimeTickRemaining = timeTick * (1.f - hit.Time);

			if (IsSwimming()) //just entered water
			{
				remainingTime += subTimeTickRemaining;
				//TODO: StartSwimming(OldLocation, OldVelocity, timeTick, remainingTime, Iterations);
				return;
			}

			if (hit.bBlockingHit)
			{
				if (IsValidLandingSpot(GetUpdatedNode().GetPosition(), hit))
				{
					remainingTime += subTimeTickRemaining;
					ProcessLanded(hit, remainingTime, iterations);
					return;
				}

				// Compute impact deflection based on final velocity, not integration step.
				// This allows us to compute a new velocity from the deflected vector, and ensures the full gravity effect is included in the slide result.
				adjusted = m_velocity * timeTick;

				// See if we can convert a normally invalid landing spot (based on the hit result) to a usable one.
				if (!hit.bStartPenetrating && ShouldCheckForValidLandingSpot(hit))
				{
					const Vector3 pawnLocation = GetUpdatedNode().GetPosition();
					FindFloorResult floorResult;
					FindFloor(pawnLocation, floorResult, nullptr);

					// Note that we only care about capsule sweep floor results, since the line trace may detect a lower walkable surface that our falling capsule wouldn't actually reach yet.
					if (!floorResult.bLineTrace && floorResult.IsWalkableFloor() && IsValidLandingSpot(
						pawnLocation, floorResult.HitResult))
					{
						remainingTime += subTimeTickRemaining;
						ProcessLanded(floorResult.HitResult, remainingTime, iterations);
						return;
					}
				}

				HandleImpact(hit, lastMoveTimeSlice, adjusted);

				// If we've changed physics mode, abort.
				if (!IsFalling())
				{
					return;
				}

				// Limit air control based on what we hit.
				// We moved to the impact point using air control, but may want to deflect from there based on a limited air control acceleration.
				Vector3 velocityNoAirControl = oldVelocity;
				Vector3 airControlAccel = m_acceleration;
				if (bHasLimitedAirControl)
				{
					// Compute VelocityNoAirControl
					{
						// Find velocity *without* acceleration.
						GuardValue restoreAcceleration(m_acceleration, Vector3::Zero);
						GuardValue restoreVelocity(m_velocity, oldVelocity);

						m_velocity.y = 0.f;
						CalcVelocity(timeTick, m_fallingLateralFriction, false, maxDeceleration);
						velocityNoAirControl = Vector3(m_velocity.x, oldVelocity.z, m_velocity.z);

						velocityNoAirControl = NewFallVelocity(velocityNoAirControl, gravity, gravityTime);
					}

					constexpr bool bCheckLandingSpot = false; // we already checked above.
					airControlAccel = (m_velocity - velocityNoAirControl) / timeTick;
					const Vector3 airControlDeltaV = LimitAirControl(airControlAccel, hit, bCheckLandingSpot) * lastMoveTimeSlice;
					adjusted = (velocityNoAirControl + airControlDeltaV) * lastMoveTimeSlice;
				}

				const Vector3 oldHitNormal = hit.Normal;
				const Vector3 oldHitImpactNormal = hit.ImpactNormal;
				Vector3 delta = ComputeSlideVector(adjusted, 1.f - hit.Time, oldHitNormal);

				// Compute velocity after deflection (only gravity component for RootMotion)
				if (subTimeTickRemaining > 1.e-4f && !m_justTeleported)
				{
					const Vector3 newVelocity = (delta / subTimeTickRemaining);
					m_velocity = newVelocity;
				}

				if (subTimeTickRemaining > 1.e-4f && (delta | adjusted) > 0.f)
				{
					// Move in deflected direction.
					SafeMoveNode(delta, pawnRotation, true, &hit);

					if (hit.bBlockingHit)
					{
						// hit second wall
						lastMoveTimeSlice = subTimeTickRemaining;
						subTimeTickRemaining = subTimeTickRemaining * (1.f - hit.Time);

						if (IsValidLandingSpot(GetUpdatedNode().GetPosition(), hit))
						{
							remainingTime += subTimeTickRemaining;
							ProcessLanded(hit, remainingTime, iterations);
							return;
						}

						HandleImpact(hit, lastMoveTimeSlice, delta);

						// If we've changed physics mode, abort.
						if (!IsFalling())
						{
							return;
						}

						// Act as if there was no air control on the last move when computing new deflection.
						if (bHasLimitedAirControl && GetGravitySpaceY(hit.Normal) > 0.001f /*VERTICAL_SLOPE_NORMAL_Z*/)
						{
							const Vector3 lastMoveNoAirControl = velocityNoAirControl * lastMoveTimeSlice;
							delta = ComputeSlideVector(lastMoveNoAirControl, 1.f, oldHitNormal);
						}

						TwoWallAdjust(delta, hit, oldHitNormal);

						// Limit air control, but allow a slide along the second wall.
						if (bHasLimitedAirControl)
						{
							constexpr bool checkLandingSpot = false; // we already checked above.
							const Vector3 airControlDeltaV = LimitAirControl(
								airControlAccel, hit, checkLandingSpot) * subTimeTickRemaining;

							// Only allow if not back in to first wall
							if (airControlDeltaV.Dot(oldHitNormal) > 0.f)
							{
								delta += (airControlDeltaV * subTimeTickRemaining);
							}
						}

						// Compute velocity after deflection (only gravity component for RootMotion)
						if (subTimeTickRemaining > 1.e-4f && !m_justTeleported)
						{
							const Vector3 NewVelocity = (delta / subTimeTickRemaining);
							m_velocity = NewVelocity;
						}

						// bDitch=true means that pawn is straddling two slopes, neither of which it can stand on
						bool bDitch = ((GetGravitySpaceY(oldHitImpactNormal) > 0.f) && (
								GetGravitySpaceY(hit.ImpactNormal) > 0.f) && (std::abs(GetGravitySpaceY(delta)) <=
								1.e-4f)
							&& ((hit.ImpactNormal | oldHitImpactNormal) < 0.f));
						SafeMoveNode(delta, pawnRotation, true, &hit);

						if (hit.Time == 0.f)
						{
							// if we are stuck then try to side step
							Vector3 sideDelta = ProjectToGravityFloor(oldHitNormal + hit.ImpactNormal).NormalizedCopy();
							if (sideDelta.IsNearlyZero())
							{
								sideDelta = Vector3(oldHitNormal.z, 0.0f, -oldHitNormal.x).NormalizedCopy();
							}
							SafeMoveNode(sideDelta, pawnRotation, true, &hit);
						}

						if (bDitch || IsValidLandingSpot(GetUpdatedNode().GetPosition(), hit) || hit.Time == 0.f)
						{
							remainingTime = 0.f;
							ProcessLanded(hit, remainingTime, iterations);
							return;
						}
						/*
						if (GetPerchRadiusThreshold() > 0.f && Hit.Time == 1.f && GetGravitySpaceY(oldHitImpactNormal) >= m_walkableFloorY)
						{
							// We might be in a virtual 'ditch' within our perch radius. This is rare.
							const Vector3 PawnLocation = GetBase().GetPosition();
							const float ZMovedDist = std::abs(GetGravitySpaceY(PawnLocation - OldLocation));
							const float MovedDist2D = ProjectToGravityFloor(PawnLocation - OldLocation).GetLength();
							if (ZMovedDist <= 0.2f * timeTick && MovedDist2D <= 4.f * timeTick)
							{
								Vector3 GravityRelativeVelocity = RotateWorldToGravity(Velocity);
								GravityRelativeVelocity.x += 0.25f * GetMaxSpeed() * (RandomStream.FRand() - 0.5f);
								GravityRelativeVelocity.z += 0.25f * GetMaxSpeed() * (RandomStream.FRand() - 0.5f);
								GravityRelativeVelocity.y = std::max<float>(m_jumpYVelocity * 0.25f, 1.f);
								m_velocity = RotateGravityToWorld(GravityRelativeVelocity);
								delta = m_velocity * timeTick;
								SafeMoveNode(delta, PawnRotation, true, &Hit);
							}
						}
						*/
					}
				}
			}

			const Vector3 gravityProjectedVelocity = ProjectToGravityFloor(m_velocity);
			if (gravityProjectedVelocity.GetSquaredLength() <= 1.e-4f * 10.f)
			{
				m_velocity = GetGravitySpaceComponentY(m_velocity);
			}
		}
	}

	bool UnitMovement::CanStepUp(const CollisionHitResult& hit) const
	{
		if (!hit.IsValidBlockingHit() || m_movementMode == MovementMode::Falling)
		{
			return false;
		}

		return true;
	}

	class ScopedNodeUpdate : public NonCopyable
	{
	public:
		explicit ScopedNodeUpdate(SceneNode& node)
			: m_node(node)
		{
			m_initialPosition = node.GetPosition();
			m_initialRotation = node.GetOrientation();
			m_initialScale = node.GetScale();
		}

		void RevertMove()
		{
			m_node.SetPosition(m_initialPosition);
			m_node.SetOrientation(m_initialRotation);
			m_node.SetScale(m_initialScale);
		}

	private:
		SceneNode& m_node;
		Vector3 m_initialPosition;
		Quaternion m_initialRotation;
		Vector3 m_initialScale;
	};

	bool UnitMovement::StepUp(const Vector3& gravDir, const Vector3& delta, const CollisionHitResult& inHit, StepDownResult* outStepDownResult)
	{
		if (!CanStepUp(inHit) || m_maxStepHeight <= 0.f)
		{
			return false;
		}

		const Vector3 oldLocation = GetUpdatedNode().GetPosition();
		float pawnRadius = 0.35f, pawnHalfHeight = 1.0f;

		// Don't bother stepping up if top of capsule is hitting something.
		const float initialImpactY = inHit.ImpactPoint | -gravDir;
		const float oldLocationY = oldLocation | -gravDir;
		if (initialImpactY > oldLocationY + (pawnHalfHeight - pawnRadius))
		{
			return false;
		}

		if (gravDir.IsZero())
		{
			return false;
		}

		float stepTravelUpHeight = m_maxStepHeight;
		float stepTravelDownHeight = stepTravelUpHeight;
		const float stepSideY = -1.f * inHit.ImpactNormal.Dot(gravDir);
		float initialFloorBaseY = oldLocationY - pawnHalfHeight;
		float floorPointY = initialFloorBaseY;

		if (IsMovingOnGround() && m_currentFloor.IsWalkableFloor())
		{
			// Since we float a variable amount off the floor, we need to enforce max step height off the actual point of impact with the floor.
			const float floorDist = std::max(0.f, m_currentFloor.GetDistanceToFloor());
			initialFloorBaseY -= floorDist;
			stepTravelUpHeight = std::max(stepTravelUpHeight - floorDist, 0.f);
			stepTravelDownHeight = (m_maxStepHeight + MAX_FLOOR_DIST * 2.f);

			const bool bHitVerticalFace = !IsWithinEdgeTolerance(inHit.Location, inHit.ImpactPoint, pawnRadius);
			if (!m_currentFloor.bLineTrace && !bHitVerticalFace)
			{
				floorPointY = m_currentFloor.HitResult.ImpactPoint | -gravDir;
			}
			else
			{
				// Base floor point is the base of the capsule moved down by how far we are hovering over the surface we are hitting.
				floorPointY -= m_currentFloor.FloorDistance;
			}
		}

		// Don't step up if the impact is below us, accounting for distance from floor.
		if (initialImpactY <= initialFloorBaseY)
		{
			return false;
		}

		// Scope our movement updates, and do not apply them until all intermediate moves are completed.
		ScopedNodeUpdate scopedStepUpMovement(GetUpdatedNode());

		// step up - treat as vertical wall
		CollisionHitResult sweepUpHit(1.f);
		const Quaternion rotation = GetUpdatedNode().GetOrientation();
		SafeMoveNode(-gravDir * stepTravelUpHeight, rotation, true, &sweepUpHit);

		if (sweepUpHit.bStartPenetrating)
		{
			// Undo movement
			scopedStepUpMovement.RevertMove();
			return false;
		}

		// step fwd
		CollisionHitResult hit(1.f);
		SafeMoveNode(delta, rotation, true, &hit);

		// Check result of forward movement
		if (hit.bBlockingHit)
		{
			if (hit.bStartPenetrating)
			{
				// Undo movement
				scopedStepUpMovement.RevertMove();
				return false;
			}

			// If we hit something above us and also something ahead of us, we should notify about the upward hit as well.
			// The forward hit will be handled later (in the bSteppedOver case below).
			// In the case of hitting something above but not forward, we are not blocked from moving so we don't need the notification.
			if (sweepUpHit.bBlockingHit && hit.bBlockingHit)
			{
				HandleImpact(sweepUpHit);
			}

			// pawn ran into a wall
			HandleImpact(hit);
			if (IsFalling())
			{
				return true;
			}

			// adjust and try again
			const float forwardHitTime = hit.Time;
			const float forwardSlideAmount = SlideAlongSurface(delta, 1.f - hit.Time, hit.Normal, hit, true);

			if (IsFalling())
			{
				scopedStepUpMovement.RevertMove();
				return false;
			}

			// If both the forward hit and the deflection got us nowhere, there is no point in this step up.
			if (forwardHitTime == 0.f && forwardSlideAmount == 0.f)
			{
				scopedStepUpMovement.RevertMove();
				return false;
			}
		}

		// Step down
		SafeMoveNode(gravDir * stepTravelDownHeight, GetUpdatedNode().GetOrientation(), true, &hit);

		// If step down was initially penetrating abort the step-up
		if (hit.bStartPenetrating)
		{
			scopedStepUpMovement.RevertMove();
			return false;
		}

		StepDownResult stepDownResult;
		if (hit.IsValidBlockingHit())
		{
			// See if this step sequence had allowed us to travel higher than our max step height allows.
			const float deltaZ = (hit.ImpactPoint | -gravDir) - floorPointY;
			if (deltaZ > m_maxStepHeight)
			{
				scopedStepUpMovement.RevertMove();
				return false;
			}

			// Reject unwalkable surface normals here.
			if (!IsWalkable(hit))
			{
				// Reject if normal opposes movement direction
				const bool bNormalTowardsMe = (delta | hit.ImpactNormal) < 0.f;
				if (bNormalTowardsMe)
				{
					scopedStepUpMovement.RevertMove();
					return false;
				}

				// Also reject if we would end up being higher than our starting location by stepping down.
				// It's fine to step down onto an unwalkable normal below us, we will just slide off. Rejecting those moves would prevent us from being able to walk off the edge.
				if ((hit.Location | -gravDir) > oldLocationY)
				{
					scopedStepUpMovement.RevertMove();
					return false;
				}
			}

			// Reject moves where the downward sweep hit something very close to the edge of the capsule. This maintains consistency with FindFloor as well.
			if (!IsWithinEdgeTolerance(hit.Location, hit.ImpactPoint, pawnRadius))
			{
				scopedStepUpMovement.RevertMove();
				return false;
			}

			// Don't step up onto invalid surfaces if traveling higher.
			if (deltaZ > 0.f && !CanStepUp(hit))
			{
				scopedStepUpMovement.RevertMove();
				return false;
			}

			// See if we can validate the floor as a result of this step down. In almost all cases this should succeed, and we can avoid computing the floor outside this method.
			if (outStepDownResult != nullptr)
			{
				FindFloor(GetUpdatedNode().GetPosition(), stepDownResult.FloorResult, &hit);

				// Reject unwalkable normals if we end up higher than our initial height.
				// It's fine to walk down onto an unwalkable surface, don't reject those moves.
				if ((hit.Location | -gravDir) > oldLocationY)
				{
					// We should reject the floor result if we are trying to step up an actual step where we are not able to perch (this is rare).
					// In those cases we should instead abort the step-up and try to slide along the stair.
					if (!stepDownResult.FloorResult.bValidFloor && stepSideY < 0.08f /*MAX_STEP_SIDE_Y*/)
					{
						scopedStepUpMovement.RevertMove();
						return false;
					}
				}

				stepDownResult.bComputedFloor = true;
			}
		}

		// Copy step down result.
		if (outStepDownResult != nullptr)
		{
			*outStepDownResult = stepDownResult;
		}

		// Don't recalculate velocity based on this height adjustment, if considering vertical adjustments.
		m_justTeleported |= !m_maintainHorizontalGroundVelocity;

		return true;
	}


	Vector3 UnitMovement::GetGravitySpaceComponentY(const Vector3& vector) const
	{
		return GetGravityDirection() * vector.Dot(GetGravityDirection());
	}

	Vector3 UnitMovement::LimitAirControl(const Vector3& fallAcceleration, const CollisionHitResult& hitResult, const bool checkForValidLandingSpot) const
	{
		Vector3 result(fallAcceleration);

		if (hitResult.IsValidBlockingHit() && GetGravitySpaceY(hitResult.Normal) > 0.001f /*VERTICAL_SLOPE_NORMAL_Y*/)
		{
			if (!checkForValidLandingSpot || !IsValidLandingSpot(hitResult.Location, hitResult))
			{
				// If acceleration is into the wall, limit contribution.
				if (fallAcceleration.Dot(hitResult.Normal) < 0.f)
				{
					// Allow movement parallel to the wall, but not into it because that may push us up.
					const Vector3 normal2D = ProjectToGravityFloor(hitResult.Normal).NormalizedCopy();
					result = Vector3::VectorPlaneProject(fallAcceleration, normal2D);
				}
			}
		}
		else if (hitResult.bStartPenetrating)
		{
			// Allow movement out of penetration.
			return (result.Dot(hitResult.Normal) > 0.f ? result : Vector3::Zero);
		}

		return result;
	}

	bool UnitMovement::ShouldCheckForValidLandingSpot(const CollisionHitResult& hit) const
	{
		// See if we hit an edge of a surface on the lower portion of the capsule.
		// In this case the normal will not equal the impact normal, and a downward sweep may find a walkable surface on top of the edge.
		if (GetGravitySpaceY(hit.Normal) > 1.e-4f && !hit.Normal.IsNearlyEqual(hit.ImpactNormal))
		{
			const Vector3 pawnLocation = GetUpdatedNode().GetPosition();
			if (IsWithinEdgeTolerance(pawnLocation, hit.ImpactPoint, 0.35f))
			{
				return true;
			}
		}

		return false;
	}

	void UnitMovement::ProcessLanded(const CollisionHitResult& hit, const float remainingTime, const int32 iterations)
	{
		m_movedUnit.OnLanded();

		if (IsFalling())
		{
			SetPostLandedPhysics(hit);
		}

		RunSimulation(remainingTime, iterations);
	}

	void UnitMovement::SetPostLandedPhysics(const CollisionHitResult& hit)
	{
		if (CanEverSwim() && IsInWater())
		{
			SetMovementMode(MovementMode::Swimming);
		}
		else
		{
			SetMovementMode(MovementMode::Walking);
		}
	}

	bool UnitMovement::IsValidLandingSpot(const Vector3& capsuleLocation, const CollisionHitResult& hit) const
	{
		if (!hit.bBlockingHit)
		{
			return false;
		}

		// Skip some checks if penetrating. Penetration will be handled by the FindFloor call (using a smaller capsule)
		if (!hit.bStartPenetrating)
		{
			// Reject unwalkable floor normals.
			if (!IsWalkable(hit))
			{
				return false;
			}

			constexpr float pawnRadius = 0.35f;

			// Reject hits that are above our lower hemisphere (can happen when sliding down a vertical surface).
			const float lowerHemisphereY = GetGravitySpaceY(hit.Location);
			if (GetGravitySpaceY(hit.ImpactPoint) >= lowerHemisphereY)
			{
				return false;
			}

			// Reject hits that are barely on the cusp of the radius of the capsule
			if (!IsWithinEdgeTolerance(hit.Location, hit.ImpactPoint, pawnRadius))
			{
				return false;
			}
		}
		else
		{
			// Penetrating
			if (GetGravitySpaceY(hit.Normal) < 1.e-4f)
			{
				// Normal is nearly horizontal or downward, that's a penetration adjustment next to a vertical or overhanging wall. Don't pop to the floor.
				return false;
			}
		}

		FindFloorResult floorResult;
		FindFloor(capsuleLocation, floorResult, &hit);

		if (!floorResult.IsWalkableFloor())
		{
			return false;
		}

		return true;
	}

	bool UnitMovement::IsWithinEdgeTolerance(const Vector3& capsuleLocation, const Vector3& testImpactPoint, const float capsuleRadius) const
	{
		const float distFromCenterSq = ProjectToGravityFloor(testImpactPoint - capsuleLocation).GetSquaredLength();
		const float reducedRadius = std::max(SWEEP_EDGE_REJECT_DISTANCE + 1.e-4f, capsuleRadius - SWEEP_EDGE_REJECT_DISTANCE);
		return distFromCenterSq < (reducedRadius * reducedRadius);
	}

	void UnitMovement::HandleFlying(float deltaTime, int32 iterations)
	{
		// TODO
		SetMovementMode(MovementMode::Falling);
	}

	Vector3 UnitMovement::NewFallVelocity(const Vector3& initialVelocity, const Vector3& gravity, const float deltaTime)
	{
		Vector3 result = initialVelocity;

		if (deltaTime > 0.f)
		{
			// Apply gravity.
			result += gravity * deltaTime;

			// Don't exceed terminal velocity.
			const float terminalLimit = std::abs(55.0f);
			if (result.GetSquaredLength() > terminalLimit * terminalLimit)
			{
				const Vector3 gravityDir = gravity.NormalizedCopy();
				if ((result | gravityDir) > terminalLimit)
				{
					result = Vector3::PointPlaneProject(result, Vector3::Zero, gravityDir) + gravityDir * terminalLimit;
				}
			}
		}

		return result;
	}

	float UnitMovement::GetGravityY() const
	{
		return -9.8f * m_gravityScale;
	}

	Vector3 UnitMovement::GetFallingLateralAcceleration(const float deltaTime) const
	{
		// No acceleration in Y
		Vector3 fallAcceleration = ProjectToGravityFloor(m_acceleration);

		// bound acceleration, falling object has minimal ability to impact acceleration
		if (fallAcceleration.GetSquaredLength() > 0.f)
		{
			fallAcceleration = GetAirControl(m_airControl, fallAcceleration);
			fallAcceleration = fallAcceleration.GetClampedToMaxSize(GetMaxAcceleration());
		}

		return fallAcceleration;
	}

	float UnitMovement::BoostAirControl(float tickAirControl) const
	{
		// Allow a burst of initial acceleration
		if (m_airControlBoostMultiplier > 0.f && ProjectToGravityFloor(m_velocity).GetSquaredLength() <
			m_airControlBoostVelocityThreshold * m_airControlBoostVelocityThreshold)
		{
			tickAirControl = std::min(1.f, m_airControlBoostMultiplier * tickAirControl);
		}

		return tickAirControl;
	}

	bool UnitMovement::SafeMoveNode(const Vector3& delta, const Quaternion& newRotation, bool bSweep,
	                                CollisionHitResult* outHit, const CollisionParams& params) const
	{
		// Set up
		const Vector3 traceStart = GetUpdatedNode().GetPosition();
		const Vector3 traceEnd = traceStart + delta;
		float DeltaSizeSq = (traceEnd - traceStart).GetSquaredLength();
		// Recalc here to account for precision loss of float addition
		const Quaternion initialRotationQuat = GetUpdatedNode().GetOrientation();

		constexpr float smallNumber = 4.f * 1.e-4f;

		// ComponentSweepMulti does nothing if moving < KINDA_SMALL_NUMBER in distance, so it's important to not try to sweep distances smaller than that. 
		const float minMovementDistSq = (bSweep ? smallNumber * smallNumber : 0.f);
		if (DeltaSizeSq <= minMovementDistSq)
		{
			// Skip if no vector or rotation.
			if (newRotation.Equals(initialRotationQuat, Radian(1.e-8f)))
			{
				// copy to optional output param
				if (outHit)
				{
					outHit->Init(traceStart, traceEnd);
				}
				return true;
			}
			DeltaSizeSq = 0.f;
		}

		// WARNING: HitResult is only partially initialized in some paths. All data is valid only if bFilledHitResult is true.
		CollisionHitResult blockingHit;
		blockingHit.bBlockingHit = false;
		blockingHit.Time = 1.f;
		bool bFilledHitResult = false;
		bool bMoved = false;
		bool bIncludesOverlapsAtEnd = false;
		bool bRotationOnly = false;

		if (!bSweep)
		{
			// not sweeping, just go directly to the new transform
			GetUpdatedNode().SetPosition(traceEnd);
			GetUpdatedNode().SetOrientation(newRotation);
			bMoved = true;
			bRotationOnly = (DeltaSizeSq == 0);
		}
		else
		{
			std::vector<CollisionHitResult> hits;

			// Perform the sweep using our SweepMultiCast implementation
			const int32 numHits = SweepMultiCast(traceStart, traceEnd, hits, params);
			const bool bHadBlockingHit = (numHits > 0 && CollisionHitResult::GetFirstBlockingHit(hits) != nullptr);

			if (!hits.empty())
			{
				const float deltaSize = std::sqrt(DeltaSizeSq);
				for (auto& hit : hits)
				{
					// Pull back slightly to prevent precision problems
					if (hit.bBlockingHit && hit.Time > 0.0f)
					{
						const float pullBack = std::max(SWEEP_EDGE_REJECT_DISTANCE, deltaSize * 0.01f);
						hit.Time = std::max(0.0f, hit.Time - (pullBack / deltaSize));
						hit.Location = hit.TraceStart + (hit.TraceEnd - hit.TraceStart) * hit.Time;
						hit.Distance = (hit.Location - hit.TraceStart).GetLength();
					}
				}

				// Find the first blocking hit
				CollisionHitResult* firstBlockingHit = CollisionHitResult::GetFirstBlockingHit(hits);
				if (firstBlockingHit)
				{
					blockingHit = *firstBlockingHit;
					bFilledHitResult = true;
				}
			}

			if (bHadBlockingHit)
			{
				// Move to the blocking hit location
				Vector3 newLocation = blockingHit.Location;
				GetUpdatedNode().SetPosition(newLocation);
				GetUpdatedNode().SetOrientation(newRotation);
				bMoved = true;
			}
			else
			{
				// No blocking hit, move to the end position
				GetUpdatedNode().SetPosition(traceEnd);
				GetUpdatedNode().SetOrientation(newRotation);
				bMoved = true;
			}
		}

		// copy to optional output param
		if (outHit)
		{
			if (bFilledHitResult)
			{
				*outHit = blockingHit;
			}
			else
			{
				outHit->Init(traceStart, traceEnd);
			}
		}

		// Return whether we moved at all.
		return bMoved;
	}

	bool UnitMovement::OverlapTest(const Vector3& position, const CollisionParams& params) const
	{
		Scene& scene = GetUpdatedNode().GetScene();
		
		// Create capsules for the sweep
		const Capsule startCapsule = CreateCapsuleAtPosition(position);

		// Create a bounding box that encompasses the entire sweep
		const AABB& sweepBounds = startCapsule.GetBounds();

		// Query the scene for potential collision candidates
		const auto query = scene.CreateAABBQuery(sweepBounds);
		if (query)
		{
			query->SetQueryMask(params.QueryMask);
			query->Execute(*query);

			const auto& queryResult = query->GetLastResult();
			for (const auto& movable : queryResult)
			{
				const ICollidable* collidable = movable->GetCollidable();
				if (!collidable || !collidable->IsCollidable())
				{
					continue;
				}

				// Perform swept collision test against this collidable
				std::vector<CollisionResult> collisionResults;

				if (collidable->TestCapsuleCollision(startCapsule, collisionResults))
				{
					return true;
				}
			}
		}

		return false;
	}

	int32 UnitMovement::SweepMultiCast(const Vector3& start, const Vector3& end,
	                                   std::vector<CollisionHitResult>& outHits, const CollisionParams& params,
	                                   const HitResultCallback* callback) const
	{
		outHits.clear();

		Scene& scene = GetUpdatedNode().GetScene();
		const Vector3 sweepVector = end - start;
		const float sweepDistance = sweepVector.GetLength();

		// Early exit for zero-distance sweeps
		if (sweepDistance < 1e-6f)
		{
			return 0;
		}

		// Create capsules for the sweep
		const Capsule startCapsule = CreateCapsuleAtPosition(start);
		const Capsule endCapsule = CreateCapsuleAtPosition(end);

		// Create a bounding box that encompasses the entire sweep
		AABB sweepBounds = startCapsule.GetBounds();
		sweepBounds.Combine(endCapsule.GetBounds());

		// Query the scene for potential collision candidates
		const auto query = scene.CreateAABBQuery(sweepBounds);
		if (query)
		{
			query->SetQueryMask(params.QueryMask);
			query->Execute(*query);

			const auto& queryResult = query->GetLastResult();
			for (const auto& movable : queryResult)
			{
				const ICollidable* collidable = movable->GetCollidable();
				if (!collidable || !collidable->IsCollidable())
				{
					continue;
				}

				// Perform swept collision test against this collidable
				std::vector<CollisionResult> collisionResults;
				if (SweepCapsuleAgainstCollidable(startCapsule, endCapsule, collidable, collisionResults))
				{
					// Convert collision results to FHitResults
					for (const auto& collisionResult : collisionResults)
					{
						CollisionHitResult hitResult = ConvertCollisionToHitResult(
							collisionResult, start, end, collisionResult.distance);

						// Apply callback if provided
						if (callback && !(*callback)(hitResult))
						{
							// Early exit requested by callback
							break;
						}

						outHits.push_back(hitResult);

						// Check if we should stop collecting hits
						if (params.bFindClosestOnly && hitResult.bBlockingHit)
						{
							break;
						}

						if (params.MaxHits > 0 && outHits.size() >= params.MaxHits)
						{
							break;
						}
					}
				}

				// Early exit if we found enough hits
				if (params.MaxHits > 0 && outHits.size() >= params.MaxHits)
				{
					break;
				}
			}
		}

		// Sort hits by time/distance (closest first)
		std::sort(outHits.begin(), outHits.end(), [](const CollisionHitResult& a, const CollisionHitResult& b)
		{
			return a.Time < b.Time;
		});

		return static_cast<int32>(outHits.size());
	}

	bool UnitMovement::SweepSingleCast(const Vector3& start, const Vector3& end, CollisionHitResult& outHit, const CollisionParams& params) const
	{
		std::vector<CollisionHitResult> hits;
		CollisionParams singleParams = params;
		singleParams.bFindClosestOnly = true;

		const int32 numHits = SweepMultiCast(start, end, hits, singleParams);

		if (numHits > 0)
		{
			// Find the first blocking hit, or just the first hit if no blocking hits
			const CollisionHitResult* blockingHit = CollisionHitResult::GetFirstBlockingHit(hits);
			outHit = blockingHit ? *blockingHit : hits[0];
			return true;
		}

		// No hits found
		outHit.Init(start, end);
		return false;
	}

	bool UnitMovement::DoJump()
	{
		if (m_movedUnit.CanJump())
		{
			const bool bFirstJump = (m_movedUnit.GetJumpCurrentCountPreJump() == 0);
			if (bFirstJump || m_dontFallBelowJumpZVelocityDuringJump)
			{
				m_velocity.y = std::max(m_velocity.y, m_jumpYVelocity);
			}

			SetMovementMode(MovementMode::Falling);
			return true;
		}

		return false;
	}

	void UnitMovement::SetWalkableFloorY(const float walkableFloorY)
	{
		m_walkableFloorY = Clamp(walkableFloorY, 0.f, 1.f);
		m_walkableFloorAngle = Radian(std::acos(m_walkableFloorY));
	}

	void UnitMovement::SetWalkableFloorAngle(const Radian& walkableFloorAngle)
	{
		m_walkableFloorAngle = Degree(Clamp(walkableFloorAngle.GetValueDegrees(), 0.f, 90.0f));
		m_walkableFloorY = std::cos(m_walkableFloorAngle.GetValueRadians());
	}

	Capsule UnitMovement::CreateCapsuleAtPosition(const Vector3& position) const
	{
		const float radius = m_movedUnit.GetCollider().GetRadius();
		constexpr float halfHeight = 0.65f; // From GameUnitC::UpdateCollider

		return {
			position + Vector3(0.0f, radius, 0.0f),
			position + Vector3(0.0f, radius + halfHeight * 2.0f, 0.0f),
			radius
		};
	}

	CollisionHitResult UnitMovement::ConvertCollisionToHitResult(const CollisionResult& collisionRes, const Vector3& traceStart, const Vector3& traceEnd, const float hitTime)
	{
		CollisionHitResult result;
		result.Init(traceStart, traceEnd);

		// Set hit information
		result.bBlockingHit = collisionRes.hasCollision;
		result.bStartPenetrating = collisionRes.distance == 0 && collisionRes.hasCollision;
		result.Time = hitTime;
		result.Distance = hitTime * (traceEnd - traceStart).GetLength();

		if (collisionRes.hasCollision)
		{
			// Set hit location and impact point
			result.Location = traceStart + (traceEnd - traceStart) * hitTime;
			result.ImpactPoint = result.Location; //CollisionRes.contactPoint;

			// Set normals
			result.ImpactNormal = collisionRes.contactNormal;
			result.Normal = collisionRes.contactNormal; // For capsule sweeps, these are typically the same
		}

		return result;
	}

	bool UnitMovement::SweepCapsuleAgainstCollidable(const Capsule& startCapsule, const Capsule& endCapsule, const ICollidable* collidable, std::vector<CollisionResult>& collisionResults) const
	{
		// First test starting position for penetration
		std::vector<CollisionResult> startResults;
		if (collidable->TestCapsuleCollision(startCapsule, startResults))
		{
			for (auto& result : startResults)
			{
				result.distance = 0.0f;
				// Mark as penetrating start
			}
			collisionResults = startResults;
			return true;
		}

		// Handle zero-distance case
		const float totalDist = (endCapsule.GetPointA() - startCapsule.GetPointA()).GetLength();
		if (totalDist < 0.0001f)
		{
			return collidable->TestCapsuleCollision(startCapsule, collisionResults);
		}

		// Use binary search or continuous collision detection instead of fixed steps
		float minT = 0.0f;
		float maxT = 1.0f;
		const int maxIterations = 20;

		for (int iter = 0; iter < maxIterations; ++iter)
		{
			const float midT = (minT + maxT) * 0.5f;
			const Vector3 testPos = startCapsule.GetPointA() + (endCapsule.GetPointA() - startCapsule.GetPointA()) * midT;

			const Capsule testCapsule(
				testPos,
				testPos + (startCapsule.GetPointB() - startCapsule.GetPointA()),
				startCapsule.GetRadius()
			);

			std::vector<CollisionResult> testResults;
			if (collidable->TestCapsuleCollision(testCapsule, testResults))
			{
				maxT = midT;
				for (auto& result : testResults)
				{
					result.distance = midT;
				}
				collisionResults = testResults;
			}
			else
			{
				minT = midT;
			}

			if (maxT - minT < 0.001f) break;
		}

		return !collisionResults.empty();
	}

	float UnitMovement::GetPerchRadiusThreshold() const
	{
		// Don't allow negative values.
		return std::max(0.f, m_perchRadiusThreshold);
	}


	float UnitMovement::GetValidPerchRadius() const
	{
		constexpr float pawnRadius = 0.35f;
		return Clamp(pawnRadius - GetPerchRadiusThreshold(), 0.0011f, pawnRadius);
	}

	bool UnitMovement::ShouldComputePerchResult(const CollisionHitResult& inHit, const bool bCheckRadius) const
	{
		if (!inHit.IsValidBlockingHit())
		{
			return false;
		}

		// Don't try to perch if the edge radius is very small.
		if (GetPerchRadiusThreshold() <= SWEEP_EDGE_REJECT_DISTANCE)
		{
			return false;
		}

		if (bCheckRadius)
		{
			const float distFromCenterSq = ProjectToGravityFloor(inHit.ImpactPoint - inHit.Location).GetSquaredLength();
			const float standOnEdgeRadius = GetValidPerchRadius();
			if (distFromCenterSq <= standOnEdgeRadius * standOnEdgeRadius)
			{
				// Already within perch radius.
				return false;
			}
		}

		return true;
	}

	Vector3 UnitMovement::GetAirControl(float tickAirControl, const Vector3& fallAcceleration) const
	{
		if (tickAirControl != 0.f)
		{
			tickAirControl = BoostAirControl(tickAirControl);
		}

		return fallAcceleration * tickAirControl;
	}

	bool UnitMovement::ShouldLimitAirControl(const Vector3& fallAcceleration) const
	{
		return (ProjectToGravityFloor(fallAcceleration).GetSquaredLength() > 0.f);
	}

	bool UnitMovement::ComputePerchResult(const float testRadius, const CollisionHitResult& inHit, const float inMaxFloorDist, FindFloorResult& outPerchFloorResult) const
	{
		if (inMaxFloorDist <= 0.f)
		{
			return false;
		}

		constexpr float pawnRadius = 0.35f;
		// Sweep further than actual requested distance, because a reduced capsule radius means we could miss some hits that the normal radius would contact.
		constexpr float pawnHalfHeight = 1.0f;
		const Vector3 capsuleLocation = inHit.Location;

		const float inHitAboveBase = std::max<float>(
			0.f, GetGravitySpaceY(inHit.ImpactPoint - capsuleLocation) + pawnHalfHeight);
		const float perchLineDist = std::max(0.f, inMaxFloorDist - inHitAboveBase);
		const float perchSweepDist = std::max(0.f, inMaxFloorDist);

		const float actualSweepDist = perchSweepDist + pawnRadius;
		ComputeFloorDist(capsuleLocation, perchLineDist, actualSweepDist, outPerchFloorResult, testRadius);

		if (!outPerchFloorResult.IsWalkableFloor())
		{
			return false;
		}

		if (inHitAboveBase + outPerchFloorResult.FloorDistance > inMaxFloorDist)
		{
			// Hit something past max distance
			outPerchFloorResult.bWalkableFloor = false;
			return false;
		}

		return true;
	}

	void UnitMovement::FindFloor(const Vector3& capsuleLocation, FindFloorResult& floorResult,
	                             const CollisionHitResult* downwardSweepResult) const
	{
		const float heightCheckAdjust = (IsMovingOnGround() ? MAX_FLOOR_DIST + 1.e-4f : -MAX_FLOOR_DIST);

		const float floorSweepTraceDist = std::max(MAX_FLOOR_DIST, m_maxStepHeight + heightCheckAdjust);
		const float floorLineTraceDist = floorSweepTraceDist;

		// Sweep floor
		if (floorLineTraceDist > 0.f || floorSweepTraceDist > 0.f || m_justTeleported)
		{
			ComputeFloorDist(capsuleLocation, floorLineTraceDist, floorSweepTraceDist, floorResult, 0.35f,
			                 downwardSweepResult);
		}

		// OutFloorResult.HitResult is now the result of the vertical floor check.
		// See if we should try to "perch" at this location.
		if (floorResult.bValidFloor && !floorResult.bLineTrace)
		{
			constexpr bool bCheckRadius = true;
			if (ShouldComputePerchResult(floorResult.HitResult, bCheckRadius))
			{
				float maxPerchFloorDist = std::max(MAX_FLOOR_DIST, m_maxStepHeight + heightCheckAdjust);
				if (IsMovingOnGround())
				{
					maxPerchFloorDist += std::max(0.f, m_perchAdditionalHeight);
				}

				FindFloorResult perchFloorResult;
				if (ComputePerchResult(GetValidPerchRadius(), floorResult.HitResult, maxPerchFloorDist, perchFloorResult))
				{
					// Don't allow the floor distance adjustment to push us up too high, or we will move beyond the perch distance and fall next time.
					constexpr float avgFloorDist = (MIN_FLOOR_DIST + MAX_FLOOR_DIST) * 0.5f;
					const float moveUpDist = (avgFloorDist - floorResult.FloorDistance);
					if (moveUpDist + perchFloorResult.FloorDistance >= maxPerchFloorDist)
					{
						floorResult.FloorDistance = avgFloorDist;
					}

					// If the regular capsule is on an unwalkable surface but the perched one would allow us to stand, override the normal to be one that is walkable.
					if (!floorResult.bWalkableFloor)
					{
						// Floor distances are used as the distance of the regular capsule to the point of collision, to make sure AdjustFloorHeight() behaves correctly.
						floorResult.SetFromLineTrace(perchFloorResult.HitResult, floorResult.FloorDistance,
						                             std::max(floorResult.FloorDistance, MIN_FLOOR_DIST), true);
					}
				}
				else
				{
					// We had no floor (or an invalid one because it was unwalkable), and couldn't perch here, so invalidate floor (which will cause us to start falling).
					floorResult.bWalkableFloor = false;
				}
			}
		}
	}

	void UnitMovement::ComputeFloorDist(const Vector3& capsuleLocation, const float lineDistance, const float sweepDistance,
	                                    FindFloorResult& outFloorResult, const float sweepRadius,
	                                    const CollisionHitResult* downwardSweepResult) const
	{
		outFloorResult.Clear();

		constexpr float pawnRadius = 0.35f;
		constexpr float pawnHalfHeight = 1.0f;

		bool skipSweep = false;
		if (downwardSweepResult != nullptr && downwardSweepResult->IsValidBlockingHit())
		{
			// Only if the supplied sweep was vertical and downward.
			const bool bIsDownward = GetGravitySpaceY(downwardSweepResult->TraceStart - downwardSweepResult->TraceEnd) > 0;
			const bool bIsVertical = ProjectToGravityFloor(downwardSweepResult->TraceStart - downwardSweepResult->TraceEnd).GetSquaredLength() <= 1.e-4f;

			if (bIsDownward && bIsVertical)
			{
				// Reject hits that are barely on the cusp of the radius of the capsule
				if (IsWithinEdgeTolerance(downwardSweepResult->Location, downwardSweepResult->ImpactPoint, pawnRadius))
				{
					// Don't try a redundant sweep, regardless of whether this sweep is usable.
					skipSweep = true;

					const bool isWalkable = IsWalkable(*downwardSweepResult);
					const float floorDist = GetGravitySpaceY(capsuleLocation - downwardSweepResult->Location);
					outFloorResult.SetFromSweep(*downwardSweepResult, floorDist, isWalkable);

					if (isWalkable)
					{
						// Use the supplied downward sweep as the floor hit result.			
						return;
					}
				}
			}
		}

		// We require the sweep distance to be >= the line distance, otherwise the HitResult can't be interpreted as the sweep result.
		if (sweepDistance < lineDistance)
		{
			ASSERT(sweepDistance >= lineDistance);
			return;
		}

		bool bBlockingHit = false;

		// Sweep test
		if (!skipSweep && sweepDistance > 0.f && sweepRadius > 0.f)
		{
			// Use a shorter height to avoid sweeps giving weird results if we start on a surface.
			// This also allows us to adjust out of penetrations.
			constexpr float shrinkScale = 0.9f;
			constexpr float shrinkHeight = (pawnHalfHeight - pawnRadius) * (1.f - shrinkScale);
			const float traceDist = sweepDistance + shrinkHeight;

			CollisionHitResult hit(1.f);
			bBlockingHit = SweepSingleCast(capsuleLocation, capsuleLocation + GetGravityDirection() * traceDist, hit);

			if (bBlockingHit)
			{
				// Reduce hit distance by ShrinkHeight because we shrank the capsule for the trace.
				// We allow negative distances here, because this allows us to pull out of penetrations.
				constexpr float maxPenetrationAdjust = std::max(MAX_FLOOR_DIST, pawnRadius);
				const float sweepResult = std::max(-maxPenetrationAdjust, hit.Time * traceDist - shrinkHeight);

				outFloorResult.SetFromSweep(hit, sweepResult, false);
				if (hit.IsValidBlockingHit() && IsWalkable(hit))
				{
					if (sweepResult <= sweepDistance)
					{
						// Hit within test distance.
						outFloorResult.bWalkableFloor = true;
						return;
					}
				}
			}
		}

		// Since we require a longer sweep than line trace, we don't want to run the line trace if the sweep missed everything.
		// We do however want to try a line trace if the sweep was stuck in penetration.
		if (!outFloorResult.bValidFloor && !outFloorResult.HitResult.bStartPenetrating)
		{
			outFloorResult.FloorDistance = sweepDistance;
			return;
		}

		// Line trace
		if (lineDistance > 0.f)
		{
		}

		// No hits were acceptable.
		outFloorResult.bWalkableFloor = false;
	}

	bool UnitMovement::IsWalkable(const CollisionHitResult& hit) const
	{
		if (!hit.IsValidBlockingHit())
		{
			return false;
		}

		// Never walk up vertical surfaces.
		const float impactNormalY = GetGravitySpaceY(hit.ImpactNormal);
		if (impactNormalY < 1.e-4f)
		{
			return false;
		}

		const float testWalkableY = m_walkableFloorY;

		// Can't walk on this surface if it is too steep.
		if (impactNormalY < testWalkableY)
		{
			return false;
		}

		return true;
	}

	void UnitMovement::AdjustFloorHeight()
	{
		if (!m_currentFloor.IsWalkableFloor())
		{
			return;
		}

		float oldFloorDist = m_currentFloor.FloorDistance;
		if (m_currentFloor.bLineTrace)
		{
			if (oldFloorDist < MIN_FLOOR_DIST && m_currentFloor.LineDist >= MIN_FLOOR_DIST)
			{
				// This would cause us to scale unwalkable walls
				return;
			}

			// Falling back to a line trace means the sweep was unwalkable (or in penetration). Use the line distance for the vertical adjustment.
			oldFloorDist = m_currentFloor.LineDist;
		}

		// Move up or down to maintain floor height.
		if (oldFloorDist < MIN_FLOOR_DIST || oldFloorDist > MAX_FLOOR_DIST)
		{
			CollisionHitResult adjustHit(1.f);
			const float initialY = GetGravitySpaceY(GetUpdatedNode().GetPosition());
			constexpr float avgFloorDist = (MIN_FLOOR_DIST + MAX_FLOOR_DIST) * 0.5f;
			const float moveDist = avgFloorDist - oldFloorDist;
			SafeMoveNode(-GetGravityDirection() * moveDist, GetUpdatedNode().GetOrientation(), true, &adjustHit);

			if (!adjustHit.IsValidBlockingHit())
			{
				m_currentFloor.FloorDistance += moveDist;
			}
			else if (moveDist > 0.f)
			{
				const float currentY = GetGravitySpaceY(GetUpdatedNode().GetPosition());
				m_currentFloor.FloorDistance += currentY - initialY;
			}
			else
			{
				m_currentFloor.FloorDistance = GetGravitySpaceY(GetUpdatedNode().GetPosition() - adjustHit.Location);
				if (IsWalkable(adjustHit))
				{
					m_currentFloor.SetFromSweep(adjustHit, m_currentFloor.FloorDistance, true);
				}
			}

			// Don't recalculate velocity based on this height adjustment, if considering vertical adjustments.
			// Also avoid it if we moved out of penetration
			m_justTeleported |= !m_maintainHorizontalGroundVelocity || (oldFloorDist < 0.f);
		}
	}

	SceneNode& UnitMovement::GetUpdatedNode() const
	{
		return *m_movedUnit.GetSceneNode();
	}
}
