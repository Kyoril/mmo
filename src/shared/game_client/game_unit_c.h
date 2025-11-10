#pragma once

#include <queue>

#include "game_object_c.h"
#include "game_aura_c.h"
#include "world_text_component.h"
#include "game/movement_info.h"
#include "game/creature_data.h"
#include "game/quest.h"
#include "game/character_customization/customizable_avatar_definition.h"
#include "math/capsule.h"

#include "unit_movement.h"
#include "movement_event.h"

namespace mmo
{
	class ManualRenderObject;
	class NetClient;

	namespace proto_client
	{
		class ModelDataEntry;
		class FactionEntry;
		class FactionTemplateEntry;
		class SpellEntry;
	}

	class GameItemC;
	class GameUnitC;
	class GamePlayerC;

	/// @brief Base class for a unit in the game client. A unit is a living object in the game world which can be interacted with,
	///	       participate in combat and more. All player characters are also units.
	class GameUnitC : public GameObjectC, public CustomizationPropertyGroupApplier
	{
	public:
		signal<void(GameUnitC &, const MovementInfo &)> movementEnded;

	public:
		/// @brief Creates a instance of the GameUnitC class.
		explicit GameUnitC(Scene &scene, NetClient &netDriver, const proto_client::Project &project, uint32 map);

		/// @brief Destroys the instance of the GameUnitC class.
		virtual ~GameUnitC() override;

	public:
		virtual ObjectTypeId GetTypeId() const override
		{
			return ObjectTypeId::Unit;
		}

		bool IsRooted() const { return (m_movementInfo.movementFlags & movement_flags::Rooted) != 0; }

		/// Adds to the input vector which will be consumed the next time input is processed.
		/// @param worldSpaceAcceleration The acceleration to add in world space.
		void AddInputVector(const Vector3 &worldSpaceAcceleration)
		{
			m_inputVector += worldSpaceAcceleration;
		}

		/// Consumes the input vector, resetting it to zero. The input vector can also be accessed via GetInputVector() if needed.
		/// @returns The input vector that was consumed.
		Vector3 ConsumeInputVector()
		{
			m_lastInputVector = m_inputVector;
			m_inputVector = Vector3::Zero;
			return m_lastInputVector;
		}

		Radian ConsumeRotation()
		{
			const Radian result = m_yawInput;
			m_yawInput = Radian(0.0f);
			return result;
		}

		/// @brief Queue a movement event
		/// @param eventType The type of event to queue
		/// @param timestamp The timestamp of the event
		/// @param movementInfo The movement info at the time of the event
		void QueueMovementEvent(MovementEventType eventType, GameTime timestamp, const MovementInfo &movementInfo);

		const Vector3 &GetInputVector() const { return m_inputVector; }

		const Vector3 &GetLastInputVector() const { return m_lastInputVector; }

		void AddYawInput(const Radian &value);

		void OnMovementModeChanged(MovementMode previousMovementMode, MovementMode newMovementMode);

		Vector3 GetForwardVector() const;

		Vector3 GetRightVector() const;

		Vector3 GetUpVector() const;

		/// @brief Deserializes the unit from the given reader.
		virtual void Deserialize(io::Reader &reader, bool complete) override;

		/// @brief Updates the unit. Should be called once per frame per unit.
		virtual void Update(float deltaTime) override;

		/// @brief Updates quest giver icon and unit name visuals
		void UpdateQuestGiverAndNameVisuals(const float deltaTime);

		/// @brief Updates normal movement (not animation-based)
		void UpdateNormalMovement(const float deltaTime);

		/// @brief Makes NPCs face their targets
		void UpdateTargetTracking() const;

		/// @brief Updates animation based on movement state
		void UpdateMovementBasedAnimation();

		/// @brief Updates animation states (transitions, blending, etc.)
		void UpdateAnimationStates(const float deltaTime, const bool isDead);

		/// @brief Updates one-shot animations
		void UpdateOneShotAnimation(const float deltaTime);

		/// @brief Handles transitions between animation states
		void UpdateAnimationTransitions(const float deltaTime);

		/// @brief Updates animation timing
		void AdvanceAnimationTimes(const float deltaTime) const;

