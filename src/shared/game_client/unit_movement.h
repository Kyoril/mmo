#pragma once

#include "base/typedefs.h"
#include "math/vector3.h"
#include "math/quaternion.h"

#include <vector>
#include <functional>

namespace mmo
{
	class SceneNode;
	class GameUnitC;
	class Scene;
	struct CollisionResult;
	class ICollidable;
	struct Capsule;
	class AABB;

	struct MovementProperties
	{
		/// @brief Flag indicating if the unit can jump
		uint8 CanJump : 1;
		/// @brief Flag indicating if the unit can walk
		uint8 CanWalk : 1;
		/// @brief Flag indicating if the unit can swim
		uint8 CanSwim : 1;
		/// @brief Flag indicating if the unit can fly
		uint8 CanFly : 1;

		/// @brief Default constructor with standard movement capabilities
		MovementProperties()
			: CanJump(true)
			, CanWalk(true)
			, CanSwim(false)
			, CanFly(false)
		{
		}
	};

	/// @brief Enumeration of possible movement modes for units
	enum class MovementMode : uint8
	{
		None,		///< No movement mode active
		Walking,	///< Ground-based walking movement
		Falling,	///< Falling through air with gravity
		Swimming,	///< Movement through water/fluid
		Flying		///< Free flight movement ignoring gravity
	};

	struct CollisionHitResult
	{
		/**
		 * 'Time' of impact along trace direction (ranging from 0.0 to 1.0) if there is a hit, indicating time between TraceStart and TraceEnd.
		 * For swept movement (but not queries) this may be pulled back slightly from the actual time of impact, to prevent precision problems with adjacent geometry.
		 */
		float Time;

		/** The distance from the TraceStart to the Location in world space. This value is 0 if there was an initial overlap (trace started inside another colliding object). */
		float Distance;

		/**
		 * The location in world space where the moving shape would end up against the impacted object, if there is a hit. Equal to the point of impact for line tests.
		 * Example: for a sphere trace test, this is the point where the center of the sphere would be located when it touched the other object.
		 * For swept movement (but not queries) this may not equal the final location of the shape since hits are pulled back slightly to prevent precision issues from overlapping another surface.
		 */
		Vector3 Location;

		/**
		 * Location in world space of the actual contact of the trace shape (box, sphere, ray, etc) with the impacted object.
		 * Example: for a sphere trace test, this is the point where the surface of the sphere touches the other object.
		 * @note: In the case of initial overlap (bStartPenetrating=true), ImpactPoint will be the same as Location because there is no meaningful single impact point to report.
		 */
		Vector3 ImpactPoint;

		/**
		 * Normal of the hit in world space, for the object that was swept. Equal to ImpactNormal for line tests.
		 * This is computed for capsules and spheres, otherwise it will be the same as ImpactNormal.
		 * Example: for a sphere trace test, this is a normalized vector pointing in towards the center of the sphere at the point of impact.
		 */
		Vector3 Normal;

		/**
		 * Normal of the hit in world space, for the object that was hit by the sweep, if any.
		 * For example if a sphere hits a flat plane, this is a normalized vector pointing out from the plane.
		 * In the case of impact with a corner or edge of a surface, usually the "most opposing" normal (opposed to the query direction) is chosen.
		 */
		Vector3 ImpactNormal;

		/**
		 * Start location of the trace.
		 * For example if a sphere is swept against the world, this is the starting location of the center of the sphere.
		 */
		Vector3 TraceStart;

		/**
		 * End location of the trace; this is NOT where the impact occurred (if any), but the furthest point in the attempted sweep.
		 * For example if a sphere is swept against the world, this would be the center of the sphere if there was no blocking hit.
		 */
		Vector3 TraceEnd;

		/**
		  * If this test started in penetration (bStartPenetrating is true) and a depenetration vector can be computed,
		  * this value is the distance along Normal that will result in moving out of penetration.
		  * If the distance cannot be computed, this distance will be zero.
		  */
		float PenetrationDepth;

		/** Indicates if this hit was a result of blocking collision. If false, there was no hit or it was an overlap/touch instead. */
		uint8 bBlockingHit : 1;

		/**
		 * Whether the trace started in penetration, i.e. with an initial blocking overlap.
		 * In the case of penetration, if PenetrationDepth > 0.f, then it will represent the distance along the Normal vector that will result in
		 * minimal contact between the swept shape and the object that was hit. In this case, ImpactNormal will be the normal opposed to movement at that location
		 * (ie, Normal may not equal ImpactNormal). ImpactPoint will be the same as Location, since there is no single impact point to report.
		 */
		uint8 bStartPenetrating : 1;

		CollisionHitResult()
		{
			Init();
		}

