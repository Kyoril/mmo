// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <memory>
#include <set>

#include "aura_container.h"
#include "game/auto_attack.h"
#include "game_object_s.h"
#include "spell_cast.h"
#include "game/spell_target_map.h"
#include "unit_mover.h"
#include "base/countdown.h"
#include "game/damage_school.h"
#include "game/spell.h"

namespace mmo
{
	namespace unit_mod_type
	{
		enum Type
		{
			/// Absolute base value of this unit based on it's level.
			BaseValue = 0,
			/// Base value mulitplier (1.0 = 100%)
			BasePct = 1,
			/// Absolute total value. Final value: BaseValue * BasePct + TotalValue * TotalPct;
			TotalValue = 2,
			/// Total value multiplier.
			TotalPct = 3,

			End = 4
		};
	}

	typedef unit_mod_type::Type UnitModType;

	namespace unit_mods
	{
		enum Type
		{
			/// Strength stat value modifier.
			StatStrength,

			/// Agility stat value modifier.
			StatAgility,

			/// Stamina stat value modifier.
			StatStamina,

			/// Intellect stat value modifier.
			StatIntellect,

			/// Spirit stat value modifier.
			StatSpirit,

			/// Health value modifier.
			Health,

			/// Mana power value modifier.
			Mana,

			/// Rage power value modifier.
			Rage,

			/// Energy power value modifier.
			Energy,

			/// Armor resistance value modifier.
			Armor,

			/// Holy resistance value modifier.
			ResistanceHoly,

			/// Fire resistance value modifier.
			ResistanceFire,

			/// Nature resistance value modifier.
			ResistanceNature,

			/// Frost resistance value modifier.
			ResistanceFrost,

			/// Shadow resistance value modifier.
			ResistanceShadow,

			/// Arcane resistance value modifier.
			ResistanceArcane,

			/// Melee attack power value modifier.
			AttackPower,

			/// Ranged attack power value modifier.
			AttackPowerRanged,

			/// Main hand weapon damage modifier.
			DamageMainHand,

			/// Off hand weapon damage modifier.
			DamageOffHand,

			/// Ranged weapon damage modifier.
			DamageRanged,

			/// Main hand weapon attack speed modifier.
			AttackSpeed,

			/// Ranged weapon attack speed modifier.
			AttackSpeedRanged,

			SpellDamage,

			Healing,

			End
		};
	}

	typedef unit_mods::Type UnitMods;

	namespace spell_mod_type
	{
		enum Type
		{
			/// Equals aura_type::AddFlatModifier
			Flat,

			/// Equals aura_type::AddPctModifier
			Pct
		};
	}

	typedef spell_mod_type::Type SpellModType;

	/// Represents a spell modifier which is used to modify spells for a GameCharacter.
	/// This is only(?) used by talents, and is thus only available for characters.
	struct SpellModifier final
	{
		/// The spell modifier operation (what should be changed?)
		SpellModOp op;

		/// The modifier type (flag or percentage)
		SpellModType type;

		/// Charge count of this modifier (some are like "Increases damage of the next N casts")
		int16 charges;

		/// The modifier value.
		int32 value;

		/// Mask to determine which spells are modified.
		uint64 mask;

		/// Affected spell index.
		uint32 spellId;

		/// Index of the affected spell index.
		uint8 effectId;
	};

	/// Contains a list of spell modifiers of a unit.
	typedef std::list<SpellModifier> SpellModList;

	/// Stores spell modifiers by it's operation.
	typedef std::map<SpellModOp, SpellModList> SpellModsByOp;

	namespace proto
	{
		class FactionTemplateEntry;
		class SpellEntry;
	}

	class UnitStats final
	{
	public:
		static uint32 DeriveFromBaseWithFactor(const uint32 statValue, const uint32 baseValue, const uint32 factor);

		static uint32 GetMaxHealthFromStamina(const uint32 stamina);

		static uint32 GetMaxManaFromIntellect(const uint32 intellect);
	};

	class NetUnitWatcherS
	{
	public:
		virtual ~NetUnitWatcherS() = default;

	public:
		virtual void OnTeleport(const Vector3& position, const Radian& facing) = 0;

		virtual void OnAttackSwingEvent(AttackSwingEvent error) = 0;

		virtual void OnXpLog(uint32 amount) = 0;

		virtual void OnSpellDamageLog(uint64 targetGuid, uint32 amount, uint8 school, DamageFlags flags, const proto::SpellEntry& spell) = 0;

		virtual void OnNonSpellDamageLog(uint64 targetGuid, uint32 amount, DamageFlags flags) = 0;

