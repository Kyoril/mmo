// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// Game master levels as stored per account in the login database (gm_level). A higher
	/// level includes every right of the lower ones.
	namespace gm_level
	{
		enum Type : uint8
		{
			/// A regular player account.
			Player = 0,

			/// Basic game master: movement, teleport, inspection and moderation commands.
			Gm = 1,

			/// Senior game master: commands that alter characters (items, levels, spells).
			SeniorGm = 2,

			/// Operator: realm administration, such as scheduling a realm shutdown.
			Operator = 3,
		};
	}
}
