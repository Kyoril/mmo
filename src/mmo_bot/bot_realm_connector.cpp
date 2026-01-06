// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_realm_connector.h"

#include "version.h"
#include "game/movement_info.h"

#include "base/clock.h"
#include "base/constants.h"
#include "base/random.h"
#include "game/spell_target_map.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <iomanip>
#include <mutex>

namespace mmo
{
	namespace
	{
		MovementType MovementTypeFromForcePacket(const uint16 opCode)
		{
			switch (opCode)
			{
			case game::realm_client_packet::ForceMoveSetWalkSpeed:
				return movement_type::Walk;
			case game::realm_client_packet::ForceMoveSetRunSpeed:
				return movement_type::Run;
			case game::realm_client_packet::ForceMoveSetRunBackSpeed:
				return movement_type::Backwards;
			case game::realm_client_packet::ForceMoveSetSwimSpeed:
				return movement_type::Swim;
			case game::realm_client_packet::ForceMoveSetSwimBackSpeed:
				return movement_type::SwimBackwards;
			case game::realm_client_packet::ForceMoveSetTurnRate:
				return movement_type::Turn;
			case game::realm_client_packet::ForceSetFlightSpeed:
				return movement_type::Flight;
			case game::realm_client_packet::ForceSetFlightBackSpeed:
				return movement_type::FlightBackwards;
			default:
				return movement_type::Run;
			}
		}
	}

	BotRealmConnector::BotRealmConnector(asio::io_service& io)
		: game::Connector(std::make_unique<asio::ip::tcp::socket>(io), nullptr)
		, m_ioService(io)
	{
	}

	PacketParseResult BotRealmConnector::HandleIncomingPacket(game::IncomingPacket& packet)
	{
		game::EncryptedConnector<game::Protocol>::PacketHandler handler = nullptr;
		{
			std::scoped_lock lock{ m_packetHandlerMutex };

			const auto it = m_packetHandlers.find(packet.GetId());
			if (it != m_packetHandlers.end())
			{
				handler = it->second;
			}
		}

		if (!handler)
		{
			WLOG("[Realm] Unhandled packet 0x" << std::hex << std::setw(2) << std::setfill('0') << packet.GetId());
			return PacketParseResult::Pass;
		}

		return handler(packet);
	}

	bool BotRealmConnector::connectionEstablished(bool success)
	{
		if (success)
		{
			// Reset server seed
			m_serverSeed = 0;

			// Generate a new client seed
			std::uniform_int_distribution<uint32> dist;
			m_clientSeed = dist(RandomGenerator);

			// Accept LogonChallenge packets from here on
			RegisterPacketHandler(game::realm_client_packet::AuthChallenge, *this, &BotRealmConnector::OnAuthChallenge);
		}
		else
		{
			ELOG("Could not connect to the realm server");
		}

		return true;
	}

	void BotRealmConnector::connectionLost()
	{
		ELOG("Lost connection to the realm server...");
		ClearPacketHandlers();
		Disconnected();
	}

	void BotRealmConnector::connectionMalformedPacket()
	{
		ELOG("Received a malformed packet");
	}

	PacketParseResult BotRealmConnector::connectionPacketReceived(game::IncomingPacket& packet)
	{
		return HandleIncomingPacket(packet);
	}

	void BotRealmConnector::SetLoginData(const std::string& accountName, const BigNumber& sessionKey)
	{
		m_account = accountName;
		m_sessionKey = sessionKey;
	}

	void BotRealmConnector::ConnectToRealm(const RealmData& data)
	{
		m_realmId = data.id;
		m_realmAddress = data.address;
		m_realmPort = data.port;
		m_realmName = data.name;

		connect(m_realmAddress, m_realmPort, *this, m_ioService);
	}

	void BotRealmConnector::Connect(const std::string& realmAddress, uint16 realmPort, const std::string& accountName, const std::string& realmName, BigNumber sessionKey)
	{
		m_realmAddress = realmAddress;
		m_realmPort = realmPort;
		m_realmName = realmName;
		m_account = accountName;
		m_sessionKey = sessionKey;

		connect(m_realmAddress, m_realmPort, *this, m_ioService);
	}

