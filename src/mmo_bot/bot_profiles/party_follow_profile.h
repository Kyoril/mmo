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
			ILOG("Party follow profile joined party leader_guid=" << partyLeaderGuid
				<< " member_count=" << memberCount
				<< " and will travel on the leader, switch to role anchors in combat, and regroup conservatively when anchors go stale");
		}

		void OnPartyLeft(BotContext& context) override
		{
			ILOG("Party follow profile left party and will hold until a new leader is available");
		}

	protected:
		void OnActivateImpl(BotContext& context) override
		{
			ILOG("Party follow profile activated - companion runtime follows the leader out of combat, switches to role-aware combat anchors, executes warrior tank actions inside the same runtime when configured, and regroups or holds conservatively on invalid anchors");
			QueueAction(std::make_shared<CompanionFollowAction>());
		}

		bool OnQueueEmpty(BotContext& context) override
		{
			QueueAction(std::make_shared<CompanionFollowAction>());
			return true;
		}
	};
}
