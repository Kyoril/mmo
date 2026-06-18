// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/countdown.h"
#include "game_server/spells/spell_cast.h"
#include "game_server/spells/spell_cast_context.h"

#include <memory>

namespace mmo
{
	/// Owns the channel countdown, ChannelStart/ChannelUpdate packets, and fires
	/// SpellCast::ended on completion. Created by SingleCastState after effects are
	/// applied for channeled spells; replaces SingleCastState as the active CastState.
	class ChannelingCastState final
		: public CastState
		, public std::enable_shared_from_this<ChannelingCastState>
	{
	public:
		explicit ChannelingCastState(
			SpellCast& cast,
			const proto::SpellEntry& spell,
			SpellCastContext context,
			GameTime remainingMs,
			scoped_connection onTargetDied,
			scoped_connection onTargetRemoved);

		// CastState overrides
		void Activate() override;

		SpellCastResult StartCast(
			SpellCast& cast,
			const proto::SpellEntry& spell,
			const SpellTargetMap& target,
			GameTime castTime,
			bool doReplacePreviousCast,
			uint64 itemGuid) override;

		void StopCast(SpellInterruptFlags reason, GameTime interruptCooldown = 0) override;

		void OnUserStartsMoving() override;

		void FinishChanneling() override;

	private:
		void EndChanneling(bool succeeded);

	private:
		SpellCast& m_cast;
		const proto::SpellEntry& m_spell;
		SpellCastContext m_context;
		GameTime m_remainingMs;
		Countdown m_countdown;
		scoped_connection m_onTargetDied;
		scoped_connection m_onTargetRemoved;
		bool m_hasFinished{ false };
		bool m_endNotified{ false };
		std::shared_ptr<ChannelingCastState> m_selfHold;
	};
}