	void BotRealmConnector::EnterWorld(const CharacterView& character)
	{
		const uint64 guid = character.GetGuid();
		m_selectedCharacterGuid = guid;

		sendSinglePacket([guid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::EnterWorld);
			packet << io::write<uint64>(guid);
			packet.Finish();
			});
	}

	void BotRealmConnector::CreateCharacter(const std::string& name, uint8 race, uint8 characterClass, uint8 characterGender, const AvatarConfiguration& customization)
	{
		sendSinglePacket([&name, race, characterClass, characterGender, &customization](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CreateChar);
			packet
				<< io::write_dynamic_range<uint8>(name)
				<< io::write<uint8>(race)
				<< io::write<uint8>(characterClass)
				<< io::write<uint8>(characterGender)
				<< customization;
			packet.Finish();
			});
	}

	void BotRealmConnector::RequestCharEnum()
	{
		sendSinglePacket([](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::CharEnum);
				packet.Finish();
			});
	}

	void BotRealmConnector::SendChatMessage(const String& message, ChatType chatType, const String& target)
	{
		sendSinglePacket([&message, chatType, &target](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::ChatMessage);
				packet
					<< io::write<uint8>(chatType)
					<< io::write_range(message) << io::write<uint8>(0);
				if (chatType == ChatType::Whisper || chatType == ChatType::Channel)
				{
					packet << io::write_dynamic_range<uint8>(target);
				}
				packet.Finish();
			});
	}

	void BotRealmConnector::SendMovementUpdate(uint64 characterId, uint16 opCode, const MovementInfo& info)
	{
		sendSinglePacket([characterId, opCode, &info](game::OutgoingPacket& packet) {
			packet.Start(opCode);
			packet << io::write<uint64>(characterId);
			packet << info;
			packet.Finish();
			});
	}

	void BotRealmConnector::SendTimeSyncResponse(uint32 syncIndex, GameTime clientTimestamp)
	{
		sendSinglePacket([syncIndex, clientTimestamp](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::TimeSyncResponse);
				packet << io::write<uint32>(syncIndex) << io::write<uint64>(clientTimestamp);
				packet.Finish();
			});
	}

	void BotRealmConnector::SendMoveWorldPortAck()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::MoveWorldPortAck);
			packet.Finish();
			});
	}

	void BotRealmConnector::SendMovementSpeedAck(MovementType type, uint32 ackId, float speed, const MovementInfo& movementInfo)
	{
		sendSinglePacket([type, ackId, speed, &movementInfo](game::OutgoingPacket& packet)
			{
				static const uint16 moveOpCodes[MovementType::Count] = {
					game::client_realm_packet::ForceMoveSetWalkSpeedAck,
					game::client_realm_packet::ForceMoveSetRunSpeedAck,
					game::client_realm_packet::ForceMoveSetRunBackSpeedAck,
					game::client_realm_packet::ForceMoveSetSwimSpeedAck,
					game::client_realm_packet::ForceMoveSetSwimBackSpeedAck,
					game::client_realm_packet::ForceMoveSetTurnRateAck,
					game::client_realm_packet::ForceSetFlightSpeedAck,
					game::client_realm_packet::ForceSetFlightBackSpeedAck
				};

				packet.Start(moveOpCodes[type]);
				packet << io::write<uint32>(ackId) << movementInfo << io::write<float>(speed);
				packet.Finish();
			});
	}

	void BotRealmConnector::SendMoveTeleportAck(uint32 ackId, const MovementInfo& movementInfo)
	{
		sendSinglePacket([ackId, &movementInfo](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::MoveTeleportAck);
				packet << io::write<uint32>(ackId) << movementInfo;
				packet.Finish();
			});
	}

	PacketParseResult BotRealmConnector::OnAuthChallenge(game::IncomingPacket& packet)
	{
		// No longer handle LogonChallenge packets during this session
		ClearPacketHandler(game::realm_client_packet::AuthChallenge);

		// Try to read the packet data
		if (!(packet >> io::read<uint32>(m_serverSeed)))
		{
			return PacketParseResult::Disconnect;
		}

		// Calculate a hash for verification
		HashGeneratorSha1 hashGen;
		hashGen.update(m_account.data(), m_account.length());
		hashGen.update(reinterpret_cast<const char*>(&m_clientSeed), sizeof(m_clientSeed));
		hashGen.update(reinterpret_cast<const char*>(&m_serverSeed), sizeof(m_serverSeed));
		Sha1_Add_BigNumbers(hashGen, { m_sessionKey });
		SHA1Hash hash = hashGen.finalize();

		// Listen for response packet
		RegisterPacketHandler(game::realm_client_packet::AuthSessionResponse, *this, &BotRealmConnector::OnAuthSessionResponse);

		// We have been challenged, respond with an answer
		sendSinglePacket([this, &hash](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::AuthSession);
			packet
				<< io::write<uint32>(mmo::Revision)
				<< io::write_dynamic_range<uint8>(this->m_account)
				<< io::write<uint32>(m_clientSeed)
				<< io::write_range(hash);
			packet.Finish();
			});

		// Initialize connection encryption afterward
		HMACHash cryptKey;
		game::Crypt::GenerateKey(cryptKey, m_sessionKey);

		game::Crypt& crypt = GetCrypt();
		crypt.SetKey(cryptKey.data(), cryptKey.size());
		crypt.Init();

		ILOG("[Realm] Handshaking...");
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnAuthSessionResponse(game::IncomingPacket& packet)
	{
		// No longer accept these packets from here on!
		ClearPacketHandler(game::realm_client_packet::AuthSessionResponse);

		// Try to read packet data
		uint8 result = 0;
		if (!(packet >> io::read<uint8>(result)))
		{
			return PacketParseResult::Disconnect;
		}

		AuthenticationResult(result);

		if (result == game::auth_result::Success)
		{
			RegisterPacketHandler(game::realm_client_packet::CharEnum, *this, &BotRealmConnector::OnCharEnum);
			RegisterPacketHandler(game::realm_client_packet::LoginVerifyWorld, *this, &BotRealmConnector::OnLoginVerifyWorld);
			RegisterPacketHandler(game::realm_client_packet::EnterWorldFailed, *this, &BotRealmConnector::OnEnterWorldFailed);
			RegisterPacketHandler(game::realm_client_packet::TimeSyncRequest, *this, &BotRealmConnector::OnTimeSyncRequest);
			RegisterPacketHandler(game::realm_client_packet::TransferPending, *this, &BotRealmConnector::OnTransferPending);
			RegisterPacketHandler(game::realm_client_packet::NewWorld, *this, &BotRealmConnector::OnNewWorld);
			RegisterPacketHandler(game::realm_client_packet::CharCreateResponse, *this, &BotRealmConnector::OnCharCreateResponse);
			RegisterPacketHandler(game::realm_client_packet::MoveTeleportAck, *this, &BotRealmConnector::OnMoveTeleport);
			RegisterPacketHandler(game::realm_client_packet::ForceMoveSetWalkSpeed, *this, &BotRealmConnector::OnForceMovementSpeedChange);
			RegisterPacketHandler(game::realm_client_packet::ForceMoveSetRunSpeed, *this, &BotRealmConnector::OnForceMovementSpeedChange);
			RegisterPacketHandler(game::realm_client_packet::ForceMoveSetRunBackSpeed, *this, &BotRealmConnector::OnForceMovementSpeedChange);
			RegisterPacketHandler(game::realm_client_packet::ForceMoveSetSwimSpeed, *this, &BotRealmConnector::OnForceMovementSpeedChange);
			RegisterPacketHandler(game::realm_client_packet::ForceMoveSetSwimBackSpeed, *this, &BotRealmConnector::OnForceMovementSpeedChange);
			RegisterPacketHandler(game::realm_client_packet::ForceMoveSetTurnRate, *this, &BotRealmConnector::OnForceMovementSpeedChange);
			RegisterPacketHandler(game::realm_client_packet::ForceSetFlightSpeed, *this, &BotRealmConnector::OnForceMovementSpeedChange);
			RegisterPacketHandler(game::realm_client_packet::ForceSetFlightBackSpeed, *this, &BotRealmConnector::OnForceMovementSpeedChange);

			// Register handlers for common packets that can be safely ignored by the bot
			RegisterPacketHandler(game::realm_client_packet::UpdateObject, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::CompressedUpdateObject, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::DestroyObjects, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::MoveStop, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::MoveStartTurnLeft, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::MoveStartTurnRight, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::AuraUpdate, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::InitialSpells, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::ActionButtons, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::MoveSetWalkSpeed, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::MoveSetRunSpeed, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::MoveSetRunBackSpeed, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::MoveSetSwimSpeed, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::MoveSetSwimBackSpeed, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::MoveSetTurnRate, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::SetFlightSpeed, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::SetFlightBackSpeed, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::GameTimeInfo, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::NameQueryResult, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::AttackStart, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::SpellCooldown, *this, &BotRealmConnector::OnIgnoredPacket);
			RegisterPacketHandler(game::realm_client_packet::CreatureMove, *this, &BotRealmConnector::OnIgnoredPacket);

			RequestCharEnum();
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnCharEnum(game::IncomingPacket& packet)
	{
		m_characterViews.clear();

		if (!(packet >> io::read_container<uint8>(m_characterViews)))
		{
			return PacketParseResult::Disconnect;
		}

		CharListUpdated();

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnLoginVerifyWorld(game::IncomingPacket& packet)
	{
		uint32 mapId;
		Vector3 position;
		float facing;

		if (!(packet >> io::read<uint32>(mapId)
			>> io::read<float>(position.x)
			>> io::read<float>(position.y)
			>> io::read<float>(position.z)
			>> io::read<float>(facing)))
		{
			return PacketParseResult::Disconnect;
		}

		m_movementInfo.position = position;
		m_movementInfo.facing = Radian(facing);
		m_movementInfo.timestamp = GetAsyncTimeMs();

		VerifyNewWorld(mapId, position, facing);

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnEnterWorldFailed(game::IncomingPacket& packet)
	{
		game::player_login_response::Type response;
		if (!(packet >> io::read<uint8>(response)))
		{
			return PacketParseResult::Disconnect;
		}

		ELOG("Failed to enter world: " << response);
		EnterWorldFailed(response);

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnTimeSyncRequest(game::IncomingPacket& packet)
	{
		uint32 syncIndex;
		if (!(packet >> io::read<uint32>(syncIndex)))
		{
			ELOG("Failed to read TimeSyncRequest packet!");
			return PacketParseResult::Disconnect;
		}

		GameTime clientTimestamp = GetAsyncTimeMs();
		SendTimeSyncResponse(syncIndex, clientTimestamp);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnTransferPending(game::IncomingPacket& packet)
	{
		uint32 mapId;
		if (!(packet >> io::read<uint32>(mapId)))
		{
			ELOG("Failed to read TransferPending packet!");
			return PacketParseResult::Disconnect;
		}

		ILOG("Transfer pending to map " << mapId << "...");
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnNewWorld(game::IncomingPacket& packet)
	{
		uint32 mapId;
		Vector3 position;
		float facingRadianVal;
		if (!(packet >> io::read<uint32>(mapId) >> io::read<float>(position.x) >> io::read<float>(position.y) >> io::read<float>(position.z) >> io::read<float>(facingRadianVal)))
		{
			ELOG("Failed to read NewWorld packet!");
			return PacketParseResult::Disconnect;
		}

		ILOG("Transfer to map " << mapId << " completed.");
		m_movementInfo.position = position;
		m_movementInfo.facing = Radian(facingRadianVal);
		m_movementInfo.timestamp = GetAsyncTimeMs();

		VerifyNewWorld(mapId, position, facingRadianVal);
		SendMoveWorldPortAck();

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnCharCreateResponse(game::IncomingPacket& packet)
	{
		game::CharCreateResult result;
		if (!(packet >> io::read<uint8>(result)))
		{
			return PacketParseResult::Disconnect;
		}

		CharacterCreated(result);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnMoveTeleport(game::IncomingPacket& packet)
	{
		uint64 guid;
		if (!(packet >> io::read_packed_guid(guid)))
		{
			ELOG("Failed to read teleported mover guid!");
			return PacketParseResult::Disconnect;
		}

		uint32 ackId;
		MovementInfo movementInfo;
		if (!(packet >> io::read<uint32>(ackId) >> movementInfo))
		{
			ELOG("Failed to read move teleport packet");
			return PacketParseResult::Disconnect;
		}

		if (m_selectedCharacterGuid == guid)
		{
			m_movementInfo = movementInfo;
			SendMoveTeleportAck(ackId, m_movementInfo);
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnForceMovementSpeedChange(game::IncomingPacket& packet)
	{
		uint32 ackId;
		float speed;

		if (!(packet >> io::read<uint32>(ackId) >> io::read<float>(speed)))
		{
			WLOG("Failed to read force movement speed change packet!");
			return PacketParseResult::Disconnect;
		}

		SendMovementSpeedAck(MovementTypeFromForcePacket(packet.GetId()), ackId, speed, m_movementInfo);
		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnIgnoredPacket(game::IncomingPacket& packet)
	{
		// Silently ignore this packet - it's not important for the bot's functionality
		return PacketParseResult::Pass;
	}
}
