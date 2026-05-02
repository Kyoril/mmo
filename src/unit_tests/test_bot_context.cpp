// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "catch.hpp"

#include "mmo_bot/bot_context.h"

namespace mmo
{
	TEST_CASE("bot context preserves a known map when world sync reports zero", "[bot-context][map]")
	{
		BotConfig config;
		BotContext context(std::shared_ptr<BotRealmConnector>{}, config, std::shared_ptr<BotNavService>{});

		CHECK_FALSE(context.HasCurrentMapId());
		CHECK(context.GetCurrentMapId() == 0u);

		context.UpdateCurrentMapIdFromWorldSync(0);
		CHECK_FALSE(context.HasCurrentMapId());
		CHECK(context.GetCurrentMapId() == 0u);

		context.SetCurrentMapId(7u);
		REQUIRE(context.HasCurrentMapId());
		CHECK(context.GetCurrentMapId() == 7u);

		context.UpdateCurrentMapIdFromWorldSync(0);
		CHECK(context.HasCurrentMapId());
		CHECK(context.GetCurrentMapId() == 7u);

		context.UpdateCurrentMapIdFromWorldSync(3u);
		CHECK(context.HasCurrentMapId());
		CHECK(context.GetCurrentMapId() == 3u);
	}
}
