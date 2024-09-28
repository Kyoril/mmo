// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

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

			End
		};
	}

	typedef unit_mods::Type UnitMods;

	namespace proto
	{
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
		virtual void OnAttackSwingEvent(AttackSwingEvent error) = 0;

		virtual void OnXpLog(uint32 amount) = 0;

		virtual void OnSpellDamageLog(uint64 targetGuid, uint32 amount, uint8 school, DamageFlags flags, const proto::SpellEntry& spell) = 0;

		virtual void OnNonSpellDamageLog(uint64 targetGuid, uint32 amount, DamageFlags flags) = 0;
	};

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

	public:
		GameUnitS(const proto::Project& project,
			TimerQueue& timers);

		virtual ~GameUnitS() override = default;

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

		/// Sets the unit modifier value to the given value.
		void SetModifierValue(UnitMods mod, UnitModType type, float value);

		/// Modifies the given unit modifier value by adding or subtracting it from the current value.
		void UpdateModifierValue(UnitMods mod, UnitModType type, float amount, bool apply);

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

		void SetCooldown(uint32 spellId, GameTime cooldownTimeMs);

		void SetSpellCategoryCooldown(uint32 spellCategory, GameTime cooldownTimeMs);

		SpellCastResult CastSpell(const SpellTargetMap& target, const proto::SpellEntry& spell, uint32 castTimeMs);

		void Damage(uint32 damage, uint32 school, GameUnitS* instigator);

		void SpellDamageLog(uint64 targetGuid, uint32 amount, uint8 school, DamageFlags flags, const proto::SpellEntry& spell);

		void Kill(GameUnitS* killer);

		uint32 GetHealth() const { return Get<uint32>(object_fields::Health); }

		uint32 GetMaxHealth() const { return Get<uint32>(object_fields::MaxHealth); }

		bool IsAlive() const { return GetHealth() > 0; }

		/// Starts the regeneration countdown.
		void StartRegeneration();

		/// Stops the regeneration countdown.
		void StopRegeneration();

		void ApplyAura(std::unique_ptr<AuraContainer>&& aura);

	public:
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

	protected:
		uint32 CalculateArmorReducedDamage(uint32 attackerLevel, uint32 damage) const;

		virtual void OnKilled(GameUnitS* killer);

		virtual void OnSpellLearned(const proto::SpellEntry& spell) {}

		virtual void OnSpellUnlearned(const proto::SpellEntry& spell) {}

		virtual void OnSpellCastEnded(bool succeeded);

		virtual void OnRegeneration();

		virtual void RegenerateHealth();

		virtual void RegeneratePower(PowerType powerType);

		void OnAttackSwingEvent(AttackSwingEvent attackSwingEvent) const;

	protected:
		virtual void PrepareFieldMap() override
		{
			m_fields.Initialize(object_fields::UnitFieldCount);
		}

	private:

		/// 
		void OnDespawnTimer();

		void TriggerNextAutoAttack();

		void OnAttackSwing();

	public:
		TimerQueue& GetTimers() const { return m_timers; }

		UnitMover& GetMover() const { return *m_mover; }

	protected:
		typedef LinearSet<const GameUnitS*> AttackingUnitSet;

		TimerQueue& m_timers;
		Countdown m_despawnCountdown;
		std::unique_ptr<UnitMover> m_mover;
		Countdown m_attackSwingCountdown;
		GameTime m_lastMainHand = 0, m_lastOffHand = 0;
		Countdown m_regenCountdown;
		GameTime m_lastManaUse;

		std::weak_ptr<GameUnitS> m_victim;

		std::set<const proto::SpellEntry*> m_spells;
		std::unique_ptr<SpellCast> m_spellCast;

		std::map<uint32, GameTime> m_spellCooldowns;
		std::map<uint32, GameTime> m_spellCategoryCooldowns;

		AttackingUnitSet m_attackingUnits;

		NetUnitWatcherS* m_netUnitWatcher = nullptr;
		mutable Vector3 m_lastPosition;

		std::vector<std::unique_ptr<AuraContainer>> m_auras;

		typedef std::array<float, unit_mod_type::End> UnitModTypeArray;
		typedef std::array<UnitModTypeArray, unit_mods::End> UnitModArray;
		UnitModArray m_unitMods;

	private:
		friend io::Writer& operator << (io::Writer& w, GameUnitS const& object);
		friend io::Reader& operator >> (io::Reader& r, GameUnitS& object);
	};

	io::Writer& operator << (io::Writer& w, GameUnitS const& object);
	io::Reader& operator >> (io::Reader& r, GameUnitS& object);
}
