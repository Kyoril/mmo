#pragma once

#include "base/typedefs.h"

namespace io
{
	class Writer;
	class Reader;
}

namespace mmo
{
	namespace guild_event
	{
		enum Type
		{
			Promotion,
			Demotion,
			Motd,
			Joined,
			Left,
			Removed,
			LeaderChanged,
			Disbanded,
			LoggedIn,
			LoggedOut,

			Count_
		};
	}

	typedef guild_event::Type GuildEvent;

    namespace guild_rank_permissions
    {
		enum Type
		{
			None = 0,

			ReadGuildChat = 1 << 0,
			WriteGuildChat = 1 << 1,
			ReadOfficerChat = 1 << 2,
			WriteOfficerChat = 1 << 3,
			Promote = 1 << 4,
			Demote = 1 << 5,
			Invite = 1 << 6,
			Remove = 1 << 7,
			SetMotd = 1 << 8,

			All =
				ReadGuildChat |
				WriteGuildChat |
				ReadOfficerChat |
				WriteOfficerChat |
				Promote |
				Demote |
				Invite |
				Remove |
				SetMotd
        };
    }

	struct GuildInfo
	{
		uint64 id;

		String name;
	};

	io::Writer& operator<<(io::Writer& writer, const GuildInfo& itemInfo);
	io::Reader& operator>>(io::Reader& reader, GuildInfo& outItemInfo);
}