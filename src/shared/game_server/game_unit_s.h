// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <memory>
#include <set>

#include "game/auto_attack.h"
#include "game_object_s.h"
#include "spell_cast.h"
#include "game/spell_target_map.h"
#include "unit_mover.h"
#include "base/countdown.h"

namespace mmo
{
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

	namespace damage_flags
	{
		enum Type
		{
			None = 0,

			Crit = 1
		};
	}

	typedef damage_flags::Type DamageFlags;

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

	public:
		virtual void SetLevel(uint32 newLevel);

		virtual void Relocate(const Vector3& position, const Radian& facing) override;

		virtual void ApplyMovementInfo(const MovementInfo& info) override;

	public:
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
	};
}
