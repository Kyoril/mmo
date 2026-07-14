// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	namespace emote_type
	{
		enum Type
		{
			/// Plays a one-shot animation (e.g. /wave) and a chat line.
			OneShot = 0,

			/// Enters a persistent stand state with a looping animation (e.g. /sit, /sleep).
			Pose = 1,

			/// Persistent face pose layered over the body animation (e.g. /happy).
			Mood = 2,

			/// A selectable animation variant for a stand-state context (special idle, sit or
			/// sleep pose). Cycled via /pose, never triggered directly.
			PoseVariant = 3,

			/// Performing it cycles the pose variant of the current stand state, exactly like
			/// the /pose command. Exposes the /pose behaviour as a catalog entry so it shows up
			/// in the emote list and can be bound to an action bar button.
			CyclePose = 4,

			Count_
		};
	}

	namespace emote_flags
	{
		enum Type
		{
			None = 0,

			/// Every character knows this emote without having to unlock it.
			DefaultKnown = 1 << 0
		};
	}
}
