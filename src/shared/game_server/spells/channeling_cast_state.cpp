// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "channeling_cast_state.h"

#include "game_server/spells/no_cast_state.h"
#include "base/clock.h"
#include "game/spell.h"

namespace mmo
{
	ChannelingCastState::ChannelingCastState(
		SpellCast& cast,
		const proto::SpellEntry& spell,
		SpellCastContext context,
		GameTime remainingMs,
		scoped_connection onTargetDied,
		scoped_connection onTargetRemoved)
		: m_cast(cast)
		, m_spell(spell)
		, m_context(std::move(context))
		, m_remainingMs(remainingMs)
		, m_countdown(cast.GetTimerQueue())
		, m_onTargetDied(std::move(onTargetDied))
		, m_onTargetRemoved(std::move(onTargetRemoved))
	{
	}

	void ChannelingCastState::Activate()
	{
		m_selfHold = shared_from_this();

		m_countdown.ended.connect([this]()
			{
				EndChanneling(true);
			});

		m_countdown.SetEnd(GetAsyncTimeMs() + m_remainingMs);
	}

	SpellCastResult ChannelingCastState::StartCast(
		SpellCast& /*cast*/,
		const proto::SpellEntry& /*spell*/,
		const SpellTargetMap& /*target*/,
		GameTime /*castTime*/,
		bool /*doReplacePreviousCast*/,
		uint64 /*itemGuid*/)
	{
		// A channel is already in progress; cannot start another cast.
		return spell_cast_result::FailedSpellInProgress;
	}

	void ChannelingCastState::StopCast(SpellInterruptFlags reason, const GameTime interruptCooldown)
	{
		EndChanneling(false);
	}

	void ChannelingCastState::OnUserStartsMoving()
	{
		if (m_hasFinished)
		{
			return;
		}

		// Channeled spells are interrupted by movement.
		if (m_spell.attributes(0) & spell_attributes::Channeled)
		{
			StopCast(spell_interrupt_flags::Movement, 0);
		}
	}

	void ChannelingCastState::FinishChanneling()
	{
		EndChanneling(true);
	}

	void ChannelingCastState::EndChanneling(bool succeeded)
	{
		if (m_hasFinished)
		{
			return;
		}

		m_hasFinished = true;

		// Disconnect target signals immediately so no re-entrant StopCast is possible.
		m_onTargetDied = scoped_connection{};
		m_onTargetRemoved = scoped_connection{};

		// Cancel the countdown in case we arrived here via StopCast (not timer expiry).
		m_countdown.Cancel();

		// Send ChannelUpdate(0) to inform clients the channel has ended.
		if (m_context.GetWorldInstance())
		{
			const uint64 casterId = m_cast.GetExecuter().GetGuid();
			m_context.SendPacketFromCaster(
				[casterId](game::OutgoingPacket& out_packet)
				{
					out_packet.Start(game::realm_client_packet::ChannelUpdate);
					out_packet
						<< io::write_packed_guid(casterId)
						<< io::write<GameTime>(0);
					out_packet.Finish();
				});
		}

		// Fire ended signal exactly once. Transition to NoCastState first, then
		// release m_selfHold — the SetState call may drop external references so
		// m_selfHold must outlive the signal fire.
		if (!m_endNotified)
		{
			m_endNotified = true;
			m_cast.ended(succeeded);
		}

		// Last statement: release our self-hold. If the signal fire above caused
		// SetState (dropping external refs), this is the final destructor trigger.
		m_selfHold.reset();
	}
}
