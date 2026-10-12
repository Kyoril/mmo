// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// Returns the bit that represents a class or race id in a class / race mask (spell classmask and
	/// racemask, quest requiredclasses and requiredraces).
	/// @param id The 0-based class or race id.
	/// @remarks Id N maps to bit N-1 and id 0 (Mage, Human) wraps to bit 31. This is the layout all
	///          authored data uses. It used to be computed as `1 << (id - 1)`, which is undefined
	///          behavior for id 0 and only produced bit 31 because x86 masks the shift count. Do not
	///          change the layout without migrating every authored mask.
	constexpr uint32 IdMaskBit(const uint32 id) noexcept
	{
		return 1u << ((id + 31u) % 32u);
	}
}
