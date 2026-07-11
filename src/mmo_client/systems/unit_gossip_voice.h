// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"

#include "client_data/project.h"

namespace mmo
{
	class GameUnitC;
	class SoundEntryPlayer;

	/// @brief Plays a gossip voice line at a friendly NPC's position when the player
	/// clicks (selects) or interacts with it. The voice line is configured per model
	/// display id (ModelDataEntry::gossip_sound_id); after repeatedly pestering the
	/// same unit, an annoyed line (gossip_pissed_sound_id) plays instead.
	///
	/// The system is fully client-local. While a line is (likely) still playing,
	/// further clicks are ignored entirely - no sound and no annoyance increment.
	/// The annoyance counter resets when a different unit is clicked or when the
	/// unit was left alone for a while.
	class UnitGossipVoice final : public NonCopyable
	{
	public:
		/// @brief Gets the singleton instance.
		static UnitGossipVoice& Get();

	public:
		/// @brief Initializes the service with its dependencies. Passing nullptrs disables it.
		void Initialize(SoundEntryPlayer* player, const proto_client::ModelDataManager* models);

		/// @brief Notifies the service that the player clicked a friendly, alive NPC.
		/// Plays the gossip (or pissed) voice line of the unit's display model, if any.
		void OnUnitClicked(GameUnitC& unit);

		/// @brief Clears all click/throttle state (called when leaving the world).
		void Reset();

	private:
		UnitGossipVoice() = default;

	private:
		SoundEntryPlayer* m_player = nullptr;
		const proto_client::ModelDataManager* m_models = nullptr;

		/// Guid of the unit the player is currently pestering.
		ObjectGuid m_pesteredGuid = 0;
		/// Counted clicks on that unit.
		uint32 m_clickCount = 0;
		/// Time of the last counted click (for the annoyance decay).
		GameTime m_lastClickTime = 0;
		/// Until this time the current voice line is (likely) still playing.
		GameTime m_busyUntil = 0;
	};
}
