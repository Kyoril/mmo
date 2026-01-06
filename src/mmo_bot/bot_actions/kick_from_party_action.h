// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_action.h"
#include "log/default_log_levels.h"

#include <string>


namespace mmo
{
	/// @brief Action that kicks a player from the party.
	/// @details Only the party leader can kick members from the party.
	class KickFromPartyAction final : public IBotAction
	{
	public:
		/// @brief Creates a new KickFromPartyAction.
		/// @param playerName The name of the player to kick from the party.
		explicit KickFromPartyAction(std::string playerName)
			: m_playerName(std::move(playerName))
		{
		}

		/// @copydoc IBotAction::GetName
		[[nodiscard]] std::string_view GetName() const override
		{
			return "KickFromParty";
		}

		/// @copydoc IBotAction::Start
		void Start(BotContext& context) override
		{
			if (!context.IsInParty())
			{
				WLOG("KickFromPartyAction: Bot is not in a party");
				return;
			}

			if (!context.IsPartyLeader())
			{
				WLOG("KickFromPartyAction: Bot is not the party leader, cannot kick members");
				return;
			}

			if (m_playerName.empty())
			{
				WLOG("KickFromPartyAction: No player name specified");
				return;
			}

			DLOG("Kicking player '" << m_playerName << "' from party...");
			context.KickFromParty(m_playerName);
		}

		/// @copydoc IBotAction::Update
		[[nodiscard]] ActionResult Update(BotContext& context, float deltaSeconds) override
		{
			// This is an instant action - completes immediately after sending the kick request
			return ActionResult::Completed;
		}

	private:
		/// @brief The name of the player to kick from the party.
		std::string m_playerName;
	};
}
