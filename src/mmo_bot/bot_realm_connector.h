// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

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
	};
}
