// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// @brief Flags describing the behaviour of a well-known chat channel.
	namespace chat_channel_flags
	{
		enum Type
		{
			/// @brief No special behaviour.
			None = 0x00,

			/// @brief Newly created characters automatically join this channel, and existing
			///        characters automatically join it on their next login unless they have
			///        explicitly left it before.
			JoinByDefault = 0x01,

			/// @brief Channel is moderated (reserved for future use).
			Moderated = 0x02,
		};
	}
}
