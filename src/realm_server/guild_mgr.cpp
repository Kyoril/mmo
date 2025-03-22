
#include "guild_mgr.h"

#include "base/clock.h"
#include "log/default_log_levels.h"

namespace mmo
{
	GuildMgr::GuildMgr(AsyncDatabase& asyncDatabase)
		: m_asyncDatabase(asyncDatabase)
	{
	}

	void GuildMgr::LoadGuilds()
	{
		ILOG("Loading guilds...");
		const auto startTime = GetAsyncTimeMs();

		auto handler = [this, startTime](std::optional<std::vector<GuildInfo>> guilds)
			{
				if (!guilds)
				{
					ELOG("Failed to load guilds!");
					throw std::runtime_error("Failed to load guilds!");
				}

				for (const auto& guild : *guilds)
				{
					if (!AddGuild(guild))
					{
						ELOG("Failed to add guild " << guild.name);
						throw std::runtime_error("Failed to add guild " + guild.name);
					}
				}

				ILOG("Successfully loaded " << guilds->size() << " guilds in " << (GetAsyncTimeMs() - startTime) << " ms");
				m_guildsLoaded = true;
			};

		m_asyncDatabase.asyncRequest(std::move(handler), & IDatabase::LoadGuilds);
	}

	Guild* GuildMgr::CreateGuild(const String& name, uint64 leaderGuid, const std::vector<uint64>& initialMembers)
	{
		if (GetGuildIdByName(name) != 0)
		{
			WLOG("Guild with name " << name << " already exists!");
			return nullptr;
		}

		// Insert the guild
		auto guild = std::make_unique<Guild>(*this, m_idGenerator.GenerateId(), name, leaderGuid);
		m_guildIdsByName[name] = guild->GetId();
		m_guildsById[guild->GetId()] = std::move(guild);



		return nullptr;
	}

	bool GuildMgr::AddGuild(const GuildInfo& info)
	{
		if (m_guildsById.find(info.id) != m_guildsById.end())
		{
			WLOG("Guild with id " << info.id << " already exists!");
			return false;
		}

		if (m_guildIdsByName.find(info.name) != m_guildIdsByName.end())
		{
			WLOG("Guild with name " << info.name << " already exists!");
			return false;
		}

		// Notify the id generator about the new guild id
		m_idGenerator.NotifyId(info.id);

		// Add the guild to the internal list
		m_guildsById[info.id] = std::make_unique<Guild>(*this, info.id, info.name, info.leaderGuid);
		m_guildIdsByName[info.name] = info.id;

		return true;
	}

	uint64 GuildMgr::GetGuildIdByName(const String& name) const
	{
		const auto it = m_guildIdsByName.find(name);
		if (it == m_guildIdsByName.end())
		{
			return 0;
		}

		return it->second;
	}
}
