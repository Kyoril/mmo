// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include "bot_companion_types.h"

namespace mmo
{
	class BotCompanionStateMachine final
	{
	public:
		[[nodiscard]] BotCompanionResult Evaluate(const BotCompanionSnapshot& snapshot) const;
	};
}