		virtual void ApplyMovementInfo(const MovementInfo &movementInfo);

		/// @copydoc GameObjectC::InitializeFieldMap
		virtual void InitializeFieldMap() override;

		bool OnAuraUpdate(io::Reader &reader);

		void SetQuestGiverStatus(QuestgiverStatus status);

		bool IsBeingMoved() const { return !m_movementPath.empty() && !m_pathCompleted; }

		const proto_client::ModelDataEntry *GetDisplayModel() const;

		const AvatarConfiguration &GetAvatarConfiguration() const { return m_configuration; }

		const std::shared_ptr<CustomizableAvatarDefinition> &GetAvatarDefinition() const { return m_customizationDefinition; }

		unit_stand_state::Type GetStandState() const { return static_cast<unit_stand_state::Type>(Get<int32>(object_fields::StandState)); }

		const proto_client::SpellEntry *GetOpenSpell() const;

		void OnLanded();

		void OnStartFalling();

		/// @brief Sets the visibility of the collision capsule debug visualization. Creates it on demand.
		/// @param show True to show, false to hide.
		void SetCollisionVisibility(bool show);

	protected:
		/// @brief Manual render object for capsule visualization
		ManualRenderObject *m_capsuleDebugObject{nullptr};

		/// @brief Create and setup capsule debug visualization (if not already created)
		void CreateCapsuleDebugVisualization();

		/// @brief Remove capsule debug visualization
		void DestroyCapsuleDebugVisualization();

	protected:
		virtual void SetupSceneObjects() override;

		void OnEntryChanged();

		void OnScaleChanged() const;

		void OnFactionTemplateChanged();

		void SetQuestGiverMesh(const String &meshName);

		virtual void RefreshUnitName();

	public:
		/// @brief Starts moving the unit forward or backward.
		void StartMove(bool forward);

		/// @brief Starts strafing the unit to the left or right.
		void StartStrafe(bool left);

		/// @brief Stops moving the unit.
		void StopMove();

		/// @brief Stops strafing the unit.
		void StopStrafe();

		/// @brief Starts turning the unit to the left or right.
		void StartTurn(bool left);

		/// @brief Stops the unit from turning.
		void StopTurn();

		/// @brief Rotates the unit to face the specified direction.
		void SetFacing(const Radian &facing);

		void Jump();

		void StopJumping();

		void ClearJumpInput(float deltaTime);

		virtual void OnJumped();

		void CheckJumpInput();

		bool CanJump() const;

		bool JumpIsAllowed() const;

		void ResetJumpState();

		int32 GetJumpCurrentCountPreJump() const { return m_jumpCurrentCountPreJump; }

		float GetJumpMaxHoldTime() const { return m_jumpMaxHoldTime; }

		float GetJumpForceTimeRemaining() const { return m_jumpForceTimeRemaining; }

		void SetJumpForceTimeRemining(float remaining) { m_jumpForceTimeRemaining = remaining; }

		/// @brief Makes the unit follow a given path of points.
		void SetMovementPath(const std::vector<Vector3> &points, GameTime moveTime, const std::optional<Radian> &targetRotation);

		void SetSpeed(const movement_type::Type type, float speed) { m_unitSpeed[type] = speed; }

		virtual void SetQueryMask(uint32 mask);

		bool CanBeLooted() const override;

		void NotifySpellCastStarted();

		void NotifySpellCastCancelled();

		void NotifySpellCastSucceeded();

		bool IsFriendly() const;

		bool IsHostile() const;

	public:
		/// @brief Returns the current health of this unit.
		uint32 GetHealth() const { return Get<uint32>(object_fields::Health); }

		/// @brief Gets the maximum health of this unit.
		uint32 GetMaxHealth() const { return Get<uint32>(object_fields::MaxHealth); }

		///
		uint32 GetLevel() const { return Get<uint32>(object_fields::Level); }

	public:
		/// @brief Applies a tint color from a spell effect.
		/// @param spellId The spell ID applying the tint.
		/// @param tintColor The tint color (RGBA).
		void AddSpellTint(uint32 spellId, const Vector4& tintColor);

