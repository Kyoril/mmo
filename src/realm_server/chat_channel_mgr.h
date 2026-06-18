// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "database.h"

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "game/chat_channel_flags.h"

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

namespace mmo
{
	namespace proto
	{
		class Project;
	}

	class PlayerManager;
	class TimerQueue;

	/// Represents a single global chat channel on the realm. A channel definition originates
	/// from the proto game data; this runtime object additionally tracks the set of currently
	/// online members so that messages can be delivered to them.
	class ChatChannel final : public NonCopyable
	{
	public:
		ChatChannel(uint32 id, String name, uint32 flags)
			: m_id(id)
			, m_name(std::move(name))
			, m_flags(flags)
		{
		}

		/// Gets the global channel id (matches the proto ChatChannelEntry id).
		uint32 GetId() const { return m_id; }

		/// Gets the channel's name (also used as the client localization key).
		const String& GetName() const { return m_name; }

		/// Gets the channel's flag bitfield (see chat_channel_flags).
		uint32 GetFlags() const { return m_flags; }

		/// Whether newly created / existing characters join this channel by default.
		bool IsJoinByDefault() const { return (m_flags & chat_channel_flags::JoinByDefault) != 0; }

		/// Updates the cached definition (used when the channel name/flags change in game data).
		void UpdateDefinition(String name, uint32 flags) { m_name = std::move(name); m_flags = flags; }

		/// Registers an online member.
		void AddMember(uint64 characterId)
		{
			std::scoped_lock lock{ m_membersMutex };
			m_onlineMembers.insert(characterId);
		}

		/// Removes an online member.
		void RemoveMember(uint64 characterId)
		{
			std::scoped_lock lock{ m_membersMutex };
			m_onlineMembers.erase(characterId);
		}

		/// Whether the given character is currently an online member of this channel.
		bool IsMember(uint64 characterId) const
		{
			std::scoped_lock lock{ m_membersMutex };
			return m_onlineMembers.find(characterId) != m_onlineMembers.end();
		}

		/// Gets a snapshot copy of the currently online member character guids. Returning a copy
		/// (rather than a reference) keeps delivery safe while joins/leaves happen on other threads.
		std::set<uint64> GetOnlineMembers() const
		{
			std::scoped_lock lock{ m_membersMutex };
			return m_onlineMembers;
		}

	private:
		uint32 m_id;
		String m_name;
		uint32 m_flags;
		mutable std::mutex m_membersMutex;
		std::set<uint64> m_onlineMembers;
	};

	/// Manages all well-known chat channels on the realm. Channel definitions are taken from the
	/// proto game data at startup; per-character membership is persisted as a delta from each
	/// channel's default behaviour (see CharacterChannelState). Outgoing channel messages are
	/// buffered and delivered in batches by a periodic flush, so a single message reaching many
	/// recipients never blocks the realm's packet handling.
	class ChannelMgr final : public NonCopyable
	{
	public:
		ChannelMgr(const proto::Project& project, AsyncChatChannelDatabase& database, PlayerManager& playerManager, TimerQueue& timerQueue);

		/// Builds the in-memory channel objects from proto data and starts the flush timer.
		void Initialize();

		/// Gets a channel by its global id, or nullptr if it doesn't exist.
		ChatChannel* GetChannelById(uint32 id);

		/// Gets a channel by name (case-insensitive), or nullptr if it doesn't exist.
		ChatChannel* GetChannelByName(const String& name);

		/// Gets all channels keyed by id.
		const std::map<uint32, std::unique_ptr<ChatChannel>>& GetChannels() const { return m_channels; }

		/// Computes the effective set of channel ids the given character's persisted membership
		/// deltas resolve to against the currently loaded channel definitions. The underlying
		/// membership rules live in ComputeEffectiveChannelMemberships (chat_channel_membership.h)
		/// so they can be unit-tested in isolation.
		std::vector<uint32> GetEffectiveChannels(const std::vector<CharacterChannelState>& states) const;

		/// Registers an online character as a member of a channel (no persistence).
		void RegisterMember(uint32 channelId, uint64 characterId);

		/// Removes an online character from a channel's member set (no persistence).
		void UnregisterMember(uint32 channelId, uint64 characterId);

		/// Enqueues a chat message for batched delivery to all online members of the channel.
		void QueueMessage(uint32 channelId, uint64 senderGuid, const String& message);

	private:
		void ScheduleFlush();
		void Flush();

	private:
		const proto::Project& m_project;
		AsyncChatChannelDatabase& m_database;
		PlayerManager& m_playerManager;
		TimerQueue& m_timerQueue;

		std::map<uint32, std::unique_ptr<ChatChannel>> m_channels;

		struct PendingMessage
		{
			uint32 channelId;
			uint64 senderGuid;
			String message;
		};

		std::mutex m_pendingMutex;
		std::vector<PendingMessage> m_pending;
	};
}