		explicit CollisionHitResult(const float time)
		{
			Init();
			Time = time;
		}

		explicit CollisionHitResult(const Vector3& start, const Vector3& end)
		{
			Init(start, end);
		}

		/** Initialize empty hit result with given time. */
		inline void Init()
		{
			std::memset(this, 0, sizeof(CollisionHitResult));
			Time = 1.f;
		}

		/** Initialize empty hit result with given time, TraceStart, and TraceEnd */
		inline void Init(const Vector3& start, const Vector3& end)
		{
			std::memset(this, 0, sizeof(CollisionHitResult));
			Time = 1.f;
			TraceStart = start;
			TraceEnd = end;
		}

		/** Reset hit result while optionally saving TraceStart and TraceEnd. */
		inline void Reset(const float time = 1.f, const bool preserveTraceData = true)
		{
			const Vector3 savedTraceStart = TraceStart;
			const Vector3 savedTraceEnd = TraceEnd;
			Init();
			Time = time;
			if (preserveTraceData)
			{
				TraceStart = savedTraceStart;
				TraceEnd = savedTraceEnd;
			}
		}

		/** Return true if there was a blocking hit that was not caused by starting in penetration. */
		inline bool IsValidBlockingHit() const
		{
			return bBlockingHit && !bStartPenetrating;
		}

		/** Static utility function that returns the first 'blocking' hit in an array of results. */
		static CollisionHitResult* GetFirstBlockingHit(std::vector<CollisionHitResult>& hits)
		{
			for (auto& hit : hits)
			{
				if (hit.bBlockingHit)
				{
					return &hit;
				}
			}

			return nullptr;
		}

		/** Static utility function that returns the number of blocking hits in array. */
		static int32 GetNumBlockingHits(const std::vector<CollisionHitResult>& InHits)
		{
			int32 NumBlocks = 0;
			for (int32 HitIdx = 0; HitIdx < InHits.size(); HitIdx++)
			{
				if (InHits[HitIdx].bBlockingHit)
				{
					NumBlocks++;
				}
			}
			return NumBlocks;
		}

		/** Static utility function that returns the number of overlapping hits in array. */
		static int32 GetNumOverlapHits(const std::vector<CollisionHitResult>& InHits)
		{
			return (InHits.size() - GetNumBlockingHits(InHits));
		}

		/**
		 * Get a copy of the HitResult with relevant information reversed.
		 * For example when receiving a hit from another object, we reverse the normals.
		 */
		static CollisionHitResult GetReversedHit(const CollisionHitResult& Hit)
		{
			CollisionHitResult result(Hit);
			result.Normal = -result.Normal;
			result.ImpactNormal = -result.ImpactNormal;

			return result;
		}
	};

	/// @brief Collision parameters for sweep operations
	struct CollisionParams
	{
		/// @brief Query mask to filter what objects to test collision against
		uint32 QueryMask = 0xFFFFFFFF;

		/// @brief Whether to include overlaps (non-blocking hits) in results
		bool bIncludeOverlaps = true;

		/// @brief Whether to find the closest hit only or all hits
		bool bFindClosestOnly = false;

		/// @brief Maximum number of hits to return (0 = unlimited)
		uint32 MaxHits = 0;

		CollisionParams()
			= default;

		explicit CollisionParams(const uint32 queryMask)
			: QueryMask(queryMask)
		{
		}
	};

	/// @brief Structure containing information about the floor beneath the unit
	struct FindFloorResult
	{
		/// @brief Hit result of the floor trace
		CollisionHitResult HitResult;

		/// @brief Distance to the floor from the base of the unit
		float FloorDistance = 0.0f;

		/// @brief True if the floor is walkable based on slope
		bool bWalkableFloor : 1 = false;

		/// @brief True if we found valid floor geometry
		bool bValidFloor : 1 = false;

		/// @brief True if this result came from a line trace rather than a sweep
		bool bLineTrace : 1 = false;

		/// @brief Distance from line trace (used when bLineTrace is true)
		float LineDist = 0.0f;

		/// @brief Default constructor that clears all values
		FindFloorResult()
		{
			Clear();
		}

		/// @brief Clears all floor result data to default values
		void Clear()
		{
			HitResult.Init();
			FloorDistance = 0.0f;
			LineDist = 0.0f;
			bWalkableFloor = false;
			bValidFloor = false;
			bLineTrace = false;
		}

		/// @brief Gets the distance to floor, using appropriate distance based on trace type
		/// @return Distance to floor in world units
		[[nodiscard]] float GetDistanceToFloor() const
		{
			// When the floor distance is set using SetFromSweep, the LineDist value will be reset.
			// However, when SetLineFromTrace is used, there's no guarantee that FloorDist is set.
			return bLineTrace ? LineDist : FloorDistance;
		}

