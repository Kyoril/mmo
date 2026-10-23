// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// Private duel notifications sent only to the two participants.
	enum class DuelEvent : uint8 { Pending, Invited, Countdown, Active, Finished, Error };
	/// The result is relative to the recipient. Cancelled also covers disconnects and transfers.
	enum class DuelResult : uint8 { Cancelled, Won, Lost, Declined, Expired };

	/// The smallest whole health value at or above one percent, always alive.
	inline uint32 DuelHealthFloor(const uint32 maxHealth)
	{
		return maxHealth == 0 ? 1 : maxHealth / 100 + (maxHealth % 100 != 0);
	}
}
