#pragma once

#include "game/spell_target_map.h"
#include "base/signal.h"
#include "base/timer_queue.h"
#include "shared/proto_data/spells.pb.h"

#include <memory>

namespace mmo
{
	class GameUnitS;

	namespace spell_interrupt_flags
	{
		enum Type
		{
			/// Used when cast is cancelled for no specific reason (always interrupts the cast)
			Any = 0x00,

			/// Interrupted on movement
			Movement = 0x01,

			/// Affected by spell delay?
			PushBack = 0x02,

			/// Kick / Counter Spell
			Interrupt = 0x04,

			/// Interrupted on auto attack?
			AutoAttack = 0x08,

			/// Interrupted on direct damage
			Damage = 0x10
		};
	}

	typedef spell_interrupt_flags::Type SpellInterruptFlags;

	class SpellCasting
	{
	public:
		signal<void(bool)> ended;
	};

	class SpellCast;

	class CastState
	{
	public:
		virtual ~CastState() { }

		virtual void Activate() = 0;

		virtual std::pair<SpellCastResult, SpellCasting*> StartCast(
			SpellCast& cast,
			const proto::SpellEntry& spell,
			const SpellTargetMap& target,
			GameTime castTime,
			bool doReplacePreviousCast
		) = 0;

		virtual void StopCast(SpellInterruptFlags reason, GameTime interruptCooldown = 0) = 0;

		virtual void OnUserStartsMoving() = 0;

		virtual void FinishChanneling() = 0;
	};

	SpellCasting& CastSpell(
		SpellCast& cast,
		const proto::SpellEntry& spell,
		const SpellTargetMap& target,
		GameTime castTime
	);

	class SpellCast
	{
	public:
		explicit SpellCast(TimerQueue& timer, GameUnitS& executor);

		GameUnitS& GetExecuter() const { return m_executor; }

		TimerQueue& GetTimerQueue() const { return m_timerQueue; }

		std::pair<SpellCastResult, SpellCasting*> StartCast(
			const proto::SpellEntry& spell,
			const SpellTargetMap& target,
			GameTime castTime);

		void StopCast(SpellInterruptFlags reason, GameTime interruptCooldown = 0) const;

		void OnUserStartsMoving();

		void SetState(const std::shared_ptr<CastState>& castState);

		void FinishChanneling();

		int32 CalculatePowerCost(const proto::SpellEntry& spell) const;

	private:

		TimerQueue& m_timerQueue;
		GameUnitS& m_executor;
		std::shared_ptr<CastState> m_castState;
	};
}