		/// @brief Check if this floor result represents a walkable surface
		/// @return True if the floor is walkable
		[[nodiscard]] bool IsWalkableFloor() const
		{
			return bValidFloor && bWalkableFloor;
		}

		/// @brief Sets floor result data from a sweep operation
		/// @param hit Hit result from the sweep
		/// @param sweepFloorDistance Distance from sweep operation
		/// @param isWalkableFloor Whether the floor is walkable
		void SetFromSweep(const CollisionHitResult& hit, float sweepFloorDistance, bool isWalkableFloor);

		/// @brief Sets floor result data from a line trace operation
		/// @param hit Hit result from the line trace
		/// @param sweepFloorDistance Distance from sweep operation
		/// @param lineDistance Distance from line trace
		/// @param bIsWalkableFloor Whether the floor is walkable
		void SetFromLineTrace(const CollisionHitResult& hit, float sweepFloorDistance, float lineDistance, bool bIsWalkableFloor);
	};

	struct StepDownResult
	{
		/// @brief True if the floor was computed as a result of the step down
		uint32 bComputedFloor : 1;
		/// @brief The result of the floor test if the floor was updated
		FindFloorResult FloorResult;	// The result of the floor test if the floor was updated.

		/// @brief Default constructor initializing computed floor flag to false
		StepDownResult()
			: bComputedFloor(false)
		{
		}
	};

	/// @brief Callback function type for early exit during sweep operations in UnitMovement
	/// @param result The current hit result being processed
	/// @return True to continue sweeping, false to exit early
	using HitResultCallback = std::function<bool(const CollisionHitResult& result)>;

	/// Unit movement component that handles physics-based character movement
	class UnitMovement
	{
	public:
		/// @brief Creates a new UnitMovement instance
		/// @param owner The GameUnitC that owns this movement component
		explicit UnitMovement(GameUnitC& owner);

	public:
		/// @brief Called once per frame to update the movement component
		/// @param deltaSeconds Time elapsed since last update in seconds
		void Tick(float deltaSeconds);

		/// @brief Computes the analog input modifier based on current input state
		/// @return Modifier value for analog input (0.0 to 1.0)
		[[nodiscard]] float ComputeAnalogInputModifier() const;

		/// @brief Performs movement calculations and updates position
		/// @param deltaTime Time step for movement calculations
		void PerformMovement(float deltaTime);

		/// @brief Runs the movement simulation with specified iterations
		/// @param deltaTime Time step for simulation
		/// @param iterations Number of simulation iterations to perform
		void RunSimulation(float deltaTime, int32 iterations);

		/// @brief Sets the current movement mode for the unit
		/// @param newMovementMode The new movement mode to set
		void SetMovementMode(MovementMode newMovementMode);

		void SetVelocity(const Vector3& velocity) { m_velocity = velocity; }

		/// @brief Moves a controlled character based on input vector
		/// @param inputVector Direction and magnitude of desired movement
		/// @param deltaTime Time step for movement
		void ControlledCharacterMove(const Vector3& inputVector, float deltaTime);

		/// @brief Constrains input acceleration to valid movement parameters
		/// @param inputAcceleration Raw input acceleration vector
		/// @return Constrained acceleration vector
		[[nodiscard]] Vector3 ConstrainInputAcceleration(const Vector3& inputAcceleration) const;

		/// @brief Scales input acceleration based on movement settings
		/// @param inputAcceleration Input acceleration to scale
		/// @return Scaled acceleration vector
		[[nodiscard]] Vector3 ScaleInputAcceleration(const Vector3& inputAcceleration) const;

		/// @brief Gets the gravity direction vector
		/// @return Reference to the gravity direction vector
		[[nodiscard]] const Vector3& GetGravityDirection() const
		{ 
			return m_gravityDirection; 
		}

		/// @brief Gets the scene node that is updated by this movement component
		/// @return Reference to the updated scene node
		[[nodiscard]] SceneNode& GetUpdatedNode() const;

	public:
		/// @brief Gets whether jumping is currently allowed for this unit
		/// @return True if jumping is allowed, false otherwise
		[[nodiscard]] bool IsJumpAllowed() const
		{ 
			return m_movementState.CanJump; 
		}

		/// @brief Sets whether jumping is allowed for this unit
		/// @param allowed True to allow jumping, false to disable
		void SetJumpAllowed(const bool allowed) 
		{ 
			m_movementState.CanJump = allowed; 
		}

		/// @brief Checks if the unit is currently moving on the ground. This does return true even without
		///        any velocity (standing still), as long as the unit stands on walkable ground.
		/// @return True if in walking movement mode, false otherwise
		[[nodiscard]] bool IsMovingOnGround() const
		{ 
			return m_movementMode == MovementMode::Walking; 
		}

