// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "animation_context.h"

namespace mmo
{
	/// @brief The clip family an airborne unit plays. Mapped to animation slots by the
	///	animation controller (kept free of the client_data protobuf types so the rule can be
	///	tested in the headless build).
	namespace airborne_slot
	{
		enum Type
		{
			/// The one-shot jump start (ascending right after a jump).
			JumpStart,
			/// The falling loop.
			Fall,
			/// The glide pose while the glider is open.
			Glide
		};
	}

	/// @brief Selects the clip family for an airborne unit.
	/// @param ctx Animation input of this frame; only gliding and verticalVelocity are read.
	/// @param jumpStartPlaying Whether the jump start clip is bound and has not ended yet.
	/// @return Glide while gliding (also when an updraft lifts the unit), JumpStart while
	///	ascending with a running jump start, Fall otherwise.
	[[nodiscard]] airborne_slot::Type SelectAirborneSlot(const AnimationContext& ctx, bool jumpStartPlaying);
}
