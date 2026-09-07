// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo
{
	/// Exit codes of a bot session, kept stable so that agents and scripts can rely on them.
	namespace bot_exit_code
	{
		enum Type
		{
			Success = 0,
			ScenarioFailed = 1,
			SetupFailed = 2,
			Timeout = 3,
			Disconnected = 4,
		};
	}
}