		/// @brief Checks if the unit is currently falling
		/// @return True if in falling movement mode, false otherwise
		[[nodiscard]] bool IsFalling() const
		{ 
			return m_movementMode == MovementMode::Falling; 
		}

		/// @brief Checks if the unit is currently swimming
		/// @return True if in swimming movement mode, false otherwise
		[[nodiscard]] bool IsSwimming() const
		{ 
			return m_movementMode == MovementMode::Swimming; 
		}

		/// @brief Checks if the unit is currently flying
		/// @return True if in flying movement mode, false otherwise
		[[nodiscard]] bool IsFlying() const
		{ 
			return m_movementMode == MovementMode::Flying; 
		}

		/// @brief Checks if this unit can ever swim (based on capabilities)
		/// @return True if swimming is possible, false otherwise
		/// @note Currently always returns false - TODO: implement swimming capability check
		[[nodiscard]] bool CanEverSwim() const
		{
			// TODO
			return false;
		}

		/// @brief Checks if the unit is currently in water
		/// @return True if in water, false otherwise
		/// @note Currently always returns false - TODO: implement water detection
		[[nodiscard]] bool IsInWater() const
		{
			// TODO
			return false;
		}

		/// @brief Checks if the unit can attempt to jump in its current state
		/// @return True if jump can be attempted, false otherwise
		[[nodiscard]] bool CanAttemptJump() const
		{
			return IsJumpAllowed() &&
				(IsMovingOnGround() || IsFalling()); // Falling included for double-jump and non-zero jump hold time, but validated by character.
		}

		/// @brief Gets the current velocity vector of the unit
		/// @return Reference to the velocity vector
		[[nodiscard]] const Vector3& GetVelocity() const { return m_velocity; }

		/// @brief Gets the current acceleration vector of the unit
		/// @return Reference to the acceleration vector
		[[nodiscard]] const Vector3& GetAcceleration() const { return m_acceleration; }

	public:
		/// @brief Calculates velocity based on input parameters and applies friction
		/// @param deltaTime Time step for velocity calculation
		/// @param friction Friction coefficient to apply
		/// @param fluid Whether the unit is moving through fluid (affects friction)
		/// @param brakingDeceleration Deceleration value for braking
		void CalcVelocity(float deltaTime, float friction, bool fluid, float brakingDeceleration);

		/// @brief Applies velocity braking to slow down the unit
		/// @param deltaTime Time step for braking calculation
		/// @param friction Friction coefficient
		/// @param brakingDeceleration Deceleration value for braking
		void ApplyVelocityBraking(float deltaTime, float friction, float brakingDeceleration);

		/// @brief Checks if the unit is exceeding the specified maximum speed
		/// @param maxSpeed Maximum allowed speed to check against
		/// @return True if current speed exceeds maxSpeed, false otherwise
		[[nodiscard]] bool IsExceedingMaxSpeed(float maxSpeed) const;

		/// @brief Gets the maximum speed for the current movement mode
		/// @return Maximum speed value
		[[nodiscard]] float GetMaxSpeed() const;

		/// @brief Gets the minimum analog input speed threshold
		/// @return Minimum analog speed value
		[[nodiscard]] float GetMinAnalogSpeed() const;

		/// @brief Calculates the maximum jump height based on current settings
		/// @return Maximum achievable jump height
		[[nodiscard]] float GetMaxJumpHeight() const;

		/// @brief Gets the maximum acceleration value
		/// @return Maximum acceleration
		[[nodiscard]] float GetMaxAcceleration() const;

		/// @brief Gets the maximum braking deceleration for the current movement mode
		/// @return Maximum braking deceleration value
		[[nodiscard]] float GetMaxBrakingDeceleration() const;

		/// @brief Gets the current acceleration being applied to the unit
		/// @return Current acceleration vector
		[[nodiscard]] Vector3 GetCurrentAcceleration() const;

		/// @brief Gets the current analog input modifier value
		/// @return Analog input modifier (0.0 to 1.0)
		[[nodiscard]] float GetAnalogInputModifier() const;

		/// @brief Attempts to make the unit jump
		/// @return True if jump was successfully initiated, false otherwise
		bool DoJump();

		/// @brief Sets the walkable floor Y component threshold
		/// @param walkableFloorY Y component value for walkable floor normal
		void SetWalkableFloorY(float walkableFloorY);

		/// @brief Sets the walkable floor angle threshold
		/// @param walkableFloorAngle Maximum angle for walkable surfaces
		void SetWalkableFloorAngle(const Radian& walkableFloorAngle);

		/// @brief Gets the walkable floor Y component threshold
		/// @return Walkable floor Y component value
		[[nodiscard]] float GetWalkableFloorY() const { return m_walkableFloorY; }

