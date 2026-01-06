// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bot_realm_connector.h"

#include "version.h"
#include "game/field_map.h"
#include "game/movement_info.h"
#include "game/object_type_id.h"

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

			// Register party invitation handler
			RegisterPacketHandler(game::realm_client_packet::GroupInvite, *this, &BotRealmConnector::OnGroupInvite);

			// Register party state handlers
			RegisterPacketHandler(game::realm_client_packet::GroupList, *this, &BotRealmConnector::OnGroupList);
			RegisterPacketHandler(game::realm_client_packet::GroupDestroyed, *this, &BotRealmConnector::OnGroupDestroyed);
			RegisterPacketHandler(game::realm_client_packet::GroupSetLeader, *this, &BotRealmConnector::OnGroupSetLeader);

			// Register handlers for common packets that can be safely ignored by the bot
			RegisterPacketHandler(game::realm_client_packet::UpdateObject, *this, &BotRealmConnector::OnUpdateObject);
			RegisterPacketHandler(game::realm_client_packet::CompressedUpdateObject, *this, &BotRealmConnector::OnIgnoredPacket);  // TODO: Implement compression
			RegisterPacketHandler(game::realm_client_packet::DestroyObjects, *this, &BotRealmConnector::OnDestroyObjects);
			RegisterPacketHandler(game::realm_client_packet::NameQueryResult, *this, &BotRealmConnector::OnNameQueryResult);
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

	PacketParseResult BotRealmConnector::OnGroupInvite(game::IncomingPacket& packet)
	{
		String inviterName;
		if (!(packet >> io::read_container<uint8>(inviterName)))
		{
			ELOG("Failed to read GroupInvite packet!");
			return PacketParseResult::Disconnect;
		}

		ILOG("Received party invitation from " << inviterName);

		// Trigger the signal for the bot profile to handle
		PartyInvitationReceived(inviterName);

		return PacketParseResult::Pass;
	}

	void BotRealmConnector::AcceptPartyInvitation()
	{
		// Send GroupAccept packet to the server
		sendSinglePacket([](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::GroupAccept);
			packet.Finish();
		});

		ILOG("Accepted party invitation");
	}

	void BotRealmConnector::DeclinePartyInvitation()
	{
		// Send GroupDecline packet to the server
		sendSinglePacket([](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::GroupDecline);
			packet.Finish();
		});

		ILOG("Declined party invitation");
	}

	const BotPartyMember* BotRealmConnector::GetPartyMember(uint32 index) const
	{
		if (!m_inParty || index >= m_partyMembers.size())
		{
			return nullptr;
		}

		return &m_partyMembers[index];
	}

	std::vector<uint64> BotRealmConnector::GetPartyMemberGuids() const
	{
		std::vector<uint64> guids;
		if (m_inParty)
		{
			guids.reserve(m_partyMembers.size());
			for (const auto& member : m_partyMembers)
			{
				guids.push_back(member.guid);
			}
		}
		return guids;
	}

	void BotRealmConnector::LeaveParty()
	{
		if (!m_inParty)
		{
			WLOG("Cannot leave party: Not in a party");
			return;
		}

		// Send GroupUninvite with empty name to leave
		sendSinglePacket([](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::GroupUninvite);
			packet << io::write_dynamic_range<uint8>(std::string{});
			packet.Finish();
		});

		ILOG("Left party");
	}

	void BotRealmConnector::KickFromParty(const std::string& playerName)
	{
		if (!m_inParty)
		{
			WLOG("Cannot kick from party: Not in a party");
			return;
		}

		if (!IsPartyLeader())
		{
			WLOG("Cannot kick from party: Not the party leader");
			return;
		}

		sendSinglePacket([&playerName](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::GroupUninvite);
			packet << io::write_dynamic_range<uint8>(playerName);
			packet.Finish();
		});

		ILOG("Kicked " << playerName << " from party");
	}

	void BotRealmConnector::InviteToParty(const std::string& playerName)
	{
		sendSinglePacket([&playerName](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::GroupInvite);
			packet << io::write_dynamic_range<uint8>(playerName);
			packet.Finish();
		});

		ILOG("Invited " << playerName << " to party");
	}

	PacketParseResult BotRealmConnector::OnGroupList(game::IncomingPacket& packet)
	{
		uint8 groupType = 0;
		uint8 isAssistant = 0;
		uint8 memberCount = 0;

		if (!(packet >> io::read<uint8>(groupType) >> io::read<uint8>(isAssistant) >> io::read<uint8>(memberCount)))
		{
			ELOG("Failed to read GroupList packet header!");
			return PacketParseResult::Disconnect;
		}

		const bool wasInParty = m_inParty;

		// Read party members
		m_partyMembers.clear();
		m_partyMembers.resize(memberCount);

		for (uint8 i = 0; i < memberCount; ++i)
		{
			String memberName;
			if (!(packet 
				>> io::read_container<uint8>(memberName)
				>> io::read<uint64>(m_partyMembers[i].guid)
				>> io::read<uint8>(m_partyMembers[i].status)
				>> io::read<uint8>(m_partyMembers[i].group)
				>> io::read<uint8>(m_partyMembers[i].assistant)))
			{
				ELOG("Failed to read GroupList member data!");
				return PacketParseResult::Disconnect;
			}
			m_partyMembers[i].name = memberName;
		}

		// Read leader guid
		if (!(packet >> io::read<uint64>(m_partyLeaderGuid)))
		{
			ELOG("Failed to read GroupList leader guid!");
			return PacketParseResult::Disconnect;
		}

		// We're in a party if group type is not 0 (none)
		m_inParty = (groupType != 0);

		ILOG("GroupList received: " << static_cast<int>(memberCount) << " members, leader: " << m_partyLeaderGuid);

		// Fire signal if we just joined a party
		if (m_inParty && !wasInParty)
		{
			PartyJoined(m_partyLeaderGuid, static_cast<uint32>(memberCount));
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnGroupDestroyed(game::IncomingPacket& packet)
	{
		ILOG("Party has been disbanded");

		m_partyMembers.clear();
		m_partyLeaderGuid = 0;
		m_inParty = false;

		PartyLeft();

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnGroupSetLeader(game::IncomingPacket& packet)
	{
		String newLeaderName;
		if (!(packet >> io::read_container<uint8>(newLeaderName)))
		{
			ELOG("Failed to read GroupSetLeader packet!");
			return PacketParseResult::Disconnect;
		}

		// Find the member with this name and update leader guid
		for (const auto& member : m_partyMembers)
		{
			if (member.name == newLeaderName)
			{
				m_partyLeaderGuid = member.guid;
				ILOG("Party leader changed to " << newLeaderName);
				break;
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnUpdateObject(game::IncomingPacket& packet)
	{
		uint16 numObjectUpdates;
		if (!(packet >> io::read<uint16>(numObjectUpdates)))
		{
			ELOG("Failed to read UpdateObject count");
			return PacketParseResult::Disconnect;
		}

		for (uint16 i = 0; i < numObjectUpdates; ++i)
		{
			ObjectTypeId typeId;
			uint8 creation;
			if (!(packet >> io::read<uint8>(typeId) >> io::read<uint8>(creation)))
			{
				ELOG("Failed to read object update header #" << i);
				return PacketParseResult::Disconnect;
			}

			// Only process units and players - skip items, containers, world objects
			if (typeId != ObjectTypeId::Unit && typeId != ObjectTypeId::Player)
			{
				// Skip this object - we need to read the data to advance the stream
				// but we don't store non-unit types
				if (!ParseObjectUpdate(packet, creation != 0, typeId))
				{
					ELOG("Failed to skip non-unit object #" << i << " (type: " << static_cast<int>(typeId) << ")");
					return PacketParseResult::Disconnect;
				}
				continue;
			}

			if (!ParseObjectUpdate(packet, creation != 0, typeId))
			{
				ELOG("Failed to parse object update #" << i << " (type: " << static_cast<int>(typeId) << ")");
				return PacketParseResult::Disconnect;
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnDestroyObjects(game::IncomingPacket& packet)
	{
		uint16 objectCount;
		if (!(packet >> io::read<uint16>(objectCount)))
		{
			ELOG("Failed to read DestroyObjects count");
			return PacketParseResult::Disconnect;
		}

		for (uint16 i = 0; i < objectCount; ++i)
		{
			uint64 guid;
			if (!(packet >> io::read_packed_guid(guid)))
			{
				ELOG("Failed to read destroyed object GUID #" << i);
				return PacketParseResult::Disconnect;
			}

			// Remove from object manager if it exists
			if (m_objectManager.RemoveUnit(guid))
			{
				UnitDespawned(guid);
			}
		}

		return PacketParseResult::Pass;
	}

	PacketParseResult BotRealmConnector::OnNameQueryResult(game::IncomingPacket& packet)
	{
		uint64 guid;
		uint8 found;
		if (!(packet >> io::read<uint64>(guid) >> io::read<uint8>(found)))
		{
			ELOG("Failed to read NameQueryResult header");
			return PacketParseResult::Disconnect;
		}

		if (found == 0)
		{
			// Player not found
			return PacketParseResult::Pass;
		}

		String name;
		if (!(packet >> io::read_container<uint8>(name)))
		{
			ELOG("Failed to read player name from NameQueryResult");
			return PacketParseResult::Disconnect;
		}

		// Update the unit's name if we have it
		if (BotUnit* unit = m_objectManager.GetUnitMutable(guid))
		{
			unit->SetName(name);
			UnitUpdated(*unit);
		}

		return PacketParseResult::Pass;
	}

	bool BotRealmConnector::ParseObjectUpdate(io::Reader& reader, const bool creation, const ObjectTypeId typeId)
	{
		uint64 guid = 0;

		// For updates (not creation), read the GUID first
		if (!creation)
		{
			if (!(reader >> io::read_packed_guid(guid)))
			{
				ELOG("Failed to read object GUID for update");
				return false;
			}
		}

		// Read update flags
		uint32 updateFlags;
		if (!(reader >> io::read<uint32>(updateFlags)))
		{
			ELOG("Failed to read update flags");
			return false;
		}

		// Read movement info if present
		MovementInfo movementInfo;
		if (updateFlags & object_update_flags::HasMovementInfo)
		{
			if (!(reader >> movementInfo))
			{
				ELOG("Failed to read movement info");
				return false;
			}
		}

		// Determine field count based on type
		size_t fieldCount = 0;
		switch (typeId)
		{
		case ObjectTypeId::Object:
			fieldCount = object_fields::WorldObjectFieldCount;
			break;
		case ObjectTypeId::Unit:
			fieldCount = object_fields::UnitFieldCount;
			break;
		case ObjectTypeId::Player:
			fieldCount = object_fields::PlayerFieldCount;
			break;
		case ObjectTypeId::Item:
			fieldCount = object_fields::ItemFieldCount;
			break;
		case ObjectTypeId::Container:
			fieldCount = object_fields::BagFieldCount;
			break;
		default:
			fieldCount = object_fields::ObjectFieldCount;
			break;
		}

		// Create a temporary field map for reading
		FieldMap<uint32> fieldMap;
		fieldMap.Initialize(fieldCount);

		// Read field data
		if (creation)
		{
			if (!(fieldMap.DeserializeComplete(reader)))
			{
				ELOG("Failed to deserialize complete field map");
				return false;
			}
		}
		else
		{
			if (!(fieldMap.DeserializeChanges(reader)))
			{
				ELOG("Failed to deserialize field map changes");
				return false;
			}
		}

		// For creation, the GUID is in the field map
		if (creation)
		{
			guid = fieldMap.GetFieldValue<uint64>(object_fields::Guid);
		}

		// Read unit speeds (only for units and players)
		std::array<float, static_cast<size_t>(MovementType::Count)> speeds = {};
		if (typeId == ObjectTypeId::Unit || typeId == ObjectTypeId::Player)
		{
			if (!(reader 
				>> io::read<float>(speeds[static_cast<size_t>(MovementType::Walk)])
				>> io::read<float>(speeds[static_cast<size_t>(MovementType::Run)])
				>> io::read<float>(speeds[static_cast<size_t>(MovementType::Backwards)])
				>> io::read<float>(speeds[static_cast<size_t>(MovementType::Swim)])
				>> io::read<float>(speeds[static_cast<size_t>(MovementType::SwimBackwards)])
				>> io::read<float>(speeds[static_cast<size_t>(MovementType::Flight)])
				>> io::read<float>(speeds[static_cast<size_t>(MovementType::FlightBackwards)])
				>> io::read<float>(speeds[static_cast<size_t>(MovementType::Turn)])))
			{
				ELOG("Failed to read unit speeds");
				return false;
			}
		}

		// Only store units and players
		if (typeId != ObjectTypeId::Unit && typeId != ObjectTypeId::Player)
		{
			return true;  // Successfully parsed but not stored
		}

		// Check if unit already exists
		const bool isNewUnit = !m_objectManager.HasUnit(guid);

		// Create or update the bot unit
		BotUnit unit(guid, typeId);

		// Extract relevant fields
		unit.SetEntry(fieldMap.GetFieldValue<uint32>(object_fields::Entry));
		unit.SetLevel(fieldMap.GetFieldValue<uint32>(object_fields::Level));
		unit.SetHealth(fieldMap.GetFieldValue<uint32>(object_fields::Health));
		unit.SetMaxHealth(fieldMap.GetFieldValue<uint32>(object_fields::MaxHealth));
		unit.SetFactionTemplate(fieldMap.GetFieldValue<uint32>(object_fields::FactionTemplate));
		unit.SetDisplayId(fieldMap.GetFieldValue<uint32>(object_fields::DisplayId));
		unit.SetUnitFlags(fieldMap.GetFieldValue<uint32>(object_fields::Flags));
		unit.SetNpcFlags(fieldMap.GetFieldValue<uint32>(object_fields::NpcFlags));
		unit.SetTargetGuid(fieldMap.GetFieldValue<uint64>(object_fields::TargetUnit));

		// Set position/movement
		if (updateFlags & object_update_flags::HasMovementInfo)
		{
			unit.SetMovementInfo(movementInfo);
		}

		// Set speeds
		unit.SetSpeeds(speeds);

		// If updating existing unit, preserve the name
		if (!isNewUnit)
		{
			if (const BotUnit* existingUnit = m_objectManager.GetUnit(guid))
			{
				if (!existingUnit->GetName().empty())
				{
					unit.SetName(existingUnit->GetName());
				}
			}
		}

		// Add or update in object manager
		m_objectManager.AddOrUpdateUnit(unit);

		// Set self guid if this is our character
		if (guid == m_selectedCharacterGuid)
		{
			m_objectManager.SetSelfGuid(guid);
		}

		// Emit signals
		if (isNewUnit)
		{
			UnitSpawned(unit);
		}
		else
		{
			UnitUpdated(unit);
		}

		return true;
	}
}