		/// @brief Removes a spell tint.
		/// @param spellId The spell ID to remove the tint for.
		void RemoveSpellTint(uint32 spellId);

		/// @brief Gets the current blended tint color.
		/// @return The blended tint color from all active spell tints.
		Vector4 GetBlendedTint() const;

	private:
		/// @brief Recalculates the blended tint and updates all SubEntity materials.
		void UpdateTintOnMaterials();

		/// @brief Ensures MaterialInstances exist for all visible SubEntities when tinting.
		void EnsureMaterialInstances();

		/// @brief Restores original shared materials when no tints are active.
		void RestoreOriginalMaterials();

	public:
		///
		uint32 GetMaxLevel() const { return Get<uint32>(object_fields::MaxLevel); }

		///
		uint32 GetXp() const { return Get<uint32>(object_fields::Xp); }

		///
		uint32 GetNextLevelXp() const { return Get<uint32>(object_fields::NextLevelXp); }

		///
		virtual uint32 GetAvailableAttributePoints() const
		{
			return 0;
		}

		virtual uint32 GetTalentPoints() const
		{
			return 0;
		}

		int32 GetPower(int32 powerType) const;

		int32 GetMaxPower(int32 powerType) const;

		uint32 GetAuraCount() const { return m_auras.size(); }

		int32 GetPowerType() const { return Get<int32>(object_fields::PowerType); }

		float GetAttackPower() const { return Get<float>(object_fields::AttackPower); }

		float GetMinDamage() const { return Get<float>(object_fields::MinDamage); }

		float GetMaxDamage() const { return Get<float>(object_fields::MaxDamage); }

		uint32 GetAttackTime() const { return Get<uint32>(object_fields::BaseAttackTime); }

		int32 GetStat(int32 statId) const;

		int32 GetPosStat(int32 statId) const;

		int32 GetNegStat(int32 statId) const;

		/// Gets the armor value of the unit.
		int32 GetArmor() const { return Get<int32>(object_fields::Armor); }

		/// This function is only used for UI display. All calculation is done on the server!
		float GetArmorReductionFactor() const;

		int32 GetHealthFromStat(int32 statId) const;

		int32 GetManaFromStat(int32 statId) const;

		int32 GetAttackPowerFromStat(int32 statId) const;

		virtual uint8 GetAttributeCost(uint32 attribute) const;

		GameAuraC *GetAura(uint32 index) const;

		/// @brief Returns whether the unit is currently alive.
		bool IsAlive() const { return GetHealth() > 0; }

		void SetTargetUnit(const std::shared_ptr<GameUnitC> &targetUnit);

	public:
		/// @brief Initializes the list of learned spells at once without notification for each single spell.
		void SetInitialSpells(const std::vector<const proto_client::SpellEntry *> &spellIds);

		/// @brief Adds a spell to the list of learned spells. Does nothing if this spell is already known.
		void LearnSpell(const proto_client::SpellEntry *spell);

		/// @brief Removes a spell from the list of learned spells. Does nothing if the spell is not known.
		void UnlearnSpell(uint32 spellId);

		/// @brief Returns whether this unit knows any spells at all.
		bool HasSpells() const { return !m_spells.empty(); }

		bool HasSpell(uint32 spellId) const;

		/// @brief Returns the spell at the specified index of learned spells of this unit. Returns nullptr if index is out of bounds.
		const proto_client::SpellEntry *GetSpell(uint32 index) const;

		/// @brief Returns the spell at the specified index of learned spells of this unit. Returns nullptr if index is out of bounds.
		const proto_client::SpellEntry *GetVisibleSpell(uint32 index) const;

		/// @brief Returns the number of known spells of this unit.
		uint32 GetSpellCount() const { return static_cast<uint32>(m_spells.size()); }

		/// @brief Returns whether the unit is currently casting any spell.
		bool IsCastingSpell() const { return false; }

	public:
		/// @brief Returns whether the unit is currently attacking any unit.
		bool IsAttacking() const { return m_victim != 0; }

		/// @brief Returns whether the unit is currently attacking the specified unit.
		bool IsAttacking(const GameUnitC &victim) const { return victim.GetGuid() == m_victim; }

