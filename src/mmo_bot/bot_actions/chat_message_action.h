// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_action.h"
#include "bot_context.h"

#include "game/chat_type.h"

#include <string>

namespace mmo
{
	/// Action that sends a chat message.
	class ChatMessageAction final : public BotAction
	{
	public:
		explicit ChatMessageAction(
			std::string message,
			ChatType chatType = ChatType::Say,
			std::string target = "")
			: m_message(std::move(message))
			, m_chatType(chatType)
			, m_target(std::move(target))
		{
		}

		std::string GetDescription() const override
		{
			return "Send chat message: \"" + m_message + "\"";
		}

		ActionResult Execute(BotContext& context) override
		{
			if (!context.IsWorldReady())
			{
				return ActionResult::Failed;
			}

			context.SendChatMessage(m_message, m_chatType, m_target);
			return ActionResult::Success;
		}

		bool CanExecute(const BotContext& context, std::string& outReason) const override
		{
			if (!context.IsWorldReady())
			{
				outReason = "Bot is not in the world yet";
				return false;
			}

			return true;
		}

	private:
		std::string m_message;
		ChatType m_chatType;
		std::string m_target;
	};
}