		/// @brief Get the walkable floor angle for this unit
		/// @return The walkable floor angle in radians
		[[nodiscard]] const Radian& GetWalkableFloorAngle() const
		{
			return m_walkableFloorAngle;
		}

	protected:
		/// @brief Perform a swept collision test using the unit's capsule shape
		/// @param delta The movement vector to sweep through
		/// @param newRotation The rotation at the end of the sweep
		/// @param bSweep Whether to perform swept collision or just teleport
		/// @param outHit Optional output for hit information
		/// @param params Collision parameters for the sweep
		/// @return True if movement was successful (no blocking hits or hits were resolved)
		bool SafeMoveNode(const Vector3& delta, const Quaternion& newRotation, bool bSweep, CollisionHitResult* outHit, const CollisionParams& params = CollisionParams()) const;
		bool OverlapTest(const Vector3& position, const CollisionParams& params) const;

		/// @brief Sweep the unit's capsule and return all hits (similar to UE5's SweepMultiCast)
		/// @param start Starting position for the sweep
		/// @param end Ending position for the sweep
		/// @param outHits Array to store all hit results
		/// @param params Collision parameters for the sweep
		/// @param callback Optional callback for early exit (can be nullptr)
		/// @return Number of hits found
		int32 SweepMultiCast(const Vector3& start, const Vector3& end, std::vector<CollisionHitResult>& outHits, const CollisionParams& params = CollisionParams(), const HitResultCallback* callback = nullptr) const;

		/// @brief Sweep the unit's capsule and return the first blocking hit
		/// @param start Starting position for the sweep
		/// @param end Ending position for the sweep
		/// @param outHit Output for the closest blocking hit
		/// @param params Collision parameters for the sweep
		/// @return True if a blocking hit was found
		bool SweepSingleCast(const Vector3& start, const Vector3& end, CollisionHitResult& outHit, const CollisionParams& params = CollisionParams()) const;

		/// @brief Handles movement logic when the unit is in walking mode
		/// @param deltaTime Time step for movement calculations
		/// @param iterations Number of movement simulation iterations
		void HandleWalking(float deltaTime, int32 iterations);
		bool ResolvePenetration(const Vector3& Adjustment, const CollisionHitResult& Hit,
		                        const Quaternion& NewRotationQuat);

		/// @brief Checks if the unit should transition to falling state
		/// @param delta Movement delta vector
		/// @param oldLocation Previous position of the unit
		/// @param remainingTime Remaining simulation time
		/// @param timeTick Current time tick
		/// @param iterations Number of simulation iterations
		/// @return True if unit should start falling, false otherwise
		bool CheckFall(const Vector3& delta,
		               const Vector3& oldLocation, float remainingTime, float timeTick, int32 iterations);

		/// @brief Initiates falling state for the unit
		/// @param iterations Number of simulation iterations
		/// @param remainingTime Remaining simulation time
		/// @param timeTick Current time tick
		/// @param delta Movement delta vector
		/// @param subLoc Sub-location for precise positioning
		void StartFalling(int32 iterations, float remainingTime, float timeTick, const Vector3& delta,
		                  const Vector3& subLoc);

		/// @brief Calculates adjustment vector to resolve penetration with collision geometry
		/// @param hit Hit result containing penetration information
		/// @return Adjustment vector to resolve penetration
		static Vector3 GetPenetrationAdjustment(const CollisionHitResult& hit);

		/// @brief Sets the Y component of a vector in gravity space
		/// @param vector Vector to modify
		/// @param y Y value to set in gravity space
		void SetGravitySpaceY(Vector3& vector, float y) const;

		/// @brief Computes movement delta for ground-based movement on slopes and ramps
		/// @param delta Original movement delta
		/// @param rampHit Hit result from ramp/slope collision
		/// @param hitFromLineTrace Whether the hit came from a line trace
		/// @return Adjusted movement delta for ground movement
		[[nodiscard]] Vector3 ComputeGroundMovementDelta(const Vector3& delta, const CollisionHitResult& rampHit,
		                                   bool hitFromLineTrace) const;

		/// @brief Slides the unit along a surface when blocked by collision
		/// @param delta Original movement delta
		/// @param time Parametric time of collision (0.0 to 1.0)
		/// @param inNormal Surface normal to slide along
		/// @param hit Hit result from collision
		/// @param handleImpact Whether to handle impact effects
		/// @return Remaining time after sliding
		float SlideAlongSurface(const Vector3& delta, float time, const Vector3& inNormal, CollisionHitResult& hit,
		                        bool handleImpact);

