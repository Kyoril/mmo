// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#include "player.h"
#include "player_manager.h"
#include "game/player_faction.h"
#include "game_server/world/world_instance.h"

namespace mmo
{
	static void SendDuelUpdate(Player& player, DuelEvent event, uint8 detail, uint64 opponent, const String& name, const Vector3& position)
	{
		player.SendPacket([&](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::DuelUpdate);
			packet << io::write<uint8>(event) << io::write<uint8>(detail) << io::write<uint64>(opponent)
				<< io::write_string(name) << io::write<float>(position.x) << io::write<float>(position.y) << io::write<float>(position.z);
			packet.Finish();
		});
	}

	void Player::OnDuelRequest(io::Reader& reader)
	{
		uint64 guid;
		if (!(reader >> io::read<uint64>(guid))) { return; }
		const auto other = m_manager.GetPlayerByCharacterGuid(guid);
		const bool busy = (m_duelSession && !m_duelSession->IsFinished())
			|| (other && other->m_duelSession && !other->m_duelSession->IsFinished());
		if (!other || other.get() == this || busy || !m_spawned || !other->m_spawned
			|| m_worldInstance != other->m_worldInstance
			|| !CanPlayerFactionsInteract(m_character->GetFactionTemplate(), other->m_character->GetFactionTemplate())
			|| !m_character->IsAlive() || !other->m_character->IsAlive()
			|| m_character->IsInCombat() || other->m_character->IsInCombat()
			|| m_character->GetHealth() <= DuelHealthFloor(m_character->GetMaxHealth())
			|| other->m_character->GetHealth() <= DuelHealthFloor(other->m_character->GetMaxHealth())
			|| m_character->GetSquaredDistanceTo(other->m_character->GetPosition(), true) > 30.0f * 30.0f)
		{
			SendDuelUpdate(*this, DuelEvent::Error, 0, guid, "", Vector3::Zero);
			return;
		}
		const std::weak_ptr<Player> a = shared_from_this(), b = other;
		auto notify = [a, b](uint64 recipient, DuelEvent event, uint8 detail, const String& name, const Vector3& position)
		{
			const auto first = a.lock(), second = b.lock();
			if (first && first->m_character->GetGuid() == recipient)
			{
				SendDuelUpdate(*first, event, detail, second ? second->m_character->GetGuid() : 0, name, position);
			}
			else if (second)
			{
				SendDuelUpdate(*second, event, detail, first ? first->m_character->GetGuid() : 0, name, position);
			}
		};
		m_duelSession = std::make_shared<PlayerDuel>(m_character, other->m_character, std::move(notify), GetAsyncTimeMs());
		other->m_duelSession = m_duelSession;
		m_duelSession->Start();
		m_duelTimer.SetEnd(GetAsyncTimeMs() + 100);
	}

	void Player::OnDuelResponse(io::Reader& reader)
	{
		uint8 accept;
		if (!(reader >> io::read<uint8>(accept)) || !m_duelSession) { return; }
		const auto session = m_duelSession;
		if (accept == 1) { session->Accept(m_character->GetGuid(), GetAsyncTimeMs()); }
		else if (accept == 0) { session->Cancel(m_character->GetGuid(), DuelResult::Declined); }
	}

	void Player::TickDuel()
	{
		const auto session = m_duelSession;
		if (!session) { return; }
		session->Update(GetAsyncTimeMs());
		if (!session->IsFinished()) { m_duelTimer.SetEnd(GetAsyncTimeMs() + 100); }
	}
}
