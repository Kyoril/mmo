
#include "guild_mgr.h"

#include "base/clock.h"
#include "game/guild_info.h"
#include "log/default_log_levels.h"

namespace mmo
{
	GuildMgr::GuildMgr(AsyncDatabase& asyncDatabase, PlayerManager& playerManager)
		: m_asyncDatabase(asyncDatabase)
		, m_playerManager(playerManager)
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

		Player* player = m_playerManager.GetPlayerByCharacterGuid(leaderGuid);
		if (!player)
		{
			ELOG("Leader is not online, guild can not be created");
			return false;
		}

		std::vector<GuildRank> defaultRanks;
		defaultRanks.emplace_back("Guild Master", guild_rank_permissions::All);
		defaultRanks.emplace_back("Officer", guild_rank_permissions::All);
		defaultRanks.emplace_back("Veteran", guild_rank_permissions::ReadGuildChat | guild_rank_permissions::WriteGuildChat);
		defaultRanks.emplace_back("Member", guild_rank_permissions::ReadGuildChat | guild_rank_permissions::WriteGuildChat);
		defaultRanks.emplace_back("Initiate", guild_rank_permissions::ReadGuildChat | guild_rank_permissions::WriteGuildChat);

		const String characterName = player->GetCharacterName();
		const uint32 level = player->GetCharacterLevel();
		const uint32 classId = player->GetCharacterClass();
		const uint32 raceId = player->GetCharacterRace();

		const uint64 guildId = m_idGenerator.GenerateId();
		auto handler = [guildId, name, leaderGuid, this, callback, defaultRanks, characterName, level, classId, raceId](bool success)
			{
				if (!success)
				{
					callback(nullptr);
					return;
				}

				const auto guild = std::make_shared<Guild>(*this, m_playerManager, m_asyncDatabase, guildId, name, leaderGuid);
				for (const auto& rank : defaultRanks)
				{
					guild->GetRanksRef().push_back(rank);
				}

				guild->GetMembersRef().emplace_back(leaderGuid, 0, characterName, level, raceId, classId);

				m_guildIdsByName[name] = guild->GetId();
				m_guildsById[guildId] = guild;
				callback(guild.get());
			};

		std::vector<GuildMember> members;
		members.emplace_back(leaderGuid, 0, name, level, raceId, classId);

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

	bool GuildMgr::DisbandGuild(uint64 guildId)
	{
		const auto it = m_guildsById.find(guildId);
		if (it == m_guildsById.end())
		{
			return false;
		}

		auto handler = [this, guildId](bool success)
			{
				ASSERT(success);

				const auto it = m_guildsById.find(guildId);
				ASSERT(it != m_guildsById.end());

				// Notify members about disbanding
				it->second->BroadcastEvent(guild_event::Disbanded);

				for (const auto& member : it->second->GetMembers())
				{
					if (auto player = m_playerManager.GetPlayerByCharacterGuid(member.guid))
					{
						player->GuildChange(0);
					}
				}

				// Delete guild
				m_guildIdsByName.erase(it->second->GetName());
				m_guildsById.erase(it);
			};

		m_asyncDatabase.asyncRequest(std::move(handler), &IDatabase::DisbandGuild, guildId);
		return true;
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
		auto guild = std::make_shared<Guild>(*this, m_playerManager, m_asyncDatabase, info.id, info.name, info.leaderGuid);
		
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

	bool Guild::HasPermission(uint64 playerGuid, uint32 permission) const
	{
		const uint32 rank = GetMemberRank(playerGuid);
		if (rank == UINT32_MAX || rank >= m_ranks.size())
		{
			return false;
		}

		return (m_ranks[rank].permissions & permission) != 0;
	}

	std::vector<uint64> Guild::GetMembersWithPermission(uint32 permission) const
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

		// Check if player is online right now
		Player* player = m_playerManager.GetPlayerByCharacterGuid(playerGuid);
		if (!player)
		{
			ELOG("Failed to add member " << playerGuid << " to guild " << m_id << ": player is not online");
			return false;
		}

		const String name = player->GetCharacterName();
		const uint32 level = player->GetCharacterLevel();
		const uint32 raceId = player->GetCharacterRace();
		const uint32 classId = player->GetCharacterClass();

		std::weak_ptr weak = shared_from_this();
		auto handler = [weak, playerGuid, rank, name, level, raceId, classId](bool success)
			{
				const auto strong = weak.lock();
				if (!strong)
				{
					return;
				}

				ASSERT(success);

				// Add the member
				strong->m_members.emplace_back(playerGuid, rank, name, level, raceId, classId);
			};

		// Update the database
		m_database.asyncRequest(std::move(handler), &IDatabase::AddGuildMember, m_id, playerGuid, rank);
		return true;
	}