		/// @brief Adjusts movement when hitting two walls simultaneously
		/// @param worldSpaceDelta Movement delta to adjust (modified in place)
		/// @param hit Current hit result
		/// @param oldHitNormal Normal from previous hit
		void TwoWallAdjust(Vector3& worldSpaceDelta, const CollisionHitResult& hit, const Vector3& oldHitNormal) const;

		/// @brief Computes slide vector along a surface normal
		/// @param delta Original movement vector
		/// @param time Parametric collision time
		/// @param normal Surface normal to slide along
		/// @return Slide vector along the surface
		[[nodiscard]] Vector3 ComputeSlideVector(const Vector3& delta, float time, const Vector3& normal) const;

		/// @brief Handles slope boosting to prevent getting stuck on shallow slopes
		/// @param slideResult Result from initial slide calculation
		/// @param delta Original movement delta
		/// @param time Collision time
		/// @param normal Surface normal
		/// @return Boosted slide result for better slope traversal
		[[nodiscard]] Vector3 HandleSlopeBoosting(const Vector3& slideResult, const Vector3& delta, float time, const Vector3& normal) const;

		/// @brief Handles impact effects when colliding with surfaces
		/// @param impact Hit result from collision
		/// @param timeSlice Time slice for impact processing
		/// @param moveDelta Movement delta that caused the impact
		void HandleImpact(const CollisionHitResult& impact, float timeSlice = 0.0f, const Vector3& moveDelta = Vector3::Zero);

		/// @brief Moves the unit along the floor while maintaining contact
		/// @param inVelocity Input velocity for floor movement
		/// @param deltaSeconds Time step for movement
		/// @param outStepDownResult Optional output for step-down results
		void MoveAlongFloor(const Vector3& inVelocity, float deltaSeconds, StepDownResult* outStepDownResult);

		/// @brief Maintains horizontal velocity when moving on sloped ground
		void MaintainHorizontalGroundVelocity();

		/// @brief Calculates the appropriate simulation time step
		/// @param remainingTime Remaining simulation time
		/// @param iterations Current iteration count
		/// @return Time step for this simulation iteration
		[[nodiscard]] float GetSimulationTimeStep(float remainingTime, int32 iterations) const;

		/// @brief Handles movement logic when the unit is swimming
		/// @param deltaTime Time step for movement calculations
		/// @param iterations Number of simulation iterations
		void HandleSwimming(float deltaTime, int32 iterations);

		/// @brief Handles movement logic when the unit is falling
		/// @param deltaTime Time step for movement calculations
		/// @param iterations Number of simulation iterations
		void HandleFalling(float deltaTime, int32 iterations);

		/// @brief Checks if the unit can step up onto a surface
		/// @param hit Hit result from collision with potential step
		/// @return True if step-up is possible, false otherwise
		[[nodiscard]] bool CanStepUp(const CollisionHitResult& hit) const;

		/// @brief Attempts to step up onto a higher surface
		/// @param gravDir Gravity direction vector
		/// @param delta Movement delta
		/// @param inHit Hit result from collision
		/// @param outStepDownResult Optional output for step-down validation
		/// @return True if step-up was successful, false otherwise
		bool StepUp(const Vector3& gravDir, const Vector3& delta, const CollisionHitResult& inHit,
		            StepDownResult* outStepDownResult);

		/// @brief Gets the Y component of a vector in gravity space
		/// @param vector Input vector
		/// @return Y component in gravity-aligned coordinate system
		[[nodiscard]] Vector3 GetGravitySpaceComponentY(const Vector3& vector) const;

		/// @brief Limits air control when falling to prevent unrealistic movement
		/// @param fallAcceleration Acceleration while falling
		/// @param hitResult Hit result from collision
		/// @param checkForValidLandingSpot Whether to check for valid landing spots
		/// @return Limited air control acceleration
		[[nodiscard]] Vector3 LimitAirControl(const Vector3& fallAcceleration, const CollisionHitResult& hitResult,
		                        bool checkForValidLandingSpot) const;

		/// @brief Determines if we should check for valid landing spots
		/// @param hit Hit result from collision
		/// @return True if landing spot validation should be performed
		[[nodiscard]] bool ShouldCheckForValidLandingSpot(const CollisionHitResult& hit) const;

		/// @brief Processes landing after falling or jumping
		/// @param hit Hit result from landing collision
		/// @param remainingTime Remaining simulation time after landing
		/// @param iterations Number of simulation iterations
		void ProcessLanded(const CollisionHitResult& hit, float remainingTime, int32 iterations);

		/// @brief Sets physics state after landing on a surface
		/// @param hit Hit result from landing collision
		void SetPostLandedPhysics(const CollisionHitResult& hit);

