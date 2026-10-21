// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "swarm_chat.h"

#include "bot_ai/bot_brain.h"
#include "bot_core/bot_realm_connector.h"
#include "bot_core/bot_session.h"
#include "bot_core/bot_unit.h"

#include "proto_data/project.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace mmo
{
	namespace
	{
		/// Zone names listed in the prompt. Enough to give bots places to talk about, few enough
		/// to keep every prompt cheap.
		constexpr int MaxZonesInFacts = 30;

		std::string toLower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(), [](const unsigned char c)
				{
					return static_cast<char>(std::tolower(c));
				});
			return text;
		}

		/// What the bot is busy with, in words a player would use.
		std::string describeActivity(const BotBrain& brain)
		{
			switch (brain.GetState())
			{
			case BotAiState::Dead:
				return "currently dead and running back to your body";
			case BotAiState::Combat:
				return "currently in a fight";
			default:
				break;
			}

			const std::string& action = brain.GetLastAction();
			if (action.find("rest") != std::string::npos || action.find("eat") != std::string::npos)
			{
				return "currently resting to get health back";
			}

			if (action.find("loot") != std::string::npos)
			{
				return "currently looting";
			}

			return "currently out grinding mobs for experience";
		}
	}

	SwarmChatParticipant::SwarmChatParticipant(BotSession& session, const BotBrain& brain,
		const BotRosterEntry& roster, const proto::Project* project)
		: m_session(session)
		, m_brain(brain)
		, m_roster(roster)
		, m_project(project)
	{
	}

	bool SwarmChatParticipant::IsInWorld() const
	{
		return m_session.IsWorldReady();
	}

	std::string SwarmChatParticipant::DescribeSelf() const
	{
		BotContext& context = m_session.GetContext();
		const BotUnit* self = context.GetSelf();

		std::ostringstream out;
		out << "a level " << (self ? self->GetLevel() : m_roster.targetLevel);

		if (m_project)
		{
			if (const auto* race = m_project->races.getById(m_roster.race))
			{
				out << ' ' << toLower(race->name());
			}

			if (const auto* characterClass = m_project->classes.getById(m_roster.characterClass))
			{
				out << ' ' << toLower(characterClass->name());
			}

			if (const auto* map = m_project->maps.getById(context.GetCurrentMapId()))
			{
				out << " in " << map->name();
			}
		}

		out << ", " << describeActivity(m_brain);

		if (self && self->GetTargetGuid() != 0)
		{
			if (const BotUnit* target = context.GetUnit(self->GetTargetGuid()); target && !target->GetName().empty())
			{
				out << " (target: " << target->GetName() << ")";
			}
		}

		return out.str();
	}

	std::vector<std::string> SwarmChatParticipant::GetPartyMemberNames() const
	{
		std::vector<std::string> names;

		BotRealmConnector& realm = m_session.GetRealm();
		if (!realm.IsInParty())
		{
			return names;
		}

		const uint64 selfGuid = m_session.GetContext().GetSelectedCharacterGuid();
		for (uint32 i = 0; i < realm.GetPartyMemberCount(); ++i)
		{
			const BotPartyMember* member = realm.GetPartyMember(i);
			if (member && member->guid != selfGuid && !member->name.empty())
			{
				names.push_back(member->name);
			}
		}

		return names;
	}

	uint32 SwarmChatParticipant::GetGeneralChannelId() const
	{
		return m_session.GetRealm().FindChatChannelId(GeneralChannelName);
	}

	void SwarmChatParticipant::SendChat(const ChatType type, const std::string& text, const std::string& target)
	{
		m_session.GetRealm().SendChatMessage(text, type, target);
	}

	bool SwarmChatParticipant::CanInvite() const
	{
		// The realm caps a normal party at five (PlayerGroup::IsFull).
		constexpr uint32 MaxPartySize = 5;

		const BotRealmConnector& realm = m_session.GetRealm();
		return !realm.IsInParty() || (realm.IsPartyLeader() && realm.GetPartyMemberCount() < MaxPartySize);
	}

	void SwarmChatParticipant::InviteToParty(const std::string& name)
	{
		m_session.GetRealm().InviteToParty(name);
	}

	std::string BuildGameFacts(const proto::Project& project)
	{
		std::ostringstream out;

		out << "Playable races:";
		bool first = true;
		for (const auto& race : project.races.getTemplates().entry())
		{
			out << (first ? " " : ", ") << race.name();
			first = false;
		}

		out << "\nClasses:";
		first = true;
		for (const auto& characterClass : project.classes.getTemplates().entry())
		{
			out << (first ? " " : ", ") << characterClass.name();
			first = false;
		}

		out << "\nSome zones:";
		first = true;
		int zones = 0;
		for (const auto& zone : project.zones.getTemplates().entry())
		{
			if (zone.name().empty() || zones >= MaxZonesInFacts)
			{
				continue;
			}

			out << (first ? " " : ", ") << zone.name();
			first = false;
			++zones;
		}

		out << "\nThe game is new and still in development, so a bug or an unfinished feature is nothing unusual.";
		return out.str();
	}
}
