// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "game/auto_attack.h"
#include "mmo_bot/bot_action.h"
#include "mmo_bot/bot_context.h"
#include "mmo_bot/bot_actions/accept_party_invitation_action.h"
#include "mmo_bot/bot_actions/companion_follow_action.h"

#define private public
#include "mmo_bot/bot_profiles/party_follow_profile.h"
#undef private

namespace mmo
{
	namespace
	{
		class InterruptibleProbeAction final : public BotAction
		{
		public:
			explicit InterruptibleProbeAction(bool& aborted)
				: m_aborted(aborted)
			{
			}

			std::string GetDescription() const override
			{
				return "interruptible probe";
			}

			ActionResult Execute(BotContext& context) override
			{
				return ActionResult::InProgress;
			}

			void OnAbort(BotContext& context) override
			{
				m_aborted = true;
			}

			bool IsInterruptible() const override
			{
				return true;
			}

		private:
			bool& m_aborted;
		};
	}

	TEST_CASE("party follow queues an urgent accept action for party invites", "[bot-party-follow][invite]")
	{
		BotConfig config;
		config.characterClass = 1;

		auto realm = std::shared_ptr<BotRealmConnector>{};
		auto nav = std::shared_ptr<BotNavService>{};
		BotContext context(realm, config, nav);
		context.SetWorldReady(true);

		PartyFollowProfile profile;
		profile.OnActivate(context);

		bool aborted = false;
		profile.m_currentAction = std::make_shared<InterruptibleProbeAction>(aborted);
		profile.m_actionQueue.clear();

		const bool shouldAccept = profile.OnPartyInvitation(context, "Leader");
		REQUIRE(shouldAccept);
		CHECK(aborted);
		REQUIRE(profile.m_currentAction == nullptr);
		REQUIRE(profile.m_actionQueue.size() == 1);
		CHECK(profile.m_actionQueue.front()->GetDescription() == "Accept party invitation");
	}
}
