// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game/spell_target_map.h"
#include "base/signal.h"
#include "base/timer_queue.h"
#include "shared/proto_data/spells.pb.h"

#include <memory>

namespace mmo
{
	class GameUnitS;

	class SpellCast;

	class CastState
	{
	public:
		virtual ~CastState() { }

		virtual void Activate() = 0;

		virtual SpellCastResult StartCast(
			SpellCast& cast,
			const proto::SpellEntry& spell,
			const SpellTargetMap& target,
			GameTime castTime,
			bool doReplacePreviousCast,
			uint64 itemGuid
		) = 0;

		virtual void StopCast(SpellInterruptFlags reason, GameTime interruptCooldown = 0) = 0;

		/// Drops the cast without telling anyone about it, for use when the casting unit is
		/// leaving the world. StopCast is the wrong tool there: it sends SpellFailure to the
		/// client and fires the ended signal, and doing either while the caster is being torn
		/// down reaches into an object that is already going away. Nothing needs to be notified
		/// of a cast belonging to a unit that no longer exists.
		virtual void AbandonCast() {}

		virtual void OnUserStartsMoving() = 0;

		virtual void FinishChanneling() = 0;

		/// @returns The spell that is currently being cast or channeled by this state,
		///          or nullptr if no spell is active (NoCastState).
		virtual const proto::SpellEntry* GetSpell() const = 0;
	};

	/// Creates and activates a cast state for the given spell.
	/// @returns CastOkay when the cast actually started. A spell that fails validation reports
	///          its failure here rather than pretending to have started - callers rely on this to
	///          tell a real cast from one that died during activation.
	SpellCastResult CastSpell(
		SpellCast& cast,
		const proto::SpellEntry& spell,
		const SpellTargetMap& target,
		GameTime castTime,
		uint64 itemGuid,
		bool isProc = false
	);

	class SpellCast
	{
	public:
		explicit SpellCast(TimerQueue& timer, GameUnitS& executor);

		GameUnitS& GetExecuter() const { return m_executor; }

		TimerQueue& GetTimerQueue() const { return m_timerQueue; }

		SpellCastResult StartCast(
			const proto::SpellEntry& spell,
			const SpellTargetMap& target,
			GameTime castTime,
			bool isProc, 
			uint64 itemGuid);

		void StopCast(SpellInterruptFlags reason, GameTime interruptCooldown = 0) const;

		void OnUserStartsMoving();

		void SetState(const std::shared_ptr<CastState>& castState);

		/// Drops any cast in progress silently, for use when the casting unit leaves the world.
		void AbandonCast();

		void FinishChanneling();

		/// @returns The spell currently being cast or channeled, or nullptr if idle.
		const proto::SpellEntry* GetSpell() const { return m_castState->GetSpell(); }

		int32 CalculatePowerCost(const proto::SpellEntry& spell) const;

	public:
		signal<void(bool)> ended;

	private:

		TimerQueue& m_timerQueue;
		GameUnitS& m_executor;
		std::shared_ptr<CastState> m_castState;
	};
}
