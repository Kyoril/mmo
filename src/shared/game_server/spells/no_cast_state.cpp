// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "no_cast_state.h"

namespace mmo
{
	void NoCastState::Activate()
	{
	}

	std::pair<SpellCastResult, SpellCasting*> NoCastState::StartCast(SpellCast& cast, const proto::SpellEntry& spell,
		const SpellTargetMap& target, GameTime castTime, bool doReplacePreviousCast, uint64 itemGuid)
	{
		SpellCasting& casting = CastSpell(
			cast,
			spell,
			std::move(target),
			castTime,
			itemGuid
		);

		return std::make_pair(spell_cast_result::CastOkay, &casting);
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
