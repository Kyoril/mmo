// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "chat_channel_mgr.h"

#include "player.h"
#include "player_manager.h"

#include "base/timer_queue.h"
#include "game/chat_type.h"
#include "game/chat_channel_membership.h"
#include "game_protocol/game_protocol.h"
#include "log/default_log_levels.h"
#include "proto_data/project.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace mmo
{
	namespace
	{
		/// Interval between channel message flushes, in milliseconds. Channel delivery is
		/// intentionally not real-time: messages are buffered and delivered in batches so that
		/// a single message reaching thousands of recipients never blocks packet handling.
		constexpr GameTime ChannelFlushInterval = 1500;

		String ToLower(const String& value)
		{
			String result = value;
			std::transform(result.begin(), result.end(), result.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return result;
		}
	}

	ChannelMgr::ChannelMgr(const proto::Project& project, AsyncChatChannelDatabase& database, PlayerManager& playerManager, TimerQueue& timerQueue)
		: m_project(project)
		, m_database(database)
		, m_playerManager(playerManager)
		, m_timerQueue(timerQueue)
	{
	}

	void ChannelMgr::Initialize()
	{
		m_channels.clear();

		const auto& channels = m_project.chatChannels.getTemplates();
		for (int i = 0; i < channels.entry_size(); ++i)
		{
			const auto& entry = channels.entry(i);
			m_channels[entry.id()] = std::make_unique<ChatChannel>(entry.id(), entry.name(), entry.has_flags() ? entry.flags() : 0);
		}

		ILOG("Loaded " << m_channels.size() << " chat channel(s)");

		// Start the periodic flush heartbeat.
		ScheduleFlush();
	}

	ChatChannel* ChannelMgr::GetChannelById(uint32 id)
	{
		const auto it = m_channels.find(id);
		return it != m_channels.end() ? it->second.get() : nullptr;
	}

	ChatChannel* ChannelMgr::GetChannelByName(const String& name)
	{
		const String lowered = ToLower(name);
		for (auto& [id, channel] : m_channels)
		{
			if (ToLower(channel->GetName()) == lowered)
			{
				return channel.get();
			}
		}

		return nullptr;
	}

	std::vector<uint32> ChannelMgr::GetEffectiveChannels(const std::vector<CharacterChannelState>& states) const
	{
		std::vector<std::pair<uint32, bool>> defs;
		defs.reserve(m_channels.size());
		for (const auto& [id, channel] : m_channels)
		{
			defs.emplace_back(id, channel->IsJoinByDefault());
		}

		std::vector<std::pair<uint32, uint8>> statePairs;
		statePairs.reserve(states.size());
		for (const auto& state : states)
		{
			statePairs.emplace_back(state.channelId, state.status);
		}

		return ComputeEffectiveChannelMemberships(defs, statePairs);
	}

	void ChannelMgr::RegisterMember(uint32 channelId, uint64 characterId)
	{
		if (ChatChannel* channel = GetChannelById(channelId))
		{
			channel->AddMember(characterId);
		}
	}

	void ChannelMgr::UnregisterMember(uint32 channelId, uint64 characterId)
	{
		if (ChatChannel* channel = GetChannelById(channelId))
		{
			channel->RemoveMember(characterId);
		}
	}

	void ChannelMgr::QueueMessage(uint32 channelId, uint64 senderGuid, const String& message)
	{
		std::scoped_lock lock{ m_pendingMutex };
		m_pending.push_back(PendingMessage{ channelId, senderGuid, message });
	}

	void ChannelMgr::ScheduleFlush()
	{
		m_timerQueue.AddEvent([this]() { Flush(); }, m_timerQueue.GetNow() + ChannelFlushInterval);
	}

	void ChannelMgr::Flush()
	{
		std::vector<PendingMessage> pending;
		{
			std::scoped_lock lock{ m_pendingMutex };
			pending.swap(m_pending);
		}

		for (const auto& msg : pending)
		{
			ChatChannel* channel = GetChannelById(msg.channelId);
			if (!channel)
			{
				// Channel was removed from game data after the message was queued - drop silently.
				continue;
			}

			// Snapshot the member set so delivery is unaffected by concurrent joins/leaves.
			const std::set<uint64> members = channel->GetOnlineMembers();
			const uint32 channelId = channel->GetId();

			for (const uint64 memberGuid : members)
			{
				Player* member = m_playerManager.GetPlayerByCharacterGuid(memberGuid);
				if (!member)
				{
					continue;
				}

				member->SendPacket([&msg, channelId](game::OutgoingPacket& outPacket)
				{
					outPacket.Start(game::realm_client_packet::ChatMessage);
					outPacket
						<< io::write_packed_guid(msg.senderGuid)
						<< io::write<uint8>(ChatType::Channel)
						<< io::write_range(msg.message)
						<< io::write<uint8>(0)  // String terminator
						<< io::write<uint8>(0)  // Chat flags
						<< io::write<uint32>(channelId);
					outPacket.Finish();
				});
			}
		}

		// Reschedule the next flush heartbeat.
		ScheduleFlush();
	}
}
