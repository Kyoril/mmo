#pragma once

#include "collision.h"
#include "game_object_c.h"
#include "world_text_component.h"
#include "game/movement_info.h"
#include "shared/client_data/proto_client/spells.pb.h"
#include "game/creature_data.h"
#include "game/quest.h"
#include "game/character_customization/customizable_avatar_definition.h"

namespace mmo
{
	namespace proto_client
	{
		class ModelDataEntry;
		class FactionEntry;
		class FactionTemplateEntry;
	}

	class GameItemC;
	class GameUnitC;
	class GamePlayerC;

	/// @brief An interface for handling client network events related to units.
	class NetClient : public NonCopyable
	{
	public:
		virtual ~NetClient() = default;

	public:
		/// @brief An attack start request happened.
		virtual void SendAttackStart(const uint64 victim, const GameTime timestamp) = 0;

		/// @brief An attack stop request happened.
		virtual void SendAttackStop(const GameTime timestamp) = 0;

		virtual void GetPlayerName(uint64 guid, std::weak_ptr<GamePlayerC> player) = 0;

		virtual void GetCreatureData(uint64 guid, std::weak_ptr<GameUnitC> creature) = 0;

		virtual void GetItemData(uint64 guid, std::weak_ptr<GameItemC> item) = 0;

		virtual void GetItemData(uint64 guid, std::weak_ptr<GamePlayerC> player) = 0;

		virtual void OnMoveFallLand(GameUnitC& unit) = 0;

		virtual void OnMoveFall(GameUnitC& unit) = 0;

		virtual void SetSelectedTarget(uint64 guid) = 0;

		virtual void OnGuildChanged(std::weak_ptr<GamePlayerC> player, uint64 guildGuid) = 0;
	};

	class GameAuraC final : public NonCopyable
	{
	public:
		signal<void()> removed;

	public:
		explicit GameAuraC(GameUnitC& owner, const proto_client::SpellEntry& spell, uint64 caster, GameTime expiration);
		~GameAuraC() override;

		bool CanExpire() const { return m_expiration > 0; }

		GameTime GetExpiration() const noexcept { return m_expiration; }

		bool IsExpired() const;

		const proto_client::SpellEntry* GetSpell() const noexcept { return m_spell; }

		uint64 GetCasterId() const noexcept { return m_casterId; }

		uint64 GetTargetId() const noexcept { return m_targetId; }

	private:
		const proto_client::SpellEntry* m_spell;
		GameTime m_expiration;
		uint64 m_casterId;
		uint64 m_targetId;

		scoped_connection m_onOwnerRemoved;
	};

	/// @brief Base class for a unit in the game client. A unit is a living object in the game world which can be interacted with,
	///	       participate in combat and more. All player characters are also units.
	class GameUnitC : public GameObjectC, public CustomizationPropertyGroupApplier
	{
	public:
		signal<void(GameUnitC&, const MovementInfo&)> movementEnded;

	public:
		/// @brief Creates a instance of the GameUnitC class.
		explicit GameUnitC(Scene& scene, NetClient& netDriver, ICollisionProvider& collisionProvider, const proto_client::Project& project)
			: GameObjectC(scene, project)
			, m_netDriver(netDriver)
			, m_collisionProvider(collisionProvider)
		{
		}

		/// @brief Destroys the instance of the GameUnitC class.
		virtual ~GameUnitC() override;

	public:
		virtual ObjectTypeId GetTypeId() const override
		{
			return ObjectTypeId::Unit;
		}

		/// @brief Deserializes the unit from the given reader.
		virtual void Deserialize(io::Reader& reader, bool complete) override;

		/// @brief Updates the unit. Should be called once per frame per unit.
		virtual void Update(float deltaTime) override;

		virtual void ApplyLocalMovement(float deltaTime);

		virtual void ApplyMovementInfo(const MovementInfo& movementInfo);

		/// @copydoc GameObjectC::InitializeFieldMap
		virtual void InitializeFieldMap() override;

		ICollisionProvider& GetCollisionProvider() const noexcept { return m_collisionProvider; }

		bool OnAuraUpdate(io::Reader& reader);

		void SetQuestgiverStatus(QuestgiverStatus status);

		bool IsBeingMoved() const { return m_movementAnimation != nullptr; }

		const proto_client::ModelDataEntry* GetDisplayModel() const;

		const AvatarConfiguration& GetAvatarConfiguration() const { return m_configuration; }

		const std::shared_ptr<CustomizableAvatarDefinition>& GetAvatarDefinition() const { return m_customizationDefinition; }

	protected:
		virtual void SetupSceneObjects() override;

		bool CanStepUp(const Vector3& collisionNormal, float penetrationDepth);

		void OnEntryChanged();

		void OnScaleChanged() const;

		void OnFactionTemplateChanged();

		void SetQuestGiverMesh(const String& meshName);

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
		void SetFacing(const Radian& facing);

		/// @brief Makes the unit follow a given path of points.
		void SetMovementPath(const std::vector<Vector3>& points, GameTime moveTime);

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

		/// 
		uint32 GetMaxLevel() const { return Get<uint32>(object_fields::MaxLevel); }

		/// 
		uint32 GetXp() const { return Get<uint32>(object_fields::Xp); }

		/// 
		uint32 GetNextLevelXp() const { return Get<uint32>(object_fields::NextLevelXp); }

		/// 
		uint32 GetAvailableAttributePoints() const { return Get<uint32>(object_fields::AvailableAttributePoints); }

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

		GameAuraC* GetAura(uint32 index) const;

		/// @brief Returns whether the unit is currently alive.
		bool IsAlive() const { return GetHealth() > 0; }

