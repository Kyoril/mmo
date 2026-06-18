// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "no_cast_state.h"

namespace mmo
{
	void NoCastState::Activate()
	{
	}

	SpellCastResult NoCastState::StartCast(SpellCast& cast, const proto::SpellEntry& spell,
		const SpellTargetMap& target, GameTime castTime, bool doReplacePreviousCast, uint64 itemGuid)
	{
		CastSpell(
			cast,
			spell,
			target,
			castTime,
			itemGuid,
			false
		);

		return spell_cast_result::CastOkay;
	}

	void NoCastState::StopCast(SpellInterruptFlags reason, GameTime interruptCooldown)
	{
	}

	void NoCastState::OnUserStartsMoving()
	{
	}

	void NoCastState::FinishChanneling()
	{
	}
}