	bool Guild::RemoveMember(uint64 playerGuid)
	{
		// Find the member
		const auto it = std::find_if(m_members.begin(), m_members.end(), [playerGuid](const GuildMember& member)
			{
				return member.guid == playerGuid;
			});

		if (it == m_members.end())
		{
			return false;
		}

		std::weak_ptr weak = shared_from_this();
		auto handler = [weak, playerGuid](const bool success)
			{
				const auto strong = weak.lock();
				if (!strong)
				{
					return;
				}

				ASSERT(success);

				const auto it = std::find_if(strong->m_members.begin(), strong->m_members.end(), [playerGuid](const GuildMember& member)
					{
						return member.guid == playerGuid;
					});

				if (it != strong->m_members.end())
				{
					strong->m_members.erase(it);
				}
			};

		// Update the database
		m_database.asyncRequest(std::move(handler), &IDatabase::RemoveGuildMember, m_id, playerGuid);

		return true;
	}

	bool Guild::PromoteMember(uint64 playerGuid, const String& promoterName, const String& promotedName)
	{
		const auto it = std::find_if(m_members.begin(), m_members.end(), [playerGuid](const GuildMember& member)
			{
				return member.guid == playerGuid;
			});

		if (it == m_members.end())
		{
			return false;
		}

		const uint32 newRankId = it->rank - 1;

		std::weak_ptr weak = shared_from_this();
		auto handler = [weak, playerGuid, newRankId, promoterName, promotedName](const bool success)
			{
				const auto strong = weak.lock();
				if (!strong)
				{
					return;
				}

				ASSERT(success);
				const auto it = std::find_if(strong->m_members.begin(), strong->m_members.end(), [playerGuid](const GuildMember& member)
					{
						return member.guid == playerGuid;
					});

				const GuildRank* newRank = strong->GetRank(newRankId);
				ASSERT(newRank);

				if (it != strong->m_members.end())
				{
					it->rank = newRankId;
					strong->BroadcastEvent(guild_event::Promotion, 0, promoterName.c_str(), promotedName.c_str(), newRank->name.c_str());
				}
			};

		// Update the database
		m_database.asyncRequest(std::move(handler), &IDatabase::SetGuildMemberRank, m_id, it->guid, newRankId);

		return true;
	}

	bool Guild::DemoteMember(uint64 playerGuid, const String& demoterName, const String& demotedName)
	{
		const auto it = std::find_if(m_members.begin(), m_members.end(), [playerGuid](const GuildMember& member)
			{
				return member.guid == playerGuid;
			});

		if (it == m_members.end())
		{
			return false;
		}

		const uint32 newRankId = it->rank + 1;

		std::weak_ptr weak = shared_from_this();
		auto handler = [weak, playerGuid, newRankId, demoterName, demotedName](const bool success)
			{
				const auto strong = weak.lock();
				if (!strong)
				{
					return;
				}

				ASSERT(success);
				const auto it = std::find_if(strong->m_members.begin(), strong->m_members.end(), [playerGuid](const GuildMember& member)
					{
						return member.guid == playerGuid;
					});

				const GuildRank* newRank = strong->GetRank(newRankId);
				ASSERT(newRank);

				if (it != strong->m_members.end())
				{
					it->rank = newRankId;
					strong->BroadcastEvent(guild_event::Demotion, 0, demoterName.c_str(), demotedName.c_str(), newRank->name.c_str());
				}
			};

		// Update the database
		m_database.asyncRequest(std::move(handler), &IDatabase::SetGuildMemberRank, m_id, it->guid, newRankId);

		return true;
	}

	void Guild::WriteRoster(io::Writer& writer)
	{
		writer
			<< io::write<uint32>(m_members.size())
			<< io::write<uint32>(m_ranks.size());

		for (const auto& rank : m_ranks)
		{
			writer << io::write<uint32>(rank.permissions);
		}

		for (auto& member : m_members)
		{
			Player* player = m_playerManager.GetPlayerByCharacterGuid(member.guid);
			if (player)
			{
				// Ensure data is up to date
				member.level = player->GetCharacterLevel();
			}

			writer
				<< io::write<uint64>(member.guid)
				<< io::write<uint8>(player != nullptr)
				<< io::write_dynamic_range<uint8>(member.name)
				<< io::write<uint32>(member.rank)
				<< io::write<uint32>(member.level)
				<< io::write<uint32>(member.classId)
				<< io::write<uint32>(member.raceId);
		}
	}

	uint32 Guild::GetLowestRank() const
	{
		return m_ranks.size() - 1;
	}

	const GuildRank* Guild::GetRank(uint32 rank) const
	{
		if (rank >= m_ranks.size())
		{
			return nullptr;
		}
		return &m_ranks[rank];
	}

	void Guild::BroadcastEvent(GuildEvent event, uint64 exceptGuid, const char* arg1, const char* arg2, const char* arg3)
	{
		BroadcastPacketWithPermission([event, arg1, arg2, arg3](game::OutgoingPacket& packet)
			{
				packet.Start(game::realm_client_packet::GuildEvent);
				packet << io::write<uint8>(event);

				uint8 stringCount = 0;
				if (arg1) stringCount++;
				if (arg2) stringCount++;
				if (arg3) stringCount++;
				packet << io::write<uint8>(stringCount);

				if (arg1)
				{
					packet << io::write_dynamic_range<uint8>(String(arg1));
				}
				if (arg2)
				{
					packet << io::write_dynamic_range<uint8>(String(arg2));
				}
				if (arg3)
				{
					packet << io::write_dynamic_range<uint8>(String(arg3));
				}

				packet.Finish();
			}, 0, exceptGuid);
	}
}
