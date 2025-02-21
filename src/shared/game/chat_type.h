#pragma once

#include "base/typedefs.h"

namespace mmo
{
	/// @brief Enumerates known chat types.
	enum class ChatType : uint8
	{
		/// @brief Unknown chat type, should never be used.
		Unknown = 0,

		/// @brief A player says something. Executed on world node.
		Say,

		/// @brief A player yells something. Higher range than say. Executed on world node.
		Yell,

		/// @brief A players says something in group chat.
		Group,

		/// @brief A players says something in raid chat.
		Raid,

		/// @brief A player says something in guild chat.
		Guild,

		Whisper,

		/// @brief A player performs an emote. Executed on world node.
		Emote,

		/// @brief A players says something in a chat channel.
		Channel,

		/// @brief An npc says something.
		UnitSay,

		/// @brief An npc yells something.
		UnitYell,

		/// @brief An npc performs an emote.
		UnitEmote,
	};
}
