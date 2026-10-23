// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#include "player_duel.h"
#include "objects/game_unit_s.h"
#include "game/player_faction.h"

namespace mmo
{
	PlayerDuel::PlayerDuel(std::shared_ptr<GameUnitS> first, std::shared_ptr<GameUnitS> second, Notify notify, GameTime now)
		: m_players{first, second}, m_guids{first->GetGuid(), second->GetGuid()},
		m_names{first->GetName(), second->GetName()}, m_notify(std::move(notify)),
		m_flag((first->GetPosition() + second->GetPosition()) * 0.5f), m_deadline(now + 60000)
	{
	}

	void PlayerDuel::Start()
	{
		const std::weak_ptr<PlayerDuel> weak = shared_from_this();
		for (int i = 0; i < 2; ++i)
		{
			if (const auto player = m_players[i].lock())
			{
				player->SetDuelPartner(m_guids[1 - i], false);
				m_connections += player->duelDefeated.connect([weak, i]()
				{
					if (const auto duel = weak.lock()) { duel->Finish(duel->m_guids[1 - i], DuelResult::Cancelled); }
				});
			}
		}
		m_notify(m_guids[0], DuelEvent::Pending, 0, m_names[1], m_flag);
		m_notify(m_guids[1], DuelEvent::Invited, 0, m_names[0], m_flag);
	}

	bool PlayerDuel::IsValid() const
	{
		const auto a = m_players[0].lock(), b = m_players[1].lock();
		return a && b && a->IsAlive() && b->IsAlive()
			&& CanPlayerFactionsInteract(a->GetFactionTemplate(), b->GetFactionTemplate())
			&& a->GetWorldInstance() == b->GetWorldInstance()
			&& a->GetSquaredDistanceTo(b->GetPosition(), true) <= 100.0f * 100.0f;
	}

	void PlayerDuel::Send(DuelEvent event, uint8 first, uint8 second)
	{
		m_notify(m_guids[0], event, first, m_names[1], m_flag);
		m_notify(m_guids[1], event, second, m_names[0], m_flag);
	}

	void PlayerDuel::Accept(uint64 player, GameTime now)
	{
		if (m_finished || m_accepted || player != m_guids[1]) { return; }
		if (now >= m_deadline || !IsValid()) { Finish(0, DuelResult::Expired); return; }
		for (const auto& weak : m_players)
		{
			const auto unit = weak.lock();
			if (unit->IsInCombat() || unit->GetHealth() <= DuelHealthFloor(unit->GetMaxHealth()))
			{
				Finish(0, DuelResult::Cancelled); return;
			}
		}
		m_accepted = true;
		m_deadline = now + 3000;
		m_lastCount = 3;
		Send(DuelEvent::Countdown, 3, 3);
	}

	void PlayerDuel::Update(GameTime now)
	{
		if (m_finished) { return; }
		if (!IsValid()) { Finish(0, DuelResult::Cancelled); return; }
		if (!m_accepted)
		{
			if (now >= m_deadline) { Finish(0, DuelResult::Expired); }
			return;
		}
		if (m_active) { return; }
		if (now >= m_deadline)
		{
			m_active = true;
			for (int i = 0; i < 2; ++i)
			{
				m_players[i].lock()->SetDuelPartner(m_guids[1 - i], true);
			}
			Send(DuelEvent::Active);
			return;
		}
		const auto count = static_cast<uint8>((m_deadline - now + 999) / 1000);
		if (count != m_lastCount) { m_lastCount = count; Send(DuelEvent::Countdown, count, count); }
	}

	void PlayerDuel::Cancel(uint64 player, DuelResult reason)
	{
		if (player != m_guids[0] && player != m_guids[1]) { return; }
		Finish(m_active ? m_guids[player == m_guids[0] ? 1 : 0] : 0, reason);
	}

	void PlayerDuel::Finish(uint64 winner, DuelResult reason)
	{
		if (m_finished) { return; }
		m_finished = true;
		for (int i = 0; i < 2; ++i)
		{
			if (const auto unit = m_players[i].lock())
			{
				unit->SetDuelPartner(0, false);
				unit->StopAttack();
				unit->CancelCast(spell_interrupt_flags::Any);
				unit->RemoveHarmfulAurasFromCaster(m_guids[1 - i]);
				unit->SetInCombat(false, true);
				if (winner != 0 && unit->GetGuid() != winner && unit->IsAlive())
				{
					unit->SetStandState(unit_stand_state::Kneel);
					const std::weak_ptr<GameUnitS> loser = unit;
					unit->GetTimers().AddEvent([loser]()
					{
						if (const auto player = loser.lock())
						{
							if (player->IsAlive() && player->GetStandState() == unit_stand_state::Kneel)
							{
								player->SetStandState(unit_stand_state::Stand);
							}
						}
					}, unit->GetTimers().GetNow() + 5000);
				}
			}
		}
		m_connections.disconnect();
		Send(DuelEvent::Finished,
			static_cast<uint8>(winner ? (winner == m_guids[0] ? DuelResult::Won : DuelResult::Lost) : reason),
			static_cast<uint8>(winner ? (winner == m_guids[1] ? DuelResult::Won : DuelResult::Lost) : reason));
	}
}
