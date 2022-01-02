// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

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
