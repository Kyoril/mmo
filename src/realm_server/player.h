// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "player_manager.h"

#include "base/non_copyable.h"
#include "game_protocol/game_protocol.h"
#include "game_protocol/game_connection.h"
#include "base/big_number.h"

#include <memory>
#include <functional>
#include <map>
#include <cassert>
#include <algorithm>
#include <limits>
#include <vector>

#include "login_connector.h"
#include "game/action_button.h"
#include "game_server/character_data.h"
#include "game/character_view.h"
#include "game/group.h"


namespace mmo
{
	class GuildMgr;
}

namespace mmo
{
	namespace proto
	{
		class Project;
	}

	class AsyncDatabase;
	class LoginConnector;
	class WorldManager;
	class World;
	class PlayerGroup;

	/// This class represents a player connction on the login server.
	class Player final
		: public NonCopyable
		, public game::IConnectionListener
		, public std::enable_shared_from_this<Player>
	{
	public:
		typedef game::EncryptedConnection<game::Protocol> Client;
		typedef std::function<PacketParseResult(game::IncomingPacket &)> PacketHandler;

	public:
		explicit Player(
			TimerQueue& timerQueue,
			PlayerManager &manager,
			WorldManager& worldManager,
			LoginConnector &loginConnector,
			AsyncDatabase &database,
			std::shared_ptr<Client> connection,
			std::string address,
			const proto::Project& project,
			IdGenerator<uint64>& groupIdGenerator,
			GuildMgr& guildMgr);

		void Kick();

		/// Gets the player connection class used to send packets to the client.
		Client &GetConnection() { assert(m_connection); return *m_connection; }

		/// Gets the player manager which manages all connected players.
		PlayerManager &GetManager() const { return m_manager; }

		/// Gets the world manager which manages all connected world nodes.
		WorldManager& GetWorldManager() const { return m_worldManager; }

		[[nodiscard]] bool HasCharacterGuid() const noexcept { return m_characterData.has_value(); }

		[[nodiscard]] uint64 GetCharacterGuid() const noexcept { return m_characterData.has_value() ? m_characterData.value().characterId : 0; }

		/// Determines whether the player is authentificated.
		/// @returns true if the player is authentificated.
		bool IsAuthenticated() const { return !m_sessionKey.isZero(); }

		/// Gets the account name the player is logged in with.
		const std::string &GetAccountName() const { return m_accountName; }

		uint64 GetAccountId() const { return m_accountId; }

		/// Gets the GM level of the player.
		uint8 GetGMLevel() const { return m_gmLevel; }

		/// Checks if the player has a specific GM level or higher.
		bool HasGMLevel(uint8 level) const { return m_gmLevel >= level; }

		[[nodiscard]] const String& GetCharacterName() const { return m_characterData->name; }

		[[nodiscard]] uint32 GetCharacterLevel() const { return m_characterData->level; }

		[[nodiscard]] uint32 GetCharacterRace() const { return m_characterData->raceId; }

		[[nodiscard]] uint32 GetCharacterClass() const { return m_characterData->classId; }

		// Inititalizes a character transfer to a new map.
		bool InitializeTransfer(uint32 map, const Vector3& location, float o, bool shouldLeaveNode = false);

		/// Commits an initialized transfer (if any).
		void CommitTransfer();

		/// 
		const InstanceId& GetWorldInstanceId() const { return m_instanceId; }

		std::shared_ptr<PlayerGroup> GetGroup() const { return m_group; }

		void SetInviterGuid(uint64 inviter);

		void SetGroup(std::shared_ptr<PlayerGroup> group) { m_group = std::move(group); }

		void SendPartyInvite(const String& inviterName);

		/// Declines a pending group invite (if available).
		void DeclineGroupInvite();

		std::shared_ptr<World> GetWorld() const { return m_world.lock(); }

		void BuildPartyMemberStatsPacket(game::OutgoingPacket& packet, uint32 groupUpdateFlags) const;

		void GuildChange(uint64 guildId);

		void NotifyCharacterUpdate(uint32 mapId, InstanceId instanceId, const GamePlayerS& character);

	public:
		/// Send an auth challenge packet to the client in order to ask it for authentication data.
		void SendAuthChallenge();

		/// Initializes the session by providing a session key. The connection to the client will 
		/// be encrypted from here on.
		void InitializeSession(const BigNumber& sessionKey);
		
		void SendProxyPacket(uint16 packetId, const std::vector<char> &buffer);

		/// Sends the Message of the Day to the player
		void SendMessageOfTheDay(const std::string& motd);

		void OnWorldLeft(const std::shared_ptr<World>& world, auth::WorldLeftReason reason);

		void CharacterLocationResponseNotification(bool succeeded, uint64 ackId, uint32 mapId, const Vector3& position, const Radian& facing);

		PacketParseResult OnGroupUpdate(auth::IncomingPacket& packet);

		void NotifyQuestData(uint32 questId, const QuestStatusData& questData);

	private:
		/// Enables or disables handling of EnterWorld packets from the client.
		void EnableEnterWorldPacket(bool enable);

		void EnableProxyPackets(bool enable);

		void JoinWorld() const;

		void OnWorldJoined(const std::shared_ptr<World>& world, const InstanceId instanceId);

		void OnWorldChanged(const std::shared_ptr<World>& world, const InstanceId instanceId);

		void OnWorldJoinFailed(const game::player_login_response::Type response);

		void OnWorldTransferAborted(const game::TransferAbortReason reason);

		void OnCharacterData(std::optional<CharacterData> characterData);

		void OnWorldDestroyed(World& world);

		void NotifyWorldNodeChanged(World* worldNode);

		void OnQueryCreature(uint64 entry);

		void OnQueryQuest(uint64 entry);

		void OnQueryItem(uint64 entry);

		void OnQueryObject(uint64 entry);

		void OnActionButtons(const ActionButtons& actionButtons);

		typedef std::function<void(bool succeeded, uint32 mapId, Vector3 position, Radian facing)> CharacterLocationAsyncCallback;

		void FetchCharacterLocationAsync(CharacterLocationAsyncCallback&& callback);

		void SendTeleportRequest(uint32 mapId, const Vector3& position, const Radian& facing) const;

		void SendPartyOperationResult(PartyOperation operation, PartyResult result, const String& playerName);

		void SendGuildCommandResult(uint8 command, uint8 result, const String& playerName);

		void OnGuildRemoveCharacterIdResolve(DatabaseId characterId, const String& playerName);

	public:
		struct PacketHandlerRegistrationHandle final
		{
		private:
			std::weak_ptr<Player> m_player;
			uint16 m_opCode;

		public:
			// Copy operations are deleted to prevent copying
			PacketHandlerRegistrationHandle(const PacketHandlerRegistrationHandle&) = delete;
			PacketHandlerRegistrationHandle& operator=(const PacketHandlerRegistrationHandle&) = delete;

			PacketHandlerRegistrationHandle(Player& player, const uint16 opCode)
				: m_player(player.shared_from_this()), m_opCode(opCode)
			{
			}

			PacketHandlerRegistrationHandle(PacketHandlerRegistrationHandle&& other) noexcept
				: m_player(std::move(other.m_player)), m_opCode(other.m_opCode)
			{
				other.m_opCode = std::numeric_limits<uint16>::max();
			}

			~PacketHandlerRegistrationHandle()
			{
				const std::shared_ptr<Player> strongPlayer = m_player.lock();
				if (m_opCode != std::numeric_limits<uint16>::max() && strongPlayer)
				{
					strongPlayer->ClearPacketHandler(m_opCode);
				}
			}
		};

		struct PacketHandlerHandleContainer final
		{
		private:
			std::vector<PacketHandlerRegistrationHandle> m_handles;

		public:
			void Add(PacketHandlerRegistrationHandle&& handle)
			{
				m_handles.push_back(std::move(handle));
			}

			void Clear()
			{
				m_handles.clear();
			}

			[[nodiscard]] bool IsEmpty() const
			{
				return m_handles.empty();
			}

		public:
			PacketHandlerHandleContainer& operator+=(PacketHandlerRegistrationHandle&& handle) {
				m_handles.push_back(std::move(handle));
				return *this;
			}
		};

		/// Registers a packet handler.
		void RegisterPacketHandler(uint16 opCode, PacketHandler &&handler);

		/// Syntactic sugar implementation of RegisterPacketHandler to avoid having to use std::bind.
		template <class Instance, class Class, class... Args1>
		void RegisterPacketHandler(uint16 opCode, Instance& object, PacketParseResult(Class::*method)(Args1...))
		{
			RegisterPacketHandler(opCode, [&object, method](Args1... args) {
				return (object.*method)(Args1(args)...);
			});
		}

		/// Syntactic sugar implementation of RegisterPacketHandler to avoid having to use std::bind.
		template <class Instance, class Class, class... Args1>
		[[nodiscard]] PacketHandlerRegistrationHandle RegisterAutoPacketHandler(uint16 opCode, Instance& object, PacketParseResult(Class::* method)(Args1...))
		{
			RegisterPacketHandler(opCode, [&object, method](Args1... args) {
				return (object.*method)(Args1(args)...);
				});

			return { *this, opCode };
		}

		/// Clears a packet handler so that the opcode is no longer handled.
		void ClearPacketHandler(uint16 opCode);

		/// Sends an encrypted packet to the game client
		/// @param generator Packet writer function pointer.
		template<class F>
		void SendPacket(F generator)
		{
			// Write native packet
			Buffer& sendBuffer = m_connection->getSendBuffer();
			io::StringSink sink(sendBuffer);

			// Get the end of the buffer (needed for encryption)
			size_t bufferPos = sink.Position();

			typename game::Protocol::OutgoingPacket packet(sink);
			generator(packet);

			// Crypt packet header
			game::Connection* cryptCon = m_connection.get();
			cryptCon->GetCrypt().EncryptSend(reinterpret_cast<uint8*>(&sendBuffer[bufferPos]), game::Crypt::CryptedSendLength);

			// Flush buffers
			m_connection->flush();
		}

	private:
		TimerQueue& m_timerQueue;
		PlayerManager &m_manager;
		WorldManager &m_worldManager;
		LoginConnector &m_loginConnector;
		AsyncDatabase &m_database;
		const proto::Project& m_project;
		IdGenerator<uint64>& m_groupIdGenerator;
		std::shared_ptr<Client> m_connection;
		std::string m_address;						// IP address in string format
		std::string m_accountName;					// Account name in uppercase letters
		uint32 m_build;								// Build version: 0.0.0.XXXXX
		std::map<uint16, PacketHandler> m_packetHandlers;
		std::mutex m_packetHandlerMutex;
		uint32 m_seed;								// Random generated seed used for packet header encryption
		uint32 m_clientSeed;
		uint64 m_accountId;
		SHA1Hash m_clientHash;
		/// Session key of the game client, retrieved by login server on successful login request.
		BigNumber m_sessionKey;
		uint8 m_gmLevel = 0;     // GM level of the player account (0: normal player, 1+: GM levels)
		ActionButtons m_actionButtons;
		bool m_pendingButtons = false;
		InstanceId m_instanceId{};
		uint32 m_transferMap = 0;
		Vector3 m_transferPosition{};
		float m_transferFacing = 0.0f;
		std::shared_ptr<PlayerGroup> m_group;

		PacketHandlerHandleContainer m_proxyHandlers;

		std::mutex m_charViewMutex;
		std::map<uint64, CharacterView> m_characterViews;

		std::weak_ptr<World> m_world;

		std::optional<CharacterData> m_characterData;
		scoped_connection m_worldDestroyed;
		PacketHandlerHandleContainer m_newWorldAckHandler;

		IdGenerator<uint64> m_callbackIdGenerator;
		std::map<uint64, CharacterLocationAsyncCallback> m_characterLocationCallbacks;
		uint64 m_inviterGuid = 0;

		scoped_connection m_onGroupLoaded;

		GuildMgr& m_guildMgr;

		uint64 m_pendingGuildInvite = 0;

		uint64 m_guildInviter = 0;

	private:
		/// Closes the connection if still connected.
		void Destroy();
		/// Requests the current character list from the database and notifies the client.
		void DoCharEnum();

		void OnGroupLoaded(PlayerGroup& group);
	
	private:
		/// @copydoc mmo::auth::IConnectionListener::connectionLost()
		void connectionLost() override;

		/// @copydoc mmo::auth::IConnectionListener::connectionMalformedPacket()
		void connectionMalformedPacket() override;

		/// @copydoc mmo::auth::IConnectionListener::connectionPacketReceived()
		PacketParseResult connectionPacketReceived(game::IncomingPacket &packet) override;

	private:
		PacketParseResult OnAuthSession(game::IncomingPacket& packet);
		PacketParseResult OnCharEnum(game::IncomingPacket& packet);
		PacketParseResult OnEnterWorld(game::IncomingPacket& packet);
		PacketParseResult OnCreateChar(game::IncomingPacket& packet);
		PacketParseResult OnDeleteChar(game::IncomingPacket& packet);
		PacketParseResult OnProxyPacket(game::IncomingPacket& packet);
		PacketParseResult OnChatMessage(game::IncomingPacket& packet);
		PacketParseResult OnNameQuery(game::IncomingPacket& packet);
		PacketParseResult OnGuildQuery(game::IncomingPacket& packet);
		PacketParseResult OnDbQuery(game::IncomingPacket& packet);
		PacketParseResult OnSetActionBarButton(game::IncomingPacket& packet);
		PacketParseResult OnMoveWorldPortAck(game::IncomingPacket& packet);
		PacketParseResult OnGroupInvite(game::IncomingPacket& packet);
		PacketParseResult OnGroupUninvite(game::IncomingPacket& packet);
		PacketParseResult OnGroupAccept(game::IncomingPacket& packet);
		PacketParseResult OnGroupDecline(game::IncomingPacket& packet);
		PacketParseResult OnLogoutRequest(game::IncomingPacket& packet);

		// Guild packet handlers
		PacketParseResult OnGuildInvite(game::IncomingPacket& packet);
		PacketParseResult OnGuildRemove(game::IncomingPacket& packet);
		PacketParseResult OnGuildPromote(game::IncomingPacket& packet);
		PacketParseResult OnGuildDemote(game::IncomingPacket& packet);
		PacketParseResult OnGuildLeave(game::IncomingPacket& packet);
		PacketParseResult OnGuildDisband(game::IncomingPacket& packet);
		PacketParseResult OnGuildMotd(game::IncomingPacket& packet);
		PacketParseResult OnGuildAccept(game::IncomingPacket& packet);
		PacketParseResult OnGuildDecline(game::IncomingPacket& packet);
		PacketParseResult OnGuildRoster(game::IncomingPacket& packet);

#ifdef MMO_WITH_DEV_COMMANDS
		PacketParseResult OnCheatTeleportToPlayer(game::IncomingPacket& packet);
		PacketParseResult OnCheatSummon(game::IncomingPacket& packet);
		PacketParseResult OnGuildCreate(game::IncomingPacket& packet);
#endif
	};

}
