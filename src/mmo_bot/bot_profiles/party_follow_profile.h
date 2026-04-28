// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include "../bot_profile.h"
#include "../bot_actions/companion_follow_action.h"

namespace mmo
{
	class PartyFollowProfile final : public BotProfile
	{
	public:
		std::string GetName() const override
		{
			return "PartyFollow";
		}

		bool OnPartyInvitation(BotContext& context, const std::string& inviterName) override
		{
			ILOG("Party follow profile accepting invitation from " << inviterName);
			return true;
		}

		void OnPartyJoined(BotContext& context, uint64 partyLeaderGuid, uint32 memberCount) override
		{
			ILOG("Party follow profile joined party leader_guid=" << partyLeaderGuid << " member_count=" << memberCount);
		}

		void OnPartyLeft(BotContext& context) override
		{
			ILOG("Party follow profile left party and will hold until a new leader is available");
		}

	protected:
		void OnActivateImpl(BotContext& context) override
		{
			ILOG("Party follow profile activated - following the current party leader when available");
			QueueAction(std::make_shared<CompanionFollowAction>());
		}

		bool OnQueueEmpty(BotContext& context) override
		{
			QueueAction(std::make_shared<CompanionFollowAction>());
			return true;
		}
	};
}
