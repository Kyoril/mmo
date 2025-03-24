
#include "guild_mgr.h"

#include "base/clock.h"
#include "game/guild_info.h"
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

		auto handler = [this, startTime](std::optional<std::vector<GuildData>> guilds)
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

	bool GuildMgr::CreateGuild(const String& name, uint64 leaderGuid, const std::vector<uint64>& initialMembers, std::function<void(Guild*)> callback)
	{
		if (GetGuildIdByName(name) != 0)
		{
			WLOG("Guild with name " << name << " already exists!");
			return false;
		}

		const uint64 guildId = m_idGenerator.GenerateId();
		auto handler = [guildId, name, leaderGuid, this, callback](bool success)
			{
				if (!success)
				{
					callback(nullptr);
					return;
				}

				auto guild = std::make_shared<Guild>(*this, m_asyncDatabase, guildId, name, leaderGuid);
				m_guildIdsByName[name] = guild->GetId();

				m_guildsById[guildId] = guild;
				callback(guild.get());
			};

		std::vector<GuildRank> defaultRanks;
		defaultRanks.emplace_back("Guild Master", guild_rank_permissions::All);
		defaultRanks.emplace_back("Officer", guild_rank_permissions::All);
		defaultRanks.emplace_back("Veteran", guild_rank_permissions::ReadGuildChat | guild_rank_permissions::WriteGuildChat);
		defaultRanks.emplace_back("Member", guild_rank_permissions::ReadGuildChat | guild_rank_permissions::WriteGuildChat);
		defaultRanks.emplace_back("Initiate", guild_rank_permissions::ReadGuildChat | guild_rank_permissions::WriteGuildChat);

		std::vector<GuildMember> members;
		members.emplace_back(leaderGuid, 0);

		m_asyncDatabase.asyncRequest(std::move(handler), &IDatabase::CreateGuild, guildId, name, leaderGuid, defaultRanks, members);

		return true;
	}

	Guild* GuildMgr::GetGuild(uint64 guildId) const
	{
		const auto it = m_guildsById.find(guildId);
		if (it == m_guildsById.end())
		{
			return nullptr;
		}
		return it->second.get();
	}

	bool GuildMgr::AddGuild(const GuildData& info)
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
		auto guild = std::make_shared<Guild>(*this, m_asyncDatabase, info.id, info.name, info.leaderGuid);
		
		// Add ranks and members
		for (const auto& rank : info.ranks)
		{
			guild->GetRanksRef().push_back(rank);
		}
		
		for (const auto& member : info.members)
		{
			guild->GetMembersRef().push_back(member);
		}
		
		m_guildsById[info.id] = std::move(guild);
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

	bool Guild::IsMember(uint64 playerGuid) const
	{
		for (const auto& member : m_members)
		{
			if (member.guid == playerGuid)
			{
				return true;
			}
		}
		return false;
	}

	uint32 Guild::GetMemberRank(uint64 playerGuid) const
	{
		for (const auto& member : m_members)
		{
			if (member.guid == playerGuid)
			{
				return member.rank;
			}
		}
		return UINT32_MAX; // Invalid rank
	}

	bool Guild::HasPermission(uint64 playerGuid, guild_rank_permissions::Type permission) const
	{
		const uint32 rank = GetMemberRank(playerGuid);
		if (rank == UINT32_MAX || rank >= m_ranks.size())
		{
			return false;
		}

		return (m_ranks[rank].permissions & permission) != 0;
	}

	std::vector<uint64> Guild::GetMembersWithPermission(guild_rank_permissions::Type permission) const
	{
		std::vector<uint64> result;
		
		for (const auto& member : m_members)
		{
			if (HasPermission(member.guid, permission))
			{
				result.push_back(member.guid);
			}
		}
		
		return result;
	}

	void Guild::LoadMembers()
	{
		if (m_membersLoaded)
		{
			return;
		}

		// In a real implementation, we would load members from the database here
		// For now, we'll just mark them as loaded since they're already loaded in AddGuild
		m_membersLoaded = true;
	}

	bool Guild::AddMember(uint64 playerGuid, uint32 rank)
	{
		// Check if the player is already a member
		if (IsMember(playerGuid))
		{
			return false;
		}

		// Check if the rank is valid
		if (rank >= m_ranks.size())
		{
			return false;
		}

		std::weak_ptr weak = shared_from_this();
		auto handler = [weak, playerGuid, rank](bool success)
			{
				auto strong = weak.lock();
				if (!strong)
				{
					return;
				}

				// Add the member
				strong->m_members.emplace_back(playerGuid, rank);
			};

		// Update the database
		m_database.asyncRequest(std::move(handler), &IDatabase::AddGuildMember, m_id, playerGuid, rank);
		return true;
	}

	bool Guild::RemoveMember(uint64 playerGuid)
	{
		// Find the member
		for (auto it = m_members.begin(); it != m_members.end(); ++it)
		{
			if (it->guid == playerGuid)
			{
				// Remove the member
				m_members.erase(it);

				// TODO: Update the database

				return true;
			}
		}

		return false;
	}

	uint32 Guild::GetLowestRank() const
	{
		return m_ranks.size() - 1;
	}
}
