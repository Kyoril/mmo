// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <set>

#include "game_object_s.h"
#include "spell_cast.h"
#include "spell_target_map.h"
#include "unit_mover.h"
#include "base/countdown.h"

namespace mmo
{
	namespace proto
	{
		class SpellEntry;
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

	public:
		GameUnitS(const proto::Project& project,
			TimerQueue& timers);

		virtual ~GameUnitS() override = default;

		virtual void Initialize() override;

		void TriggerDespawnTimer(GameTime despawnDelay);

		virtual void WriteObjectUpdateBlock(io::Writer& writer, bool creation = true) const override;

		virtual void WriteValueUpdateBlock(io::Writer& writer, bool creation = true) const override;

		virtual bool HasMovementInfo() const override { return true; }

	public:
		bool HasSpell(uint32 spellId) const;

		void SetInitialSpells(const std::vector<uint32>& spellIds);

		void AddSpell(uint32 spellId);

		void RemoveSpell(uint32 spellId);

		SpellCastResult CastSpell(const SpellTargetMap& target, const proto::SpellEntry& spell, uint32 castTimeMs);

	protected:
		virtual void OnSpellLearned(const proto::SpellEntry& spell) {}

		virtual void OnSpellUnlearned(const proto::SpellEntry& spell) {}

		virtual void OnSpellCastEnded(bool succeeded);

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

	protected:
		TimerQueue& m_timers;
		Countdown m_despawnCountdown;
		std::unique_ptr<UnitMover> m_mover;
		Countdown m_attackSwingCountdown;
		GameTime m_lastMainHand, m_lastOffHand;
		Countdown m_regenCountdown;
		GameTime m_lastManaUse;

		std::set<const proto::SpellEntry*> m_spells;
		std::unique_ptr<SpellCast> m_spellCast;
	};
}