		/// @brief Returns whether the unit can attack right now at all.
		bool CanAttack() const { return false; }

		/// @brief Starts auto attacking the specified unit.
		void Attack(GameUnitC &victim);

		/// @brief Stops auto attacking.
		void StopAttack();

		void NotifyAttackStopped();

		bool IsInCombat() const { return (Get<uint32>(object_fields::Flags) & unit_flags::InCombat) != 0; }

		float GetSpeed(const movement_type::Type type) const { return m_unitSpeed[type]; }

		void SetCreatureInfo(const CreatureInfo &creatureInfo);

		const String &GetName() const override;

		void SetTargetAnimState(AnimationState *newTargetState);

		void PlayOneShotAnimation(AnimationState *animState);

		/// @brief Cancel the currently playing one-shot animation and refresh movement state
		void CancelOneShotAnimation();

		/// @brief Force an update of movement-based animations (e.g., after canceling spell animations)
		void RefreshMovementAnimation();

		void NotifyAttackSwingEvent();

		void NotifyHitEvent();

		void SetWeaponProficiency(uint32 mask);

		void SetArmorProficiency(uint32 mask);

		uint32 GetWeaponProficiency() const { return m_weaponProficiency; }

		uint32 GetArmorProficiency() const { return m_armorProficiency; }

		/// @brief Get the maximum step-up height for this unit
		/// @return Maximum step-up height in world units
		float GetMaxStepUpHeight() const
		{
			return m_maxStepUpHeight;
		}

		/// @brief Set the maximum step-up height for this unit
		/// @param height Maximum step-up height in world units
		void SetMaxStepUpHeight(const float height)
		{
			m_maxStepUpHeight = height;
		}

		/// @brief Returns the default walkable slope angle for this unit.
		[[nodiscard]] Radian GetWalkableSlope() const
		{
			// Default walkable slope angle
			return Radian(Degree(55.0f));
		}

		ManualRenderObject *GetNormalDebugObject() const
		{
			return m_normalDebugObject;
		}

	public:
		/// @brief Returns the current movement information of this unit.
		[[nodiscard]] const MovementInfo &GetMovementInfo() const { return m_movementInfo; }

		const Capsule &GetCollider() const { return m_collider; }

		const proto_client::FactionEntry *GetFaction() const { return m_faction; }

		const proto_client::FactionTemplateEntry *GetFactionTemplate() const { return m_factionTemplate; }

		bool IsFriendlyTo(const GameUnitC &other) const;

		bool IsHostileTo(const GameUnitC &other) const;

	protected:
		void OnDisplayIdChanged();

		void UpdateCollider();

		Vector3 GetDefaultQuestGiverOffset();

		void UpdateMovementInfo();

	public:
		void Apply(const VisibilitySetPropertyGroup &group, const AvatarConfiguration &configuration) override;
		void Apply(const MaterialOverridePropertyGroup &group, const AvatarConfiguration &configuration) override;
		void Apply(const ScalarParameterPropertyGroup &group, const AvatarConfiguration &configuration) override;

		void SetUnitNameVisible(bool show);

		UnitMovement *GetUnitMovement() const
		{
			ASSERT(m_unitMovement);
			return m_unitMovement.get();
		}

	private:
		/// Plays the landing animation when transitioning from falling to ground
		void PlayLandAnimation();

		/// Updates path-based movement towards the next waypoint
		void UpdatePathMovement(const float deltaTime);

		/// Returns true if the unit is currently following a movement path
		bool IsFollowingPath() const { return !m_movementPath.empty() && !m_pathCompleted; }

		/// Calculates position along the path based on distance traveled
		Vector3 CalculatePositionAlongPath(float distance) const;

		/// Returns true if this unit is controlled by the local player
		bool IsControlledByLocalPlayer() const;

	protected:
		NetClient &m_netDriver;
		MovementInfo m_movementInfo;

		float m_movementAnimationTime = 0.0f;
		Vector3 m_movementStart;
		Quaternion m_movementStartRot;
		Vector3 m_movementEnd;

