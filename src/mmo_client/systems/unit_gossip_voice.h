// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include "client_data/project.h"

namespace mmo
{
	class GameUnitC;
	class SoundEntryPlayer;
	class TimerQueue;

	/// Identifies which kind of NPC dialog window an open/close notification
	/// refers to. Each source can have at most one npc dialog open at a time.
	namespace npc_dialog_source
	{
		enum Type
		{
			Quest = 0,
			Vendor = 1,
			Trainer = 2,
			Bank = 3,

			Count_ = 4
		};
	}

	/// @brief Plays a gossip voice line at a friendly NPC's position when the player
	/// clicks (selects) or interacts with it. The voice line is configured per model
	/// display id (ModelDataEntry::gossip_sound_id); after repeatedly pestering the
	/// same unit, an annoyed line (gossip_pissed_sound_id) plays instead.
	///
	/// The system is fully client-local. While a line is (likely) still playing,
	/// further clicks are ignored entirely - no sound and no annoyance increment.
	/// The annoyance counter resets when a different unit is clicked or when the
	/// unit was left alone for a while.
	/// It also plays a goodbye voice line when the npc's last dialog window closes for real.
	class UnitGossipVoice final : public NonCopyable
	{
	public:
		/// @brief Gets the singleton instance.
		static UnitGossipVoice& Get();

	public:
		/// @brief Initializes the service with its dependencies. Passing nullptrs disables it.
		void Initialize(SoundEntryPlayer* player, const proto_client::ModelDataManager* models, TimerQueue* timers);

		/// @brief Notifies the service that the player clicked a friendly, alive NPC.
		/// Plays the gossip (or pissed) voice line of the unit's display model, if any.
		void OnUnitClicked(GameUnitC& unit);

		/// @brief Notifies the service that an NPC dialog window (gossip/quest, vendor,
		/// trainer, bank) has opened for the given npc. Cancels a pending goodbye line
		/// of that npc - the conversation continues instead of ending.
		void OnNpcDialogOpened(npc_dialog_source::Type source, ObjectGuid guid);

		/// @brief Notifies the service that an NPC dialog window has closed. If no other
		/// dialog window of the same npc remains open and none opens within a short grace
		/// period, the model's goodbye voice line plays at the npc's position.
		void OnNpcDialogClosed(npc_dialog_source::Type source, ObjectGuid guid);

		/// @brief Clears all click/throttle state (called when leaving the world).
		void Reset();

	private:
		UnitGossipVoice() = default;

	private:
		/// @brief Timer callback: plays the pending goodbye line if it wasn't cancelled.
		void PlayPendingGoodbye(uint32 generation);

	private:
		SoundEntryPlayer* m_player = nullptr;
		const proto_client::ModelDataManager* m_models = nullptr;
		TimerQueue* m_timers = nullptr;

		/// Guid of the unit the player is currently pestering.
		ObjectGuid m_pesteredGuid = 0;
		/// Counted clicks on that unit.
		uint32 m_clickCount = 0;
		/// Time of the last counted click (for the annoyance decay).
		GameTime m_lastClickTime = 0;
		/// Until this time the current voice line is (likely) still playing.
		GameTime m_busyUntil = 0;
		/// Unit whose voice line is (likely) still playing until m_busyUntil.
		ObjectGuid m_busyGuid = 0;

		/// Which npc guid each dialog source currently has open (0 = none).
		ObjectGuid m_openDialogGuids[npc_dialog_source::Count_] = {};
		/// Npc whose goodbye line is armed and waiting for the grace period.
		ObjectGuid m_pendingGoodbyeGuid = 0;
		/// Bumped to invalidate already-scheduled goodbye timer events.
		uint32 m_goodbyeGeneration = 0;
	};
}