		void SetTargetUnit(const std::shared_ptr<GameUnitC>& targetUnit);

	public:
		/// @brief Initializes the list of learned spells at once without notification for each single spell.
		void SetInitialSpells(const std::vector<const proto_client::SpellEntry*>& spellIds);

		/// @brief Adds a spell to the list of learned spells. Does nothing if this spell is already known.
		void LearnSpell(const proto_client::SpellEntry* spell);

		/// @brief Removes a spell from the list of learned spells. Does nothing if the spell is not known.
		void UnlearnSpell(uint32 spellId);

		/// @brief Returns whether this unit knows any spells at all.
		bool HasSpells() const noexcept { return !m_spells.empty(); }

		bool HasSpell(uint32 spellId) const;

		/// @brief Returns the spell at the specified index of learned spells of this unit. Returns nullptr if index is out of bounds.
		const proto_client::SpellEntry* GetSpell(uint32 index) const;

		/// @brief Returns the spell at the specified index of learned spells of this unit. Returns nullptr if index is out of bounds.
		const proto_client::SpellEntry* GetVisibleSpell(uint32 index) const;

		/// @brief Returns the number of known spells of this unit.
		uint32 GetSpellCount() const noexcept { return static_cast<uint32>(m_spells.size()); }

		/// @brief Returns whether the unit is currently casting any spell.
		bool IsCastingSpell() const { return false; }

	public:
		/// @brief Returns whether the unit is currently attacking any unit.
		bool IsAttacking() const noexcept { return m_victim != 0; }

		/// @brief Returns whether the unit is currently attacking the specified unit.
		bool IsAttacking(const GameUnitC& victim) const noexcept { return victim.GetGuid() == m_victim; }

		/// @brief Returns whether the unit can attack right now at all.
		bool CanAttack() const noexcept { return false; }

		/// @brief Starts auto attacking the specified unit.
		void Attack(GameUnitC& victim);

		/// @brief Stops auto attacking.
		void StopAttack();

		void NotifyAttackStopped();

		bool IsInCombat() const { return (Get<uint32>(object_fields::Flags) & unit_flags::InCombat) != 0; }

		float GetSpeed(const movement_type::Type type) const { return m_unitSpeed[type]; }

		void SetCreatureInfo(const CreatureInfo& creatureInfo);

		const String& GetName() const override;

		void SetTargetAnimState(AnimationState* newTargetState);

		void PlayOneShotAnimation(AnimationState* animState);

		void NotifyAttackSwingEvent();

		void NotifyHitEvent();

	public:
		/// @brief Returns the current movement information of this unit.
		[[nodiscard]] const MovementInfo& GetMovementInfo() const noexcept { return m_movementInfo; }

		const Capsule& GetCollider() const noexcept { return m_collider; }

		const proto_client::FactionEntry* GetFaction() const noexcept { return m_faction; }

		const proto_client::FactionTemplateEntry* GetFactionTemplate() const noexcept { return m_factionTemplate; }

		bool IsFriendlyTo(const GameUnitC& other) const;

		bool IsHostileTo(const GameUnitC& other) const;

	protected:
		void OnDisplayIdChanged();

		void UpdateCollider();

		void PerformGroundCheck();

		Vector3 GetDefaultQuestGiverOffset();

	public:
		void Apply(const VisibilitySetPropertyGroup& group, const AvatarConfiguration& configuration) override;
		void Apply(const MaterialOverridePropertyGroup& group, const AvatarConfiguration& configuration) override;
		void Apply(const ScalarParameterPropertyGroup& group, const AvatarConfiguration& configuration) override;

		void SetUnitNameVisible(bool show);

	protected:
		NetClient& m_netDriver;
		MovementInfo m_movementInfo;

		float m_movementAnimationTime = 0.0f;
		std::unique_ptr<Animation> m_movementAnimation;
		Vector3 m_movementStart;
		Quaternion m_movementStartRot;
		Vector3 m_movementEnd;
		std::vector<const proto_client::SpellEntry*> m_spells;
		std::vector<const proto_client::SpellEntry*> m_spellBookSpells;

		uint64 m_victim = 0;

		std::weak_ptr<GameUnitC> m_targetUnit;

		float m_unitSpeed[movement_type::Count];

		CreatureInfo m_creatureInfo;

		bool m_casting = false;

		SceneNode* m_nameComponentNode{ nullptr };
		std::unique_ptr<WorldTextComponent> m_nameComponent;

		Capsule m_collider;

		const proto_client::FactionEntry* m_faction = nullptr;

		const proto_client::FactionTemplateEntry* m_factionTemplate = nullptr;

		std::vector<std::unique_ptr<GameAuraC>> m_auras;

		std::shared_ptr<CustomizableAvatarDefinition> m_customizationDefinition;

	protected:
		// Animation stuff
		AnimationState* m_idleAnimState{ nullptr };
		AnimationState* m_readyAnimState{ nullptr };
		AnimationState* m_runAnimState{ nullptr };
		AnimationState* m_deathState{ nullptr };
		AnimationState* m_unarmedAttackState{ nullptr };
		AnimationState* m_castReleaseState{ nullptr };
		AnimationState* m_castingState{ nullptr };
		AnimationState* m_damageHitState{ nullptr };

		AnimationState* m_targetState = nullptr;
		AnimationState* m_currentState = nullptr;
		AnimationState* m_oneShotState = nullptr;
		ICollisionProvider& m_collisionProvider;

		SceneNode* m_questGiverNode = nullptr;
		Entity* m_questGiverEntity = nullptr;

		AvatarConfiguration m_configuration;

		Vector3 m_questOffset = Vector3::Zero;
	};
}
