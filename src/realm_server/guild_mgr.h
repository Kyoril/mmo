#pragma once

#include "database.h"
#include "base/non_copyable.h"

namespace mmo
{
	class GuildMgr;
	class GamePlayerS;

	class Guild final : public NonCopyable
	{
	public:
		Guild(GuildMgr& manager, uint64 id, String name, uint64 leaderGuid)
			: m_id(id)
			, m_name(std::move(name))
			, m_leaderGuid(leaderGuid)
		{
		}

	public:
		uint64 GetId() const { return m_id; }

		const String& GetName() const { return m_name; }

		uint64 GetLeaderGuid() const { return m_leaderGuid; }

	private:
		uint64 m_id;
		String m_name;
		uint64 m_leaderGuid;
	};

	class GuildMgr final : public NonCopyable
	{
	public:
		explicit GuildMgr(AsyncDatabase& asyncDatabase);

	public:
		void LoadGuilds();

		Guild* CreateGuild(const String& name, uint64 leaderGuid, const std::vector<uint64>& initialMembers);

		bool HasGuild(uint64 guildId) const { return m_guildsById.find(guildId) != m_guildsById.end(); }

		uint64 GetGuildIdByName(const String& name) const;

		bool GuildsLoaded() const { return m_guildsLoaded; }

	private:
		bool AddGuild(const GuildInfo& info);

	private:
		AsyncDatabase& m_asyncDatabase;

		std::map<uint64, std::unique_ptr<Guild>> m_guildsById;

		std::map<String, uint64> m_guildIdsByName;

		IdGenerator<uint64> m_idGenerator { 1 };

		volatile bool m_guildsLoaded = false;
	};
}
