// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_roster.h"

#include "bot_chat/chat_director.h"

#include <string>
#include <vector>

namespace mmo
{
	class BotBrain;
	class BotSession;

	namespace proto
	{
		class Project;
	}

	/// Name of the public channel bots talk in.
	constexpr const char* GeneralChannelName = "General";

	/// Lets the chat director talk through one swarm bot.
	class SwarmChatParticipant final : public IChatParticipant
	{
	public:
		SwarmChatParticipant(BotSession& session, const BotBrain& brain, const BotRosterEntry& roster, const proto::Project* project);

		[[nodiscard]] const std::string& GetCharacterName() const override { return m_roster.characterName; }
		[[nodiscard]] bool IsInWorld() const override;
		[[nodiscard]] std::string DescribeSelf() const override;
		[[nodiscard]] std::vector<std::string> GetPartyMemberNames() const override;
		[[nodiscard]] uint32 GetGeneralChannelId() const override;
		void SendChat(ChatType type, const std::string& text, const std::string& target) override;

	private:
		BotSession& m_session;
		const BotBrain& m_brain;
		const BotRosterEntry& m_roster;
		const proto::Project* m_project;
	};

	/// Facts about the game world for the system prompt, read off the game data: the zones,
	/// races and classes that exist, so that bots name real places rather than invented ones.
	[[nodiscard]] std::string BuildGameFacts(const proto::Project& project);
}
