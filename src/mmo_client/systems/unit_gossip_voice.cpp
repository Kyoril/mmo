// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "unit_gossip_voice.h"

#include "base/clock.h"
#include "base/timer_queue.h"
#include "game_client/game_unit_c.h"
#include "game_client/object_mgr.h"
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

		/// After a dialog closed, another dialog of the same npc may open within this
		/// window (gossip -> vendor etc.) without counting as "closed for real".
		constexpr GameTime goodbyeGraceMs = 500;
	}

	UnitGossipVoice& UnitGossipVoice::Get()
	{
		static UnitGossipVoice s_instance;
		return s_instance;
	}

	void UnitGossipVoice::Initialize(SoundEntryPlayer* player, const proto_client::ModelDataManager* models, TimerQueue* timers)
	{
		m_player = player;
		m_models = models;
		m_timers = timers;
		Reset();
	}

	void UnitGossipVoice::Reset()
	{
		m_pesteredGuid = 0;
		m_clickCount = 0;
		m_lastClickTime = 0;
		m_busyUntil = 0;

		for (ObjectGuid& openGuid : m_openDialogGuids)
		{
			openGuid = 0;
		}
		m_pendingGoodbyeGuid = 0;
		++m_goodbyeGeneration;
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

	void UnitGossipVoice::OnNpcDialogOpened(const npc_dialog_source::Type source, const ObjectGuid guid)
	{
		if (guid == 0)
		{
			return;
		}

		m_openDialogGuids[source] = guid;

		if (m_pendingGoodbyeGuid == guid)
		{
			m_pendingGoodbyeGuid = 0;
			++m_goodbyeGeneration;
			DLOG("Goodbye voice line cancelled: follow-up dialog opened for npc " << guid);
		}
	}

	void UnitGossipVoice::OnNpcDialogClosed(const npc_dialog_source::Type source, const ObjectGuid guid)
	{
		if (guid == 0)
		{
			return;
		}

		if (m_openDialogGuids[source] == guid)
		{
			m_openDialogGuids[source] = 0;
		}

		for (const ObjectGuid openGuid : m_openDialogGuids)
		{
			if (openGuid == guid)
			{
				// Another dialog window of the same npc is still open - the
				// conversation continues, this was only a window transition.
				return;
			}
		}

		if (!m_player || !m_models || !m_timers)
		{
			return;
		}

		m_pendingGoodbyeGuid = guid;
		const uint32 generation = ++m_goodbyeGeneration;
		m_timers->AddEvent([generation]() { UnitGossipVoice::Get().PlayPendingGoodbye(generation); }, GetAsyncTimeMs() + goodbyeGraceMs);
	}

	void UnitGossipVoice::PlayPendingGoodbye(const uint32 generation)
	{
		if (generation != m_goodbyeGeneration || m_pendingGoodbyeGuid == 0)
		{
			return;
		}

		const ObjectGuid guid = m_pendingGoodbyeGuid;
		m_pendingGoodbyeGuid = 0;

		const std::shared_ptr<GameUnitC> unit = ObjectMgr::Get<GameUnitC>(guid);
		if (!unit || !unit->IsAlive())
		{
			return;
		}

		const GameTime now = GetAsyncTimeMs();
		if (now < m_busyUntil)
		{
			DLOG("Goodbye voice line skipped for npc " << guid << ": another voice line is still playing");
			return;
		}

		const uint32 displayId = unit->Get<uint32>(object_fields::DisplayId);
		const proto_client::ModelDataEntry* model = m_models->getById(displayId);
		if (!model || model->goodbye_sound_id() == 0)
		{
			return;
		}

		if (m_player->PlayEntry(model->goodbye_sound_id(), unit->GetPosition()) == InvalidChannel)
		{
			return;
		}

		m_busyUntil = now + static_cast<GameTime>(m_player->GetEntryLength(model->goodbye_sound_id()) * 1000.0f);
		DLOG("Goodbye voice line played for npc " << guid);
	}
}
