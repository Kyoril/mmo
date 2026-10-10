// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "channeling_cast_state.h"
#include "spell_interrupt_rules.h"

#include "game_server/objects/game_unit_s.h"
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
		GameUnitS* unitTarget)
		: m_cast(cast)
		, m_spell(spell)
		, m_context(std::move(context))
		, m_remainingMs(remainingMs)
		, m_countdown(cast.GetTimerQueue())
		, m_unitTarget(unitTarget)
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

		// Subscribed here rather than inherited from SingleCastState: those connections call back
		// into the SingleCastState, which would end the cast without ever ending the channel.
		if (m_unitTarget)
		{
			m_onTargetDied = m_unitTarget->killed.connect([this](GameUnitS*) { OnTargetLost(); });
			m_onTargetRemoved = m_unitTarget->despawned.connect([this](GameObjectS&) { OnTargetLost(); });
			m_unitTarget = nullptr;
		}
	}

	void ChannelingCastState::OnTargetLost()
	{
		// EndChanneling releases m_selfHold, which may be the last reference.
		auto strongThis = shared_from_this();
		EndChanneling(false);
	}

	SpellCastResult ChannelingCastState::StartCast(
		SpellCast& cast,
		const proto::SpellEntry& spell,
		const SpellTargetMap& target,
		const GameTime castTime,
		bool /*doReplacePreviousCast*/,
		const uint64 itemGuid)
	{
		// A channel is already in progress; cannot start another cast.
		if (!m_hasFinished)
		{
			return spell_cast_result::FailedSpellInProgress;
		}

		// A finished channel stays installed until the next cast replaces it, just like a finished
		// SingleCastState. This went unnoticed while a channel's first periodic trigger swapped it
		// out for the proc that tick cast, which accepted the next cast in its place.
		return CastSpell(cast, spell, target, castTime, itemGuid);
	}

	void ChannelingCastState::AbandonCast()
	{
		// Same teardown as SingleCastState::AbandonCast, and needed for the same reason: this state
		// keeps itself alive, owns a running channel countdown and subscriptions to the target, and
		// reaches its caster through a raw SpellCast&. Without this a unit destroyed mid-channel
		// leaves an armed state that dereferences the dead caster when the channel would have ended.
		auto strongThis = shared_from_this();

		// Clients were told this channel started, and only ChannelUpdate(0) ends it for them: the
		// despawn broadcast that follows this hook does not reach a client that keeps seeing the
		// caster, e.g. its own client when the caster merely leaves this instance.
		if (!m_hasFinished)
		{
			SendChannelEnded();
		}

		m_hasFinished = true;
		m_endNotified = true;

		m_countdown.Cancel();

		m_onTargetDied.disconnect();
		m_onTargetRemoved.disconnect();

		m_selfHold.reset();
	}

	void ChannelingCastState::StopCast(SpellInterruptFlags reason, const GameTime interruptCooldown)
	{
		if (m_hasFinished)
		{
			return;
		}

		if (reason == spell_interrupt_flags::Interrupt && IsUninterruptible(m_spell))
		{
			return;
		}

		// Apply the interrupt lockout cooldown to the channeled spell so it cannot be
		// recast immediately after being interrupted (mirrors SingleCastState::StopCast).
		if (interruptCooldown > 0)
		{
			m_cast.GetExecuter().SetCooldown(m_spell.id(), interruptCooldown);
		}

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

		SendChannelEnded();

		// A channel's work runs through the aura it put on its caster (a periodic trigger, as in
		// Fire Barrage or Dirge of the Grave). That aura lasts as long as the full channel, so an
		// interrupted channel kept ticking to the end unless the aura is taken away here.
		if (!succeeded)
		{
			GameUnitS& executer = m_cast.GetExecuter();
			executer.RemoveAllAurasFromCaster(executer.GetGuid(), m_spell.id());
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

	void ChannelingCastState::SendChannelEnded() const
	{
		// SendPacketFromCaster is a no-op without a world instance.
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
}
