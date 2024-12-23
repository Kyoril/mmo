// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

namespace mmo
{
	namespace receive_state
	{
		enum Enum
		{
			Incomplete,
			Complete,
			Malformed
		};
	};

	typedef receive_state::Enum ReceiveState;
}
