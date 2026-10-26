// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "game/spell_target_map.h"
#include "base/signal.h"
#include "base/timer_queue.h"
#include "shared/proto_data/spells.pb.h"

#include <memory>
#include <vector>

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

	/// Creates a cast state for the given spell and installs it as the cast's current state.
	/// Only for deliberate casts: procs go through SpellCast::StartCast, which never replaces the
	/// current state.
	/// @returns CastOkay when the cast actually started. A spell that fails validation reports
	///          its failure here rather than pretending to have started - callers rely on this to
	///          tell a real cast from one that died during activation.
	SpellCastResult CastSpell(
		SpellCast& cast,
		const proto::SpellEntry& spell,
		const SpellTargetMap& target,
		GameTime castTime,
		uint64 itemGuid
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

		/// Whether a state is still installed after callbacks which may have started another cast.
		bool IsCurrentState(const CastState& state) const
		{
			return m_castState.get() == &state;
		}

		/// Drops any cast in progress silently, for use when the casting unit leaves the world.
		/// This includes every cast still resolving on its own (see TrackDetachedCast).
		void AbandonCast();

		/// Registers a cast that resolves on its own after it stopped being the current state -
		/// a proc, or a finished cast whose projectile is still in flight. Such a cast reaches the
		/// caster when it resolves, so AbandonCast has to be able to stop it.
		/// @param castState The state. Only a weak reference is kept; the state keeps itself alive.
		void TrackDetachedCast(const std::shared_ptr<CastState>& castState);

		void FinishChanneling();

		/// @returns The spell currently being cast or channeled, or nullptr if idle.
		const proto::SpellEntry* GetSpell() const { return m_castState->GetSpell(); }

		int32 CalculatePowerCost(const proto::SpellEntry& spell) const;

	public:
		signal<void(bool)> ended;

	private:
		/// Resolves a proc or triggered cast beside the current state instead of in place of it.
		void StartProcCast(const proto::SpellEntry& spell, const SpellTargetMap& target, GameTime castTime, uint64 itemGuid);

	private:

		TimerQueue& m_timerQueue;
		GameUnitS& m_executor;
		std::shared_ptr<CastState> m_castState;
		/// Casts that may still be resolving beside the current state: procs, and finished casts
		/// with a projectile in flight. They keep themselves alive; these references only let
		/// AbandonCast reach them.
		std::vector<std::weak_ptr<CastState>> m_detachedCasts;
	};
}
