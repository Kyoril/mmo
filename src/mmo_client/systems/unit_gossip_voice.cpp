// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "unit_gossip_voice.h"

#include "base/clock.h"
#include "game_client/game_unit_c.h"
#include "game_client/sound_entry_player.h"

namespace mmo
{
	namespace
	{
		/// Clicks 1 to (pissedClickThreshold - 1) play the normal gossip line; from
		/// this click on, the pissed line plays instead (if one is configured).
		constexpr uint32 pissedClickThreshold = 4;

		/// Time without a counted click after which the annoyance counter resets.
		constexpr GameTime annoyanceResetMs = 30000;
	}

	UnitGossipVoice& UnitGossipVoice::Get()
	{
		static UnitGossipVoice s_instance;
		return s_instance;
	}

	void UnitGossipVoice::Initialize(SoundEntryPlayer* player, const proto_client::ModelDataManager* models)
	{
		m_player = player;
		m_models = models;
		Reset();
	}

	void UnitGossipVoice::Reset()
	{
		m_pesteredGuid = 0;
		m_clickCount = 0;
		m_lastClickTime = 0;
		m_busyUntil = 0;
	}

	void UnitGossipVoice::OnUnitClicked(GameUnitC& unit)
	{
		if (!m_player || !m_models)
		{
			return;
		}

		const uint32 displayId = unit.Get<uint32>(object_fields::DisplayId);
		const proto_client::ModelDataEntry* model = m_models->getById(displayId);
		if (!model || model->gossip_sound_id() == 0)
		{
			return;
		}

		// While a gossip line is (likely) still playing, clicks are ignored entirely:
		// no sound and no annoyance increment. Time based on purpose - channels are
		// recycled, so a stored channel index could no longer belong to the line.
		const GameTime now = GetAsyncTimeMs();
		if (now < m_busyUntil)
		{
			return;
		}

		// Clicking a different unit or leaving this one alone for a while calms it down.
		if (unit.GetGuid() != m_pesteredGuid || now - m_lastClickTime > annoyanceResetMs)
		{
			m_pesteredGuid = unit.GetGuid();
			m_clickCount = 0;
		}

		++m_clickCount;
		m_lastClickTime = now;

		uint32 soundId = model->gossip_sound_id();
		if (m_clickCount >= pissedClickThreshold && model->gossip_pissed_sound_id() != 0)
		{
			soundId = model->gossip_pissed_sound_id();
		}

		// 3D playback at the unit's position; out-of-range non-looped 3D sounds are
		// not started at all, in which case we also don't block follow-up clicks.
		if (m_player->PlayEntry(soundId, unit.GetPosition()) == InvalidChannel)
		{
			return;
		}

		m_busyUntil = now + static_cast<GameTime>(m_player->GetEntryLength(soundId) * 1000.0f);
	}
}
