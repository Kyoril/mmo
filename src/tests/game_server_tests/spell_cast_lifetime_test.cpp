// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"
#include "test_unit_factory.h"
#include "game_server/spells/spell_cast.h"
#include "game_server/spells/no_cast_state.h"
#include "asio/io_service.hpp"

using namespace mmo;

namespace
{
	class ReplacedCastState final : public CastState
	{
	public:
		ReplacedCastState(SpellCast& cast, bool& destroyed, bool& destroyedDuringStop)
			: m_cast(cast), m_destroyed(destroyed), m_destroyedDuringStop(destroyedDuringStop)
		{
		}

		~ReplacedCastState() override
		{
			m_destroyed = true;
		}

		void Activate() override
		{
		}
		SpellCastResult StartCast(SpellCast&, const proto::SpellEntry&, const SpellTargetMap&, GameTime, bool, uint64) override
		{
			return spell_cast_result::FailedSpellInProgress;
		}

		void StopCast(SpellInterruptFlags, GameTime) override
		{
			// Observe only stack references after replacement: the broken dispatcher can
			// destroy this state here, and reading another member would itself be unsafe.
			auto& destroyed = m_destroyed;
			auto& destroyedDuringStop = m_destroyedDuringStop;
			m_cast.SetState(std::make_shared<NoCastState>());
			destroyedDuringStop = destroyed;
		}

		void OnUserStartsMoving() override
		{
		}

		void FinishChanneling() override
		{
		}

		const proto::SpellEntry* GetSpell() const override
		{
			return nullptr;
		}

	private:
		SpellCast& m_cast;
		bool& m_destroyed;
		bool& m_destroyedDuringStop;
	};
}

TEST_CASE("Cancellation holds a cast state until reentrant replacement returns", "[spell][cast_lifetime]")
{
	asio::io_service io;
	TimerQueue timers(io);
	proto::Project project;
	auto unit = test::MakeUnit(project, timers);
	bool destroyed = false, destroyedDuringStop = false;
	SpellCast cast(timers, *unit);
	cast.SetState(std::make_shared<ReplacedCastState>(cast, destroyed, destroyedDuringStop));
	cast.StopCast(spell_interrupt_flags::Any);
	CHECK_FALSE(destroyedDuringStop);
	CHECK(destroyed);
}