		/// @brief Validates if a location represents a valid landing spot
		/// @param capsuleLocation Location to test for landing
		/// @param hit Hit result from potential landing surface
		/// @return True if the spot is valid for landing, false otherwise
		[[nodiscard]] bool IsValidLandingSpot(const Vector3& capsuleLocation, const CollisionHitResult& hit) const;

		/// @brief Checks if an impact point is within acceptable edge tolerance
		/// @param capsuleLocation Current capsule location
		/// @param testImpactPoint Impact point to test
		/// @param capsuleRadius Radius of the unit's capsule
		/// @return True if within edge tolerance, false otherwise
		[[nodiscard]] bool IsWithinEdgeTolerance(const Vector3& capsuleLocation, const Vector3& testImpactPoint,
		                           float capsuleRadius) const;

		/// @brief Calculates new falling velocity based on gravity and time
		/// @param initialVelocity Starting velocity
		/// @param gravity Gravity vector
		/// @param deltaTime Time step
		/// @return New velocity after applying gravity
		static Vector3 NewFallVelocity(const Vector3& initialVelocity, const Vector3& gravity, float deltaTime);

		/// @brief Handles movement logic when the unit is flying
		/// @param deltaTime Time step for movement calculations
		/// @param iterations Number of simulation iterations
		void HandleFlying(float deltaTime, int32 iterations);

		/// @brief Gets the Y component of gravity
		/// @return Gravity Y component value
		[[nodiscard]] float GetGravityY() const;

		/// @brief Gets lateral acceleration applied while falling
		/// @param deltaTime Time step for acceleration calculation
		/// @return Lateral acceleration vector
		[[nodiscard]] Vector3 GetFallingLateralAcceleration(float deltaTime) const;

		/// @brief Boosts air control when certain conditions are met
		/// @param tickAirControl Base air control value for this tick
		/// @return Boosted air control value
		[[nodiscard]] float BoostAirControl(float tickAirControl) const;

		/// @brief Calculates air control acceleration while falling
		/// @param tickAirControl Air control value for this tick
		/// @param fallAcceleration Current falling acceleration
		/// @return Air control acceleration vector
		[[nodiscard]] Vector3 GetAirControl(float tickAirControl, const Vector3& fallAcceleration) const;

		/// @brief Determines if air control should be limited
		/// @param fallAcceleration Current falling acceleration
		/// @return True if air control should be limited, false otherwise
		[[nodiscard]] bool ShouldLimitAirControl(const Vector3& fallAcceleration) const;

		/// @brief Computes perch result to determine if unit can perch on an edge
		/// @param testRadius Radius to test for perching
		/// @param inHit Hit result from collision
		/// @param inMaxFloorDist Maximum distance to floor for perching
		/// @param outPerchFloorResult Output structure for perch floor information
		/// @return True if perching is possible, false otherwise
		bool ComputePerchResult(float testRadius, const CollisionHitResult& inHit, float inMaxFloorDist, FindFloorResult& outPerchFloorResult) const;

		/// @brief Gets the Y component of a vector in gravity space
		/// @param vector Input vector to get Y component from
		/// @return Y component value in gravity-aligned coordinate system
		[[nodiscard]] float GetGravitySpaceY(const Vector3& vector) const
		{ 
			return vector.Dot(-GetGravityDirection()); 
		}

		/// @brief Projects a vector onto the gravity floor plane
		/// @param vector Vector to project
		/// @return Projected vector on the gravity floor plane
		[[nodiscard]] Vector3 ProjectToGravityFloor(const Vector3& vector) const
		{ 
			return Vector3::VectorPlaneProject(vector, GetGravityDirection()); 
		}

		/// @brief Gets the capsule radius for collision
		/// @return Capsule radius in world units
		[[nodiscard]] float GetCapsuleRadius() const;

		/// @brief Gets the capsule half-height (distance from center to top/bottom sphere centers)
		/// @return Capsule half-height in world units
		[[nodiscard]] float GetCapsuleHalfHeight() const;

		/// @brief Gets the total capsule height from bottom to top
		/// @return Total capsule height in world units
		[[nodiscard]] float GetCapsuleTotalHeight() const;

		/// @brief Create a capsule representing the unit's collision shape at a given position
		/// @note The position represents the BOTTOM CENTER (feet) of the capsule, not the center.
		///       This differs from Unreal Engine which uses center-based positioning.
		/// @param position The feet position to create the capsule at
		/// @return Capsule collision shape
		[[nodiscard]] Capsule CreateCapsuleAtPosition(const Vector3& position) const;

		/// @brief Convert CollisionResult to CollisionHitResult
		/// @param collisionRes The collision result from the collision system
		/// @param traceStart The start position of the trace
		/// @param traceEnd The end position of the trace
		/// @param hitTime The parametric time of the hit (0.0-1.0)
		/// @return Converted CollisionHitResult
		static CollisionHitResult ConvertCollisionToHitResult(const CollisionResult& collisionRes, const Vector3& traceStart, const Vector3& traceEnd, float hitTime);

