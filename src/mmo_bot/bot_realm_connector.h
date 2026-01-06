// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_object_manager.h"
#include "mmo_client/realm_data.h"

#include "game_protocol/game_connector.h"
#include "base/big_number.h"
#include "base/signal.h"
#include "game/character_view.h"
#include "game/movement_type.h"
#include "game/movement_info.h"
#include "game/chat_type.h"

#include "asio/io_service.hpp"

namespace mmo
{
	/// Represents a party member for the bot.
	struct BotPartyMember
	{
		uint64 guid { 0 };
		std::string name;
		uint8 group { 0 };
		bool assistant { false };
		uint32 status { 0 };
	};

	/// Minimal realm connector variant that tolerates unknown packets and exposes the hooks needed for the bot.
	class BotRealmConnector final
		: public game::Connector
		, public game::IConnectorListener
	{
	public:
		signal<void(uint8)> AuthenticationResult;
		signal<void()> CharListUpdated;
		signal<void()> Disconnected;
		signal<void(game::player_login_response::Type)> EnterWorldFailed;
		signal<void(uint32, Vector3, float)> VerifyNewWorld;
		signal<void(game::CharCreateResult)> CharacterCreated;
		signal<void(const std::string&)> PartyInvitationReceived;
		/// @brief Emitted when the bot joins a party or receives an updated party list.
		/// @param leaderGuid The GUID of the party leader.
		/// @param memberCount The number of members in the party.
		signal<void(uint64, uint32)> PartyJoined;
		/// @brief Emitted when the bot leaves or is removed from a party.
		signal<void()> PartyLeft;

		/// @brief Emitted when a new unit is spawned in the world.
		signal<void(const BotUnit&)> UnitSpawned;

		/// @brief Emitted when a unit is despawned from the world.
		signal<void(uint64)> UnitDespawned;

		/// @brief Emitted when a unit's data is updated.
		signal<void(const BotUnit&)> UnitUpdated;

	private:
		asio::io_service& m_ioService;
		std::string m_realmAddress;
		uint16 m_realmPort { 0 };
		std::string m_realmName;
		std::string m_account;
		BigNumber m_sessionKey;
		uint32 m_serverSeed { 0 };
		uint32 m_clientSeed { 0 };
		uint32 m_realmId { 0 };

		uint64 m_selectedCharacterGuid { 0 };
		MovementInfo m_movementInfo;

		// Party state
		std::vector<BotPartyMember> m_partyMembers;
		uint64 m_partyLeaderGuid { 0 };
		bool m_inParty { false };

		// Object management
		BotObjectManager m_objectManager;

	public:
		/// A list of character views.
		std::vector<CharacterView> m_characterViews;

	public:
		explicit BotRealmConnector(asio::io_service& io);

		/// Accept unhandled packets without disconnecting.
		PacketParseResult HandleIncomingPacket(game::IncomingPacket& packet) override;

	public:
		// ~ Begin IConnectorListener
		bool connectionEstablished(bool success) override;
		void connectionLost() override;
		void connectionMalformedPacket() override;
		PacketParseResult connectionPacketReceived(game::IncomingPacket& packet) override;
		// ~ End IConnectorListener

		void SetLoginData(const std::string& accountName, const BigNumber& sessionKey);

		void ConnectToRealm(const RealmData& data);

		void Connect(const std::string& realmAddress, uint16 realmPort, const std::string& accountName, const std::string& realmName, BigNumber sessionKey);

		void EnterWorld(const CharacterView& character);

		void CreateCharacter(const std::string& name, uint8 race, uint8 characterClass, uint8 characterGender, const AvatarConfiguration& customization);

		void RequestCharEnum();

		void SendChatMessage(const String& message, ChatType chatType, const String& target);

		void SendMovementUpdate(uint64 characterId, uint16 opCode, const MovementInfo& info);

		void SendTimeSyncResponse(uint32 syncIndex, GameTime clientTimestamp);

		void SendMoveWorldPortAck();

		void SendMovementSpeedAck(MovementType type, uint32 ackId, float speed, const MovementInfo& movementInfo);

		void SendMoveTeleportAck(uint32 ackId, const MovementInfo& movementInfo);

		const std::vector<CharacterView>& GetCharacterViews() const { return m_characterViews; }

		const MovementInfo& GetMovementInfo() const { return m_movementInfo; }

		uint64 GetSelectedGuid() const { return m_selectedCharacterGuid; }

		void AcceptPartyInvitation();

		void DeclinePartyInvitation();

		// ============================================================
		// Party Information Methods
		// ============================================================

		/// Checks if the bot is currently in a party.
		bool IsInParty() const { return m_inParty; }

		/// Gets the number of members in the party (including the bot).
		uint32 GetPartyMemberCount() const { return m_inParty ? static_cast<uint32>(m_partyMembers.size()) : 0; }

		/// Gets the GUID of the party leader.
		uint64 GetPartyLeaderGuid() const { return m_partyLeaderGuid; }

		/// Checks if the bot is the party leader.
		bool IsPartyLeader() const { return m_inParty && m_partyLeaderGuid == m_selectedCharacterGuid; }

		/// Gets a party member by index.
		const BotPartyMember* GetPartyMember(uint32 index) const;

		/// Gets all party member GUIDs.
		std::vector<uint64> GetPartyMemberGuids() const;

		// ============================================================
		// Party Action Methods
		// ============================================================

		/// Leaves the current party.
		void LeaveParty();

		/// Kicks a player from the party by name.
		void KickFromParty(const std::string& playerName);

		/// Invites a player to the party by name.
		void InviteToParty(const std::string& playerName);

		// ============================================================
		// Object Management Methods
		// ============================================================

		/// Gets the object manager containing all known units.
		BotObjectManager& GetObjectManager() { return m_objectManager; }

		/// Gets the object manager containing all known units (const).
		const BotObjectManager& GetObjectManager() const { return m_objectManager; }

	private:
		PacketParseResult OnAuthChallenge(game::IncomingPacket& packet);

		PacketParseResult OnAuthSessionResponse(game::IncomingPacket& packet);

		PacketParseResult OnCharEnum(game::IncomingPacket& packet);

		PacketParseResult OnLoginVerifyWorld(game::IncomingPacket& packet);

		PacketParseResult OnEnterWorldFailed(game::IncomingPacket& packet);

		PacketParseResult OnTimeSyncRequest(game::IncomingPacket& packet);

		PacketParseResult OnTransferPending(game::IncomingPacket& packet);

		PacketParseResult OnNewWorld(game::IncomingPacket& packet);

		PacketParseResult OnCharCreateResponse(game::IncomingPacket& packet);

		PacketParseResult OnMoveTeleport(game::IncomingPacket& packet);

		PacketParseResult OnForceMovementSpeedChange(game::IncomingPacket& packet);

		PacketParseResult OnIgnoredPacket(game::IncomingPacket& packet);

		PacketParseResult OnGroupInvite(game::IncomingPacket& packet);

		PacketParseResult OnGroupList(game::IncomingPacket& packet);

		PacketParseResult OnGroupDestroyed(game::IncomingPacket& packet);

		PacketParseResult OnGroupSetLeader(game::IncomingPacket& packet);

		PacketParseResult OnUpdateObject(game::IncomingPacket& packet);

		PacketParseResult OnDestroyObjects(game::IncomingPacket& packet);

		PacketParseResult OnNameQueryResult(game::IncomingPacket& packet);

		/// @brief Handles movement packets from other units.
		PacketParseResult OnMovementPacket(game::IncomingPacket& packet);

		/// @brief Parses a single object update from the packet stream.
		/// @param packet The packet to read from.
		/// @param creation Whether this is a creation (new object) or update.
		/// @param typeId The type ID of the object.
		/// @return True if parsing succeeded.
		bool ParseObjectUpdate(io::Reader& reader, bool creation, ObjectTypeId typeId);
	};
}
