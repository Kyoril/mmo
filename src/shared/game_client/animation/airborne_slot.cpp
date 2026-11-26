// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "airborne_slot.h"

namespace mmo
{
	airborne_slot::Type SelectAirborneSlot(const AnimationContext& ctx, const bool jumpStartPlaying)
	{
		if (ctx.gliding)
		{
			return airborne_slot::Glide;
		}

		if (ctx.verticalVelocity > 0.0f && jumpStartPlaying)
		{
			return airborne_slot::JumpStart;
		}

		return airborne_slot::Fall;
	}
}