		/// @brief Sweep a capsule against a specific collidable object
		/// @param startCapsule Starting capsule configuration
		/// @param endCapsule Ending capsule configuration  
		/// @param collidable The object to test collision against
		/// @param collisionResults Output collision information if hit occurs
		/// @return True if collision detected during sweep, false otherwise
		bool SweepCapsuleAgainstCollidable(const Capsule& startCapsule, const Capsule& endCapsule, const ICollidable* collidable, std::vector<CollisionResult>& collisionResults) const;

		/// @brief Gets the perch radius threshold for edge detection
		/// @return Perch radius threshold value
		[[nodiscard]] float GetPerchRadiusThreshold() const;

		/// @brief Gets the valid perch radius for the unit
		/// @return Valid perch radius value
		[[nodiscard]] float GetValidPerchRadius() const;

		/// @brief Determines if perch result should be computed for a hit
		/// @param inHit Hit result to check
		/// @param bCheckRadius Whether to check radius constraints
		/// @return True if perch result should be computed, false otherwise
		[[nodiscard]] bool ShouldComputePerchResult(const CollisionHitResult& inHit, bool bCheckRadius = true) const;

		/// @brief Find the floor beneath the unit
		/// @param capsuleLocation Location of the unit's capsule center
		/// @param floorResult Output structure containing floor information
		/// @param downwardSweepResult Optional hit result from a previous downward sweep
		void FindFloor(const Vector3& capsuleLocation, FindFloorResult& floorResult, const CollisionHitResult* downwardSweepResult) const;

		/// @brief Computes distance to floor using line and sweep traces
		/// @param capsuleLocation Location of the unit's capsule center
		/// @param lineDistance Distance for line trace
		/// @param sweepDistance Distance for sweep trace
		/// @param outFloorResult Output structure for floor information
		/// @param sweepRadius Radius for sweep trace
		/// @param downwardSweepResult Optional hit result from previous downward sweep
		void ComputeFloorDist(const Vector3& capsuleLocation, float lineDistance, float sweepDistance,
		                      FindFloorResult& outFloorResult, float sweepRadius,
		                      const CollisionHitResult* downwardSweepResult = nullptr) const;

		/// @brief Check if a hit result represents a walkable floor
		/// @param hit The hit result to check
		/// @return True if the surface is walkable
		[[nodiscard]] bool IsWalkable(const CollisionHitResult& hit) const;

		/// @brief Adjust the unit's position to align with the floor
		void AdjustFloorHeight();

	private:
		GameUnitC& m_movedUnit;

		/// Stores the runtime movement capabilities of the moved unit.
		MovementProperties m_movementState{ };

		/// The velocity of the moved unit.
		Vector3 m_velocity = Vector3::Zero;

		Vector3 m_gravityDirection = Vector3::NegativeUnitY;

		MovementMode m_movementMode = MovementMode::None;

		float m_maxAcceleration = 40.48f;

		Vector3 m_acceleration = Vector3::Zero;

		float m_analogInputModifier = 0.0f;

		float m_maxSimulationTimeStep = 0.05f;

		int32 m_maxSimulationIterations = 8;

		uint8 m_movementInProgress : 1 = 0;

		uint8 m_maintainHorizontalGroundVelocity : 1 = 1;

		float m_groundFriction = 8.0f;

		float m_brakingDecelerationWalking = m_maxAcceleration;

		float m_brakingDecelerationFalling = 0.0f;

		float m_brakingDecelerationSwimming = 0.0f;

		float m_brakingDecelerationFlying = 0.0f;

		bool m_forceMaxAcceleration = false;

		float m_minAnalogWalkSpeed = 0.0f;

		float m_gravityScale = 2.0f;

		float m_maxStepHeight = 0.45f;

		float m_jumpYVelocity = 8.0f;

		float m_brakingFrictionFactor = 8.0f;

		float m_brakingSubStepTime = 1.0f / 33.0f;

		float m_fallingLateralFriction = 0.0f;

		float m_airControl = 0.1f;

		float m_airControlBoostMultiplier = 1.5f;

		float m_airControlBoostVelocityThreshold = 0.25f;

		FindFloorResult m_currentFloor {};

		bool m_justTeleported = true;

		bool m_dontFallBelowJumpZVelocityDuringJump = true;

		bool m_applyGravityWhileJumping = true;

		float m_perchRadiusThreshold = 0.0f;

		float m_perchAdditionalHeight = 0.4f;

		float m_walkableFloorY = 0.0f;

		Radian m_walkableFloorAngle;
	};
}