		virtual void OnSpeedChangeApplied(MovementType type, float speed, uint32 ackId) = 0;

		virtual void OnLevelUp(uint32 newLevel, int32 healthDiff, int32 manaDiff, int32 staminaDiff, int32 strengthDiff, int32 agilityDiff, int32 intDiff, int32 spiritDiff, int32 talentPoints, int32 attributePoints) = 0;

		virtual void OnSpellModChanged(SpellModType type, uint8 effectIndex, SpellModOp op, int32 value) = 0;
	};

	/// Enumerates possible movement changes which need to be acknowledged by the client.
	enum class MovementChangeType
	{
		/// Default value. Do not use!
		Invalid,

		/// Character has been rooted or unrooted.
		Root,
		/// Character can or can no longer walk on water.
		WaterWalk,
		/// Character is hovering or no longer hovering.
		Hover,
		/// Character can or can no longer fly.
		CanFly,
		/// Character has or has no longer slow fall
		FeatherFall,

		/// Walk speed changed
		SpeedChangeWalk,
		/// Run speed changed
		SpeedChangeRun,
		/// Run back speed changed
		SpeedChangeRunBack,
		/// Swim speed changed
		SpeedChangeSwim,
		/// Swim back speed changed
		SpeedChangeSwimBack,
		/// Turn rate changed
		SpeedChangeTurnRate,
		/// Flight speed changed
		SpeedChangeFlightSpeed,
		/// Flight back speed changed
		SpeedChangeFlightBackSpeed,

		/// Character teleported
		Teleport,
		/// Character was knocked back
		KnockBack
	};

	/// Bundles informations which are only used for knock back acks.
	struct KnockBackInfo
	{
		float vcos = 0.0f;
		float vsin = 0.0f;
		/// 2d speed value
		float speedXY = 0.0f;
		/// z axis speed value
		float speedZ = 0.0f;
	};