		// Path-based movement system (replaces animation-based movement)
		std::vector<Vector3> m_movementPath;
		Vector3 m_pathStartPosition; // Where the unit was when path started
		size_t m_currentPathIndex = 0;
		std::optional<Radian> m_targetRotation;
		bool m_pathCompleted = false;
		GameTime m_pathStartTime = 0;			 // When the path movement started
		float m_pathTotalLength = 0.0f;			 // Total length of all path segments
		std::vector<float> m_pathSegmentLengths; // Length of each segment for time calculation

		// Path gravity simulation
		float m_pathVerticalVelocity = 0.0f; // Current vertical velocity for gravity
		bool m_pathOnGround = true;			 // Whether unit is on ground during path movement

		std::vector<const proto_client::SpellEntry *> m_spells;
		std::vector<const proto_client::SpellEntry *> m_spellBookSpells;

		uint64 m_victim = 0;

		std::weak_ptr<GameUnitC> m_targetUnit;

		float m_unitSpeed[movement_type::Count];

		CreatureInfo m_creatureInfo;

		SceneNode *m_nameComponentNode{nullptr};
		std::unique_ptr<WorldTextComponent> m_nameComponent;

		Capsule m_collider;
		const proto_client::FactionEntry *m_faction = nullptr;

		const proto_client::FactionTemplateEntry *m_factionTemplate = nullptr;

		std::vector<std::unique_ptr<GameAuraC>> m_auras;

		std::shared_ptr<CustomizableAvatarDefinition> m_customizationDefinition;

		uint32 m_weaponProficiency = 0;
		uint32 m_armorProficiency = 0;

	protected:
		// Animation stuff
		AnimationState *m_idleAnimState{nullptr};
		AnimationState *m_readyAnimState{nullptr};
		AnimationState *m_runAnimState{nullptr};
		AnimationState *m_deathState{nullptr};
		AnimationState *m_unarmedAttackState{nullptr};
		AnimationState *m_castReleaseState{nullptr};
		AnimationState *m_castingState{nullptr};
		AnimationState *m_damageHitState{nullptr};
		// Jump animation states
		AnimationState *m_jumpStartState{nullptr};
		AnimationState *m_fallingState{nullptr};
		AnimationState *m_landState{nullptr};

		AnimationState *m_targetState = nullptr;
		AnimationState *m_currentState = nullptr;
		AnimationState *m_oneShotState = nullptr;

		SceneNode *m_questGiverNode = nullptr;
		Entity *m_questGiverEntity = nullptr;

		AvatarConfiguration m_configuration;

		Vector3 m_questOffset = Vector3::Zero;

		float m_maxStepUpHeight = 0.35f;

		ManualRenderObject *m_normalDebugObject = nullptr;

		Vector3 m_velocity;

		Vector3 m_acceleration;

		Vector3 m_inputVector{0.0f, 0.0f, 0.0f};

		Vector3 m_lastInputVector{0.0f, 0.0f, 0.0f};

		std::unique_ptr<UnitMovement> m_unitMovement;

		bool m_pressedJump : 1 = false;

		bool m_wasJumping : 1 = false;

		float m_jumpKeyHoldTime = 0.0f;

		float m_jumpForceTimeRemaining = 0.0f;

		int32 m_jumpCurrentCount = 0;

		int32 m_jumpCurrentCountPreJump = 0;

		int32 m_jumpMaxCount = 1;

		float m_jumpMaxHoldTime = 0.0f;

		Radian m_yawInput{0.0f};

		std::queue<MovementEvent> m_movementEventQueue;

		GameTime m_lastHeartbeat = 0;

		/// @brief Map of active spell tints (spellId -> tint color).
		std::map<uint32, Vector4> m_spellTints;

		/// @brief Struct to track original materials and instances per SubEntity.
		struct SubEntityMaterialState
		{
			std::shared_ptr<class MaterialInterface> originalMaterial; // Shared material before tinting
			std::shared_ptr<class MaterialInstance> tintInstance; // Per-unit MaterialInstance for tinting
		};

		/// @brief Map of SubEntity -> material state (only populated when tinting is active).
		std::map<class SubEntity*, SubEntityMaterialState> m_tintMaterialStates;
	};
}