	struct TeleportInfo
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float o = 0.0f;
	};

	/// Contains infos about a pending movement change which first needs to
	/// be acknowledged by the client before it's effects can be applied.
	struct PendingMovementChange final
	{
		/// A counter which is used to verify that the ackknowledged change
		/// is for the expected pending change.
		uint32 counter;
		/// Defines what kind of change should be applied.
		MovementChangeType changeType;
		/// A timestamp value used for timeouts.
		GameTime timestamp;

		// Additional data to perform checks whether the ack packet data is correct
		// and hasn't been modified at the client side.
		union
		{
			/// The new value which should be applied. Currently only used for speed changes.
			float speed;

			/// Whether the respective movement flags should be applied or misapplied. This is
			/// only used for Hover / FeatherFall etc., and ignored for speed changes.
			bool apply;

			KnockBackInfo knockBackInfo;

			TeleportInfo teleportInfo;
		};

		PendingMovementChange();
	};

	namespace combat_capabilities
	{
		enum Type
		{
			None = 0,

			/// The unit can block attacks.
			CanBlock = 0x1,

			/// The unit can parry attacks.
			CanParry = 0x2,

			/// The unit can dodge attacks.
			CanDodge = 0x4
		};
	}

	/// @brief Represents a living object (unit) in the game world.
	class GameUnitS : public GameObjectS
	{
	public:

		/// Fired when this unit was killed. Parameter: GameUnit* killer (may be nullptr if killer
		/// information is not available (for example due to environmental damage))
		signal<void(GameUnitS*)> killed;
		signal<void(GameUnitS&, float)> threatened;
		signal<void(GameUnitS*, uint32)> takenDamage;
		signal<void(const proto::SpellEntry&)> startedCasting;

		signal<void(GameUnitS&)> meleeAttackDone;

	public:
		GameUnitS(const proto::Project& project, TimerQueue& timers);

		virtual ~GameUnitS() override;

		virtual void Initialize() override;

		void TriggerDespawnTimer(GameTime despawnDelay);

		virtual void WriteObjectUpdateBlock(io::Writer& writer, bool creation = true) const override;

		virtual void WriteValueUpdateBlock(io::Writer& writer, bool creation = true) const override;

		virtual bool HasMovementInfo() const override { return true; }

		virtual void RefreshStats();

		void SetNetUnitWatcher(NetUnitWatcherS* watcher) { m_netUnitWatcher = watcher; }

		const Vector3& GetPosition() const noexcept override;

		/// Gets the specified unit modifier value.
		float GetModifierValue(UnitMods mod, UnitModType type) const;

		float GetCalculatedModifierValue(UnitMods mod) const;

		/// Sets the unit modifier value to the given value.
		void SetModifierValue(UnitMods mod, UnitModType type, float value);

		/// Modifies the given unit modifier value by adding or subtracting it from the current value.
		void UpdateModifierValue(UnitMods mod, UnitModType type, float amount, bool apply);

		/// Gets the next pending movement change and removes it from the queue of pending movement changes.
		/// You need to make sure that there are any pending changes before calling this method.
		PendingMovementChange PopPendingMovementChange();

		void PushPendingMovementChange(PendingMovementChange change);

		bool HasPendingMovementChange() const { return !m_pendingMoveChanges.empty(); }

		/// Determines whether there is a timed out pending movement change.
		bool HasTimedOutPendingMovementChange() const;

		virtual bool IsInteractable(const GameUnitS& interactor) const;

		virtual float GetInteractionDistance() const;

	public:
		virtual void SetLevel(uint32 newLevel);

		virtual void Relocate(const Vector3& position, const Radian& facing) override;

		virtual void ApplyMovementInfo(const MovementInfo& info) override;

	public:
		static PowerType GetPowerTypeByUnitMod(UnitMods mod);

		static UnitMods GetUnitModByStat(uint8 stat);

		static UnitMods GetUnitModByPower(PowerType power);

		bool SpellHasCooldown(uint32 spellId, uint32 spellCategory) const;

		bool HasSpell(uint32 spellId) const;

		void SetInitialSpells(const std::vector<uint32>& spellIds);

		void AddSpell(uint32 spellId);

		void RemoveSpell(uint32 spellId);

		const std::set<const proto::SpellEntry*>& GetSpells() const;

		void SetCooldown(uint32 spellId, GameTime cooldownTimeMs);

		void SetSpellCategoryCooldown(uint32 spellCategory, GameTime cooldownTimeMs);

		SpellCastResult CastSpell(const SpellTargetMap& target, const proto::SpellEntry& spell, uint32 castTimeMs, bool isProc = false, uint64 itemGuid = 0);

		void CancelCast(SpellInterruptFlags reason, GameTime interruptCooldown = 0) const;

		void Damage(uint32 damage, uint32 school, GameUnitS* instigator);

		int32 Heal(uint32 amount, GameUnitS* instigator);

		void SpellDamageLog(uint64 targetGuid, uint32 amount, uint8 school, DamageFlags flags, const proto::SpellEntry& spell);

		void Kill(GameUnitS* killer);

		uint32 GetHealth() const { return Get<uint32>(object_fields::Health); }

		uint32 GetMaxHealth() const { return Get<uint32>(object_fields::MaxHealth); }

		bool IsAlive() const { return GetHealth() > 0; }

		/// Starts the regeneration countdown.
		void StartRegeneration() const;

		/// Stops the regeneration countdown.
		void StopRegeneration() const;

		void ApplyAura(std::shared_ptr<AuraContainer>&& aura);

		void RemoveAllAurasDueToItem(uint64 itemGuid);

		void RemoveAllAurasFromCaster(uint64 casterGuid);

		void RemoveAura(const std::shared_ptr<AuraContainer>& aura);

		void BuildAuraPacket(io::Writer& writer) const;

		void NotifyManaUsed();

		/// Executed when an attack was successfully parried.
		virtual void OnParry();

		/// Executed when an attack was successfully dodged.
		virtual void OnDodge();

		/// Executed when an attack was successfully blocked.
		virtual void OnBlock();

		/// Teleports the unit to a new location on the same map.
		void TeleportOnMap(const Vector3& position, const Radian& facing);

		/// Modifies the character spell modifiers by applying or misapplying a new mod.
		/// @param mod The spell modifier to apply or misapply.
		/// @param apply Whether to apply or misapply the spell mod.
		void ModifySpellMod(const SpellModifier& mod, bool apply);

		/// Gets the total amount of spell mods for one type and one spell.
		int32 GetTotalSpellMods(SpellModType type, SpellModOp op, uint32 spellId) const;

		/// Applys all matching spell mods of this character to a given value.
		/// @param op The spell modifier operation to apply.
		/// @param spellId Id of the spell, to know which modifiers do match.
		/// @param ref_value Reference of the base value, which will be modified by this method.
		/// @returns Delta value or 0 if ref_value didn't change.
		template<class T>
		T ApplySpellMod(SpellModOp op, uint32 spellId, T& ref_value) const
		{
			float totalPct = 1.0f;
			int32 totalFlat = 0;

			totalFlat += GetTotalSpellMods(spell_mod_type::Flat, op, spellId);
			totalPct += static_cast<float>(GetTotalSpellMods(spell_mod_type::Pct, op, spellId)) * 0.01f;

			const float diff = static_cast<float>(ref_value) * (totalPct - 1.0f) + static_cast<float>(totalFlat);
			ref_value = T(static_cast<float>(ref_value) + diff);

			return T(diff);
		}

	private:
		void SetVictim(const std::shared_ptr<GameUnitS>& victim);

		void VictimKilled(GameUnitS* killer);

		void VictimDespawned(GameObjectS&);

	protected:
		virtual float GetUnitMissChance() const;

		virtual bool HasOffhandWeapon() const;

		bool CanDualWield() const;

		int32 GetMaxSkillValueForLevel(uint32 level) const;

		MeleeAttackOutcome RollMeleeOutcomeAgainst(GameUnitS& victim, WeaponAttack attackType) const;

	public:
		float MeleeMissChance(const GameUnitS& victim, weapon_attack::Type attackType, int32 skillDiff, uint32 spellId) const;

		float CriticalHitChance(const GameUnitS& victim, weapon_attack::Type attackType) const;

		/// Returns the dodge chance in percent, ranging from 0 to 100.0f. If the unit can't dodge at all, this will always return 0.
		float DodgeChance() const;

		/// Returns the parry chance in percent, ranging from 0 to 100.0f. If the unit can't parry at all, this will always return 0.
		float ParryChance() const;

		/// Returns the block chance in percent, ranging from 0 to 100.0f. If the unit can't block at all, this will always return 0.
		float BlockChance() const;

		/// Returns true if the unit can block attacks.
		bool CanBlock() const { return (m_combatCapabilities & combat_capabilities::CanBlock) != 0; }

		/// Re-evaluates if the unit can block attacks based on spell effects.
		///	@param gainedEffect Set this to true if you are sure the unit should can block attacks. This is just a performance shortcut,
		///	so we don't iterate through spell effects if we don't need to. If this is set to false, spell effects will be checked.
		void NotifyCanBlock(bool gainedEffect);

		/// Returns true if the unit can parry melee attacks.
		bool CanParry() const { return (m_combatCapabilities & combat_capabilities::CanParry) != 0; }

		/// Re-evaluates if the unit can parry attacks based on spell effects.
		///	@param gainedEffect Set this to true if you are sure the unit should can parry attacks. This is just a performance shortcut,
		///	so we don't iterate through spell effects if we don't need to. If this is set to false, spell effects will be checked.
		void NotifyCanParry(bool gainedEffect);

		/// Returns true if the unit can dodge melee attacks.
		bool CanDodge() const { return (m_combatCapabilities & combat_capabilities::CanDodge) != 0; }

		/// Re-evaluates if the unit can dodge attacks based on spell effects.
		///	@param gainedEffect Set this to true if you are sure the unit should can dodge attacks. This is just a performance shortcut,
		///	so we don't iterate through spell effects if we don't need to. If this is set to false, spell effects will be checked.
		void NotifyCanDodge(bool gainedEffect);

		/// Returns true if the unit has an active aura effect of the given type. Don't use this too often as it's iterating through all active auras,
		///	which is not a constant complexity operation.
		bool HasAuraEffect(AuraType type) const;

		/// Returns true if the unit has a spell with a spell effect of the given type. Don't use this too often as it's iterating through all spells the unit knows,
		///	which is not a constant complexity operation.
		bool HasSpellEffect(SpellEffect type) const;

		bool IsAttacking(const std::shared_ptr<GameUnitS>& victim) const { return m_victim.lock() == victim; }

		bool IsAttacking() const { return GetVictim() != nullptr; }

		GameUnitS* GetVictim() const { return m_victim.lock().get(); }

		void StartAttack(const std::shared_ptr<GameUnitS>& victim);

		void StopAttack();

		void SetTarget(uint64 targetGuid);

		bool IsInCombat() const { return (Get<uint32>(object_fields::Flags) & unit_flags::InCombat) != 0; }

		void SetInCombat(bool inCombat);

		float GetMeleeReach() const { return 5.0f; /* TODO */ }

		void AddAttackingUnit(const GameUnitS& attacker);

		void RemoveAttackingUnit(const GameUnitS& attacker);

		void RemoveAllAttackingUnits();

		static float GetBaseSpeed(const MovementType type);

		float GetSpeed(const MovementType type) const;

		/// Called by spell auras to notify that a speed aura has been applied or misapplied.
		/// If this unit is player controlled, a client notification is sent and the speed is
		/// only changed after the client has acknowledged this change. Otherwise, the speed
		/// is immediately applied using ApplySpeedChange.
		void NotifySpeedChanged(MovementType type, bool initial = false);

		/// Immediately applies a speed change for this unit. Never call this method directly
		/// unless you know what you're doing.
		void ApplySpeedChange(MovementType type, float speed, bool initial = false);

		uint32 CalculateArmorReducedDamage(uint32 attackerLevel, uint32 damage) const;

		/// If this returns true, the other unit is an enemy and we can attack it.
		bool UnitIsEnemy(const GameUnitS& other) const;

		/// If this returns true, the unit is a friend of us and thus we can not attack it.
		bool UnitIsFriendly(const GameUnitS& other) const;

		const proto::FactionTemplateEntry* GetFactionTemplate() const;

		uint32 GetLevel() const { return Get<uint32>(object_fields::Level); }

		void SetBinding(uint32 mapId, const Vector3& position, const Radian& facing);

		uint32 GetBindMap() const { return m_bindMap; }

		const Vector3& GetBindPosition() const { return m_bindPosition; }

		const Radian& GetBindFacing() const { return m_bindFacing; }

	protected:
		virtual void OnKilled(GameUnitS* killer);

		virtual void OnSpellLearned(const proto::SpellEntry& spell) {}

		virtual void OnSpellUnlearned(const proto::SpellEntry& spell) {}

		virtual void OnSpellCastEnded(bool succeeded);

		virtual void OnRegeneration();

		virtual void RegenerateHealth();

		virtual void RegeneratePower(PowerType powerType);

		virtual void AddPower(PowerType powerType, int32 amount);

		void OnAttackSwingEvent(AttackSwingEvent attackSwingEvent) const;

	public:
		int32 GetMaximumBasePoints(AuraType type) const;

		int32 GetMinimumBasePoints(AuraType type) const;

		float GetTotalMultiplier(AuraType type) const;

		void OnAttackSwing();

	protected:
		virtual void PrepareFieldMap() override
		{
			m_fields.Initialize(object_fields::UnitFieldCount);
		}

	private:

		/// 
		void OnDespawnTimer();

		void TriggerNextAutoAttack();

	public:
		TimerQueue& GetTimers() const { return m_timers; }

		UnitMover& GetMover() const { return *m_mover; }

		/// Generates the next client ack id for this unit.
		inline uint32 GenerateAckId() { return m_ackGenerator.GenerateId(); }

	protected:
		typedef LinearSet<const GameUnitS*> AttackingUnitSet;

		TimerQueue& m_timers;
		Countdown m_despawnCountdown;
		std::unique_ptr<UnitMover> m_mover;
		Countdown m_attackSwingCountdown;
		GameTime m_lastMainHand = 0, m_lastOffHand = 0;
		Countdown m_regenCountdown;
		GameTime m_lastManaUse = 0;

		std::weak_ptr<GameUnitS> m_victim;

		std::set<const proto::SpellEntry*> m_spells;
		std::unique_ptr<SpellCast> m_spellCast;

		std::map<uint32, GameTime> m_spellCooldowns;
		std::map<uint32, GameTime> m_spellCategoryCooldowns;

		AttackingUnitSet m_attackingUnits;

		NetUnitWatcherS* m_netUnitWatcher = nullptr;
		mutable Vector3 m_lastPosition;

		std::vector<std::shared_ptr<AuraContainer>> m_auras;

		typedef std::array<float, unit_mod_type::End> UnitModTypeArray;
		typedef std::array<UnitModTypeArray, unit_mods::End> UnitModArray;
		UnitModArray m_unitMods;

		std::array<float, movement_type::Count> m_speedBonus;
		IdGenerator<uint32> m_ackGenerator;
		std::list<PendingMovementChange> m_pendingMoveChanges;

		float m_manaRegenPerTick = 0.0f;
		float m_healthRegenPerTick = 0.0f;

		mutable const proto::FactionTemplateEntry* m_cachedFactionTemplate = nullptr;
		scoped_connection_container m_victimSignals;

		uint32 m_bindMap;
		Vector3 m_bindPosition;
		Radian m_bindFacing;

		bool m_canDualWield = false;
		SpellModsByOp m_spellModsByOp;

		uint8 m_combatCapabilities = combat_capabilities::None;

	private:
		friend io::Writer& operator << (io::Writer& w, GameUnitS const& object);
		friend io::Reader& operator >> (io::Reader& r, GameUnitS& object);
	};

	io::Writer& operator << (io::Writer& w, GameUnitS const& object);
	io::Reader& operator >> (io::Reader& r, GameUnitS& object);
}
