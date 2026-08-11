// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "realm_connector.h"
#include "version.h"
#include "game/movement_info.h"

#include "base/constants.h"
#include "base/localization.h"
#include "base/random.h"
#include "base/sha1.h"
#include "console/console_var.h"
#include "game/spell_target_map.h"
#include "game_states/login_state.h"
#include "log/default_log_levels.h"


namespace mmo
{
	/// @brief Console variable to enable debug logging of movement packets sent by the client.
	static ConsoleVar* s_debugMovementPacketsCVar = nullptr;

	/// @brief Helper function to get the name of a movement opcode for logging.
	static const char* GetMovementOpCodeName(uint16 opCode)
	{
		switch (opCode)
		{
		case game::client_realm_packet::MoveStartForward: return "MOVE_START_FORWARD";
		case game::client_realm_packet::MoveStartBackward: return "MOVE_START_BACKWARD";
		case game::client_realm_packet::MoveStop: return "MOVE_STOP";
		case game::client_realm_packet::MoveStartStrafeLeft: return "MOVE_START_STRAFE_LEFT";
		case game::client_realm_packet::MoveStartStrafeRight: return "MOVE_START_STRAFE_RIGHT";
		case game::client_realm_packet::MoveStopStrafe: return "MOVE_STOP_STRAFE";
		case game::client_realm_packet::MoveJump: return "MOVE_JUMP";
		case game::client_realm_packet::MoveStartTurnLeft: return "MOVE_START_TURN_LEFT";
		case game::client_realm_packet::MoveStartTurnRight: return "MOVE_START_TURN_RIGHT";
		case game::client_realm_packet::MoveStopTurn: return "MOVE_STOP_TURN";
		case game::client_realm_packet::MoveFallLand: return "MOVE_FALL_LAND";
		case game::client_realm_packet::MoveSetFacing: return "MOVE_SET_FACING";
		case game::client_realm_packet::MoveHeartBeat: return "MOVE_HEARTBEAT";
		case game::client_realm_packet::MoveEnded: return "MOVE_ENDED";
		case game::client_realm_packet::MoveStartWalk: return "MOVE_START_WALK";
		case game::client_realm_packet::MoveStopWalk: return "MOVE_STOP_WALK";
		case game::client_realm_packet::MoveChargeAck: return "MOVE_CHARGE_ACK";
		default: return "UNKNOWN";
		}
	}

	RealmConnector::RealmConnector(asio::io_service & io)
		: game::Connector(std::make_unique<asio::ip::tcp::socket>(io), nullptr)
		, m_ioService(io)
		, m_realmPort(0)
		, m_serverSeed(0)
		, m_clientSeed(0)
		, m_realmId(0)
	{
		s_debugMovementPacketsCVar = ConsoleVarMgr::RegisterConsoleVar(
			"debugMovementPackets",
			"Enables debug logging of movement packets sent to the server (0=off, 1=on)",
			"0");
	}

	PacketParseResult RealmConnector::OnAuthChallenge(game::IncomingPacket & packet)
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
		Sha1_Add_BigNumbers(hashGen, {m_sessionKey});
		SHA1Hash hash = hashGen.finalize();

		// Listen for response packet
		RegisterPacketHandler(game::realm_client_packet::AuthSessionResponse, *this, &RealmConnector::OnAuthSessionResponse);

		// We have been challenged, respond with an answer
		sendSinglePacket([this, &hash](game::OutgoingPacket& packet) {
			// Resolve the client locale from the "locale" console variable so the realm
			// (and, via the realm, the world) serves game data in the correct language.
			const auto* localeCVar = ConsoleVarMgr::FindConsoleVar("locale");
			const LocaleIndex clientLocale = localeCVar ?
				LocaleIndexFromString(localeCVar->GetStringValue()) : LocaleIndex::enUS;

			packet.Start(game::client_realm_packet::AuthSession);
			packet
				<< io::write<uint32>(mmo::Revision)
				<< io::write_dynamic_range<uint8>(this->m_account)
				<< io::write<uint32>(m_clientSeed)
				<< io::write_range(hash)
				<< io::write<uint32>(LocaleFourCC(clientLocale));	// Locale (from "locale" CVar)
			packet.Finish();
		});

		// NOTE: Connection encryption is NOT initialized here. The AuthSessionResponse packet is sent
		// unencrypted by the realm so that it can also deliver an authentication *failure* (where the
		// realm never receives a session key from the login server and thus cannot encrypt). Encryption
		// is initialized in OnAuthSessionResponse once we know the session was accepted.

		// Debug log
		ILOG("[Realm] Handshaking...");

		// Proceed handling network packets
		return PacketParseResult::Pass;
	}

	PacketParseResult RealmConnector::OnKickReason(game::IncomingPacket& packet)
	{
		uint8 reason = 0;
		if (!(packet >> io::read<uint8>(reason)))
		{
			return PacketParseResult::Disconnect;
		}

		if (reason > auth::session_kick_reason::AccountBanned)
		{
			// A newer server may know reasons this build does not. The disconnect that follows is
			// what matters; without a reason we can recognise, the generic message is shown.
			WLOG("Realm sent unknown session kick reason " << static_cast<uint16>(reason));
			return PacketParseResult::Pass;
		}

		m_kickReason = static_cast<auth::SessionKickReason>(reason);

		return PacketParseResult::Pass;
	}

	PacketParseResult RealmConnector::OnAuthSessionResponse(game::IncomingPacket & packet)
	{
		// No longer accept these packets from here on!
		ClearPacketHandler(game::realm_client_packet::AuthSessionResponse);

		// Try to read packet data
		uint8 result = 0;
		if (!(packet >> io::read<uint8>(result)))
		{
			return PacketParseResult::Disconnect;
		}

		// Notify the UI about the authentication result
		AuthenticationResult(result);

		// Was this a success?
		if (result == game::auth_result::Success)
		{
			// The session was accepted: initialize connection encryption now. Everything after the
			// (unencrypted) AuthSessionResponse is encrypted on both sides.
			HMACHash cryptKey;
			game::Crypt::GenerateKey(cryptKey, m_sessionKey);

			game::Crypt& crypt = GetCrypt();
			crypt.SetKey(cryptKey.data(), cryptKey.size());
			crypt.Init();

			// From here on, we accept CharEnum packets
			RegisterPacketHandler(game::realm_client_packet::CharEnum, *this, &RealmConnector::OnCharEnum);
			RegisterPacketHandler(game::realm_client_packet::LoginVerifyWorld, *this, &RealmConnector::OnLoginVerifyWorld);
			RegisterPacketHandler(game::realm_client_packet::EnterWorldFailed, *this, &RealmConnector::OnEnterWorldFailed);
			RegisterPacketHandler(game::realm_client_packet::RealmConfig, *this, &RealmConnector::OnRealmConfig);

			// And now, we ask for the character list
			sendSinglePacket([](game::OutgoingPacket& outPacket)
			{
				outPacket.Start(game::client_realm_packet::CharEnum);
				// This packet is empty
				outPacket.Finish();
			});

			return PacketParseResult::Pass;
		}

		// Authentication was rejected by the realm (e.g. not allowed to connect). The UI has already
		// been notified above and will show an error dialog. Close the connection cleanly via close()
		// instead of returning Disconnect: the Disconnect path resets the underlying socket to null,
		// which would crash the next ConnectToRealm() call (access violation in connect()/is_open()).
		// close() keeps the socket object alive so the player can return to the realm list and connect
		// again.
		close();
		return PacketParseResult::Pass;
	}

	PacketParseResult RealmConnector::OnCharEnum(game::IncomingPacket & packet)
	{
		// Delete old characters in cache
		m_characterViews.clear();

		// Load new character view list
		if (!(packet >> io::read_container<uint8>(m_characterViews)))
		{
			return PacketParseResult::Disconnect;
		}

		// Notify about character list update
		CharListUpdated();

		return PacketParseResult::Pass;
	}

	PacketParseResult RealmConnector::OnLoginVerifyWorld(game::IncomingPacket& packet)
	{
		DLOG("New world packet received");

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

		// TODO: We might need to do another world load here because the map id might be different from the map id we expected when logging in?
		VerifyNewWorld(mapId, position, facing);

		return PacketParseResult::Pass;
	}
	
	PacketParseResult RealmConnector::OnEnterWorldFailed(game::IncomingPacket& packet)
	{
		game::player_login_response::Type response;
		if(!(packet >> io::read<uint8>(response)))
		{
			return PacketParseResult::Disconnect;
		}

		ELOG("Failed to enter world: " << response);
		EnterWorldFailed(response);

		return PacketParseResult::Pass;
	}

	PacketParseResult RealmConnector::OnRealmConfig(game::IncomingPacket& packet)
	{
		m_disabledRaces.clear();
		m_disabledClasses.clear();

		uint8 numDisabledRaces = 0;
		if (!(packet >> io::read<uint8>(numDisabledRaces)))
		{
			return PacketParseResult::Disconnect;
		}

		for (uint8 i = 0; i < numDisabledRaces; ++i)
		{
			uint8 raceId = 0;
			if (!(packet >> io::read<uint8>(raceId)))
			{
				return PacketParseResult::Disconnect;
			}
			m_disabledRaces.insert(raceId);
		}

		uint8 numDisabledClasses = 0;
		if (!(packet >> io::read<uint8>(numDisabledClasses)))
		{
			return PacketParseResult::Disconnect;
		}

		for (uint8 i = 0; i < numDisabledClasses; ++i)
		{
			uint8 classId = 0;
			if (!(packet >> io::read<uint8>(classId)))
			{
				return PacketParseResult::Disconnect;
			}
			m_disabledClasses.insert(classId);
		}

		DLOG("Realm config received: " << static_cast<int>(numDisabledRaces) << " disabled race(s), "
			<< static_cast<int>(numDisabledClasses) << " disabled class(es)");

		return PacketParseResult::Pass;
	}
	
	bool RealmConnector::connectionEstablished(bool success)
	{
		if (m_cancelled)
		{
			m_cancelled = false;

			// The connection attempt was cancelled before it resolved. Discard the result silently
			// and close the socket if it did connect after all.
			if (success)
			{
				close();
			}

			return false;
		}

		if (success)
		{
			// Reset server seed
			m_serverSeed = 0;

			// A reason left over from a previous session must not be shown for this one.
			m_kickReason.reset();

			// Generate a new client seed
			std::uniform_int_distribution<uint32> dist;
			m_clientSeed = dist(RandomGenerator);

			// Accept LogonChallenge packets from here on
			RegisterPacketHandler(game::realm_client_packet::AuthChallenge, *this, &RealmConnector::OnAuthChallenge);

			// The realm may terminate the session at any point from here on -- including before we
			// finish authenticating -- so this handler is registered for the whole connection.
			RegisterPacketHandler(game::realm_client_packet::KickReason, *this, &RealmConnector::OnKickReason);
		}
		else
		{
			ELOG("Could not connect to the realm server");
		}

		return true;
	}

	void RealmConnector::connectionLost()
	{
		// Log this event
		ELOG("Lost connection to the realm server...");

		// Clear packet handlers
		ClearPacketHandlers();

		Disconnected();
	}

	void RealmConnector::connectionMalformedPacket()
	{
		ELOG("Received a malformed packet");
	}

	PacketParseResult RealmConnector::connectionPacketReceived(game::IncomingPacket & packet)
	{
		return HandleIncomingPacket(packet);
	}

	void RealmConnector::SetLoginData(const std::string & accountName, const BigNumber & sessionKey)
	{
		m_account = accountName;
		m_sessionKey = sessionKey;
	}

	void RealmConnector::ConnectToRealm(const RealmData & data)
	{
		m_realmId = data.id;
		m_realmAddress = data.address;
		m_realmPort = data.port;
		m_realmName = data.name;

		m_cancelled = false;

		// Connect to the server
		connect(m_realmAddress, m_realmPort, *this, m_ioService);
	}

	void RealmConnector::Connect(const std::string& realmAddress, uint16 realmPort, const std::string& accountName, const std::string& realmName, BigNumber sessionKey)
	{
		m_realmAddress = realmAddress;
		m_realmPort = realmPort;
		m_realmName = realmName;
		m_account = accountName;
		m_sessionKey = sessionKey;

		m_cancelled = false;

		// Connect to the server
		connect(m_realmAddress, m_realmPort, *this, m_ioService);
	}

	void RealmConnector::CancelConnect()
	{
		m_cancelled = true;
		close();
	}

	void RealmConnector::RequestCharacterList()
	{
		sendSinglePacket([](game::OutgoingPacket& outPacket)
		{
			outPacket.Start(game::client_realm_packet::CharEnum);
			outPacket.Finish();
		});
	}

	void RealmConnector::EnterWorld(const CharacterView & character)
	{
		const uint64 guid = character.GetGuid();
		
		sendSinglePacket([guid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::EnterWorld);
			packet
				<< io::write<uint64>(guid);
			packet.Finish();
		});
	}

	void RealmConnector::CreateCharacter(const std::string& name, uint8 race, uint8 characterClass, uint8 characterGender, const AvatarConfiguration& customization)
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

	void RealmConnector::DeleteCharacter(const CharacterView& character)
	{
		const uint64 guid = character.GetGuid();

		sendSinglePacket([guid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::DeleteChar);
			packet << io::write<uint64>(guid);
			packet.Finish();
			});
	}

	void RealmConnector::SendMovementUpdate(uint64 characterId, uint16 opCode, const MovementInfo& info)
	{
		// Log the movement packet if debug is enabled
		if (s_debugMovementPacketsCVar && s_debugMovementPacketsCVar->GetBoolValue())
		{
			DLOG("[MOVE OUT] " << GetMovementOpCodeName(opCode) 
				<< " pos=(" << info.position.x << ", " << info.position.y << ", " << info.position.z << ")"
				<< " flags=" << info.movementFlags
				<< " facing=" << info.facing.GetValueDegrees() << "°"
				<< " ts=" << info.timestamp);
		}

		sendSinglePacket([characterId, opCode, &info](game::OutgoingPacket& packet) {
			packet.Start(opCode);
			packet << io::write<uint64>(characterId);
			packet << info;
			packet.Finish();
			});
	}

	void RealmConnector::SetSelection(uint64 guid)
	{
		sendSinglePacket([guid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::SetSelection);
			packet << io::write<uint64>(guid);
			packet.Finish();
			});
	}

	void RealmConnector::CheckLineOfSight(uint64 targetGuid)
	{
		sendSinglePacket([targetGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatCheckLineOfSight);
			packet << io::write<uint64>(targetGuid);
			packet.Finish();
			});
	}

	void RealmConnector::CreateMonster(uint32 entry)
	{
		sendSinglePacket([entry](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatCreateMonster);
			packet << io::write<uint32>(entry);
			packet.Finish();
			});
	}

	void RealmConnector::DestroyMonster(uint64 guid)
	{
		sendSinglePacket([guid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatDestroyMonster);
			packet << io::write<uint64>(guid);
			packet.Finish();
			});
	}

	void RealmConnector::FaceMe(uint64 guid)
	{
		sendSinglePacket([guid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatFaceMe);
			packet << io::write<uint64>(guid);
			packet.Finish();
			});
	}

	void RealmConnector::FollowMe(uint64 guid)
	{
		sendSinglePacket([guid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatFollowMe);
			packet << io::write<uint64>(guid);
			packet.Finish();
			});
	}

	void RealmConnector::LearnSpell(uint32 spellId)
	{
		sendSinglePacket([spellId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatLearnSpell);
			packet << io::write<uint32>(spellId);
			packet.Finish();
			});
	}

	void RealmConnector::LearnEmote(uint32 emoteId)
	{
		sendSinglePacket([emoteId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatLearnEmote);
			packet << io::write<uint32>(emoteId);
			packet.Finish();
			});
	}

	void RealmConnector::SendEmote(uint32 emoteId, uint64 targetGuid)
	{
		sendSinglePacket([emoteId, targetGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::Emote);
			packet << io::write<uint32>(emoteId) << io::write<uint64>(targetGuid);
			packet.Finish();
			});
	}

	void RealmConnector::SendCyclePose()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CyclePose);
			packet.Finish();
			});
	}

	void RealmConnector::LevelUp(uint8 level)
	{
		sendSinglePacket([level](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatLevelUp);
			packet << io::write<uint8>(level);
			packet.Finish();
			});
	}

	void RealmConnector::ClassLevelUp(uint8 levels)
	{
		sendSinglePacket([levels](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatClassLevelUp);
			packet << io::write<uint8>(levels);
			packet.Finish();
			});
	}

	void RealmConnector::GiveMoney(uint32 amount)
	{
		sendSinglePacket([amount](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatGiveMoney);
			packet << io::write<uint32>(amount);
			packet.Finish();
			});
	}

	void RealmConnector::AddItem(uint32 itemId, uint8 count)
	{
		sendSinglePacket([itemId, count](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatAddItem);
			packet << io::write<uint32>(itemId) << io::write<uint8>(count);
			packet.Finish();
			});
	}

	void RealmConnector::WorldPort(uint32 mapId, const Vector3& position, const Radian& facing)
	{
		sendSinglePacket([mapId, &position, &facing](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatWorldPort);
			packet << io::write<uint32>(mapId) << io::write<float>(position.x) << io::write<float>(position.y) << io::write<float>(position.z) << io::write<float>(facing.GetValueRadians());
			packet.Finish();
			});
	}

	void RealmConnector::SummonPlayer(const String& playerName)
	{
		sendSinglePacket([&playerName](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatSummon);
			packet << io::write_dynamic_range<uint8>(playerName);
			packet.Finish();
			});
	}

	void RealmConnector::TeleportToPlayer(const String& playerName)
	{
		sendSinglePacket([&playerName](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatTeleportToPlayer);
			packet << io::write_dynamic_range<uint8>(playerName);
			packet.Finish();
			});
	}

	void RealmConnector::SetSpeed(float speed)
	{
		sendSinglePacket([speed](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatSpeed);
			packet << io::write<float>(speed);
			packet.Finish();
			});
	}

	void RealmConnector::Morph(uint32 displayId)
	{
		sendSinglePacket([displayId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatMorph);
			packet << io::write<uint32>(displayId);
			packet.Finish();
			});
	}

	void RealmConnector::KillTarget()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatKill);
			packet.Finish();
			});
	}

	void RealmConnector::ReviveTarget()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatRevive);
			packet.Finish();
			});
	}

	void RealmConnector::CastSpell(uint32 spellId, const SpellTargetMap& targetMap)
	{
		sendSinglePacket([spellId, &targetMap](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CastSpell);
			packet
				<< io::write<uint32>(spellId)
				<< targetMap;
			packet.Finish();
			});
	}

	void RealmConnector::SendReviveRequest()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::ReviveRequest);
			packet.Finish();
			});
	}

	void RealmConnector::SendReviveResponse(bool accept)
	{
		sendSinglePacket([accept](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::ReviveResponse);
			packet << io::write<uint8>(accept ? 1 : 0);
			packet.Finish();
			});
	}

	void RealmConnector::SendMovementSpeedAck(MovementType type, uint32 ackId, float speed, const MovementInfo& movementInfo)
	{
		sendSinglePacket([type, ackId, speed, &movementInfo](game::OutgoingPacket& packet) {
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

	void RealmConnector::SendMoveTeleportAck(uint32 ackId, const MovementInfo& movementInfo)
	{
		sendSinglePacket([ackId, &movementInfo](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::MoveTeleportAck);
			packet << io::write<uint32>(ackId) << movementInfo;
			packet.Finish();
			});
	}

	void RealmConnector::SendMoveRootAck(uint32 ackId, const MovementInfo& movementInfo)
	{
		sendSinglePacket([ackId, &movementInfo](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::MoveRootAck);
			packet << io::write<uint32>(ackId) << movementInfo;
			packet.Finish();
			});
	}

	void RealmConnector::SendMoveStunAck(uint32 ackId, const MovementInfo& movementInfo)
	{
		sendSinglePacket([ackId, &movementInfo](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::MoveStunAck);
			packet << io::write<uint32>(ackId) << movementInfo;
			packet.Finish();
			});
	}

	void RealmConnector::SendMoveSleepAck(uint32 ackId, const MovementInfo& movementInfo)
	{
		sendSinglePacket([ackId, &movementInfo](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::MoveSleepAck);
			packet << io::write<uint32>(ackId) << movementInfo;
			packet.Finish();
			});
	}

	void RealmConnector::SendMoveFearAck(uint32 ackId, const MovementInfo& movementInfo)
	{
		sendSinglePacket([ackId, &movementInfo](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::MoveFearAck);
			packet << io::write<uint32>(ackId) << movementInfo;
			packet.Finish();
			});
	}

	void RealmConnector::SendMoveDisorientAck(uint32 ackId, const MovementInfo& movementInfo)
	{
		sendSinglePacket([ackId, &movementInfo](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::MoveDisorientAck);
			packet << io::write<uint32>(ackId) << movementInfo;
			packet.Finish();
			});
	}

	void RealmConnector::SendMoveChargeAck(uint32 ackId, const MovementInfo& movementInfo, float speed)
	{
		sendSinglePacket([ackId, &movementInfo, speed](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::MoveChargeAck);
			packet << io::write<uint32>(ackId) << movementInfo << io::write<float>(speed);
			packet.Finish();
			});
	}

	void RealmConnector::AutoStoreLootItem(uint8 lootSlot)
	{
		sendSinglePacket([lootSlot](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::AutoStoreLootItem);
			packet
				<< io::write<uint8>(lootSlot);
			packet.Finish();
			});
	}

	void RealmConnector::SetGroupLootMethod(const uint8 method, const uint64 masterGuid, const uint8 threshold)
	{
		sendSinglePacket([method, masterGuid, threshold](game::OutgoingPacket& packet)
		{
			packet.Start(game::client_realm_packet::SetLootMethod);
			packet
				<< io::write<uint8>(method)
				<< io::write<uint64>(masterGuid)
				<< io::write<uint8>(threshold);
			packet.Finish();
		});
	}

	void RealmConnector::AutoEquipItem(uint8 srcBag, uint8 srcSlot)
	{
		sendSinglePacket([srcBag, srcSlot](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::AutoEquipItem);
			packet
				<< io::write<uint8>(srcBag)
				<< io::write<uint8>(srcSlot);
			packet.Finish();
			});
	}

	void RealmConnector::SwapItem(uint8 srcBag, uint8 srcSlot, uint8 dstBag, uint8 dstSlot)
	{
		sendSinglePacket([srcBag, srcSlot, dstBag, dstSlot](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::SwapItem);
			packet
				<< io::write<uint8>(srcBag)
				<< io::write<uint8>(srcSlot)
				<< io::write<uint8>(dstBag)
				<< io::write<uint8>(dstSlot);
			packet.Finish();
			});
	}

	void RealmConnector::SwapInvItem(uint8 srcSlot, uint8 dstSlot)
	{
		sendSinglePacket([srcSlot, dstSlot](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::SwapInvItem);
			packet
				<< io::write<uint8>(srcSlot)
				<< io::write<uint8>(dstSlot);
			packet.Finish();
			});
	}

	void RealmConnector::SplitItem(uint8 srcBag, uint8 srcSlot, uint8 dstBag, uint8 dstSlot, uint8 count)
	{
		sendSinglePacket([srcBag, srcSlot, dstBag, dstSlot, count](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::SplitItem);
			packet
				<< io::write<uint8>(srcBag)
				<< io::write<uint8>(srcSlot)
				<< io::write<uint8>(dstBag)
				<< io::write<uint8>(dstSlot)
				<< io::write<uint8>(count);
			packet.Finish();
			});
	}

	void RealmConnector::DestroyItem(uint8 bag, uint8 slot, uint8 count)
	{
		sendSinglePacket([bag, slot, count](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::DestroyItem);
			packet
				<< io::write<uint8>(bag)
				<< io::write<uint8>(slot)
				<< io::write<uint8>(count);
			packet.Finish();
			});
	}

	void RealmConnector::Loot(uint64 lootObjectGuid)
	{
		sendSinglePacket([lootObjectGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::Loot);
			packet
				<< io::write<uint64>(lootObjectGuid);
			packet.Finish();
			});
	}

	void RealmConnector::LootMoney()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::LootMoney);
			packet.Finish();
			});
	}

	void RealmConnector::LootRelease(uint64 lootObjectGuid)
	{
		sendSinglePacket([lootObjectGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::LootRelease);
			packet
				<< io::write<uint64>(lootObjectGuid);
			packet.Finish();
			});
	}

	void RealmConnector::UseObject(uint64 objectGuid)
	{
		sendSinglePacket([objectGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::UseObject);
			packet
				<< io::write<uint64>(objectGuid);
			packet.Finish();
			});
	}

	void RealmConnector::GossipHello(uint64 targetGuid)
	{
		sendSinglePacket([targetGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GossipHello);
			packet
				<< io::write<uint64>(targetGuid);
			packet.Finish();
			});
	}

	void RealmConnector::QuestGiverHello(uint64 targetGuid)
	{
		sendSinglePacket([targetGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::QuestGiverHello);
			packet
				<< io::write<uint64>(targetGuid);
			packet.Finish();
			});
	}

	void RealmConnector::TrainerMenu(uint64 targetGuid)
	{
		sendSinglePacket([targetGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::TrainerMenu);
			packet
				<< io::write<uint64>(targetGuid);
			packet.Finish();
			});
	}

	void RealmConnector::ListInventory(uint64 targetGuid)
	{
		sendSinglePacket([targetGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::ListInventory);
			packet
				<< io::write<uint64>(targetGuid);
			packet.Finish();
			});
	}

	void RealmConnector::BankerActivate(uint64 targetGuid)
	{
		sendSinglePacket([targetGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::BankerActivate);
			packet
				<< io::write<uint64>(targetGuid);
			packet.Finish();
			});
	}

	void RealmConnector::BuyBankBagSlot(uint64 bankerGuid)
	{
		sendSinglePacket([bankerGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::BuyBankBagSlot);
			packet
				<< io::write<uint64>(bankerGuid);
			packet.Finish();
			});
	}

	void RealmConnector::MailListRequest()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::MailListRequest);
			packet.Finish();
			});
	}

	void RealmConnector::SendMail(const String& recipient, const String& subject, const String& body, uint32 money, const std::vector<uint16>& itemSlots)
	{
		sendSinglePacket([&recipient, &subject, &body, money, &itemSlots](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::SendMail);
			packet
				<< io::write_dynamic_range<uint8>(recipient)
				<< io::write_dynamic_range<uint8>(subject)
				<< io::write_dynamic_range<uint16>(body)
				<< io::write<uint32>(money)
				<< io::write<uint8>(itemSlots.size());
			for (const uint16 slot : itemSlots)
			{
				packet << io::write<uint16>(slot);
			}
			packet.Finish();
			});
	}

	void RealmConnector::MailTakeMoney(uint64 mailId)
	{
		sendSinglePacket([mailId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::MailTakeMoney);
			packet << io::write<uint64>(mailId);
			packet.Finish();
			});
	}

	void RealmConnector::MailTakeItem(uint64 mailId, uint64 attachmentId)
	{
		sendSinglePacket([mailId, attachmentId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::MailTakeItem);
			packet
				<< io::write<uint64>(mailId)
				<< io::write<uint64>(attachmentId);
			packet.Finish();
			});
	}

	void RealmConnector::MailDelete(uint64 mailId)
	{
		sendSinglePacket([mailId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::MailDelete);
			packet << io::write<uint64>(mailId);
			packet.Finish();
			});
	}

	void RealmConnector::MailMarkRead(uint64 mailId)
	{
		sendSinglePacket([mailId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::MailMarkRead);
			packet << io::write<uint64>(mailId);
			packet.Finish();
			});
	}

	void RealmConnector::SellItem(uint64 vendorGuid, uint64 itemGuid)
	{
		sendSinglePacket([vendorGuid, itemGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::SellItem);
			packet
				<< io::write<uint64>(vendorGuid)
				<< io::write<uint64>(itemGuid);
			packet.Finish();
			});
	}

	void RealmConnector::BuyItem(uint64 vendorGuid, uint32 itemId, uint8 count)
	{
		sendSinglePacket([vendorGuid, itemId, count](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::BuyItem);
			packet
				<< io::write<uint64>(vendorGuid)
				<< io::write<uint32>(itemId)
				<< io::write<uint8>(count)
				;
			packet.Finish();
			});
	}

	void RealmConnector::AddAttributePoint(uint32 attribute)
	{
		sendSinglePacket([attribute](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::AttributePoint);
			packet
				<< io::write<uint32>(attribute);
			packet.Finish();
			});
	}

	void RealmConnector::UseItem(uint8 srcBag, uint8 srcSlot, uint64 itemGuid, const SpellTargetMap& targetMap)
	{
		sendSinglePacket([srcBag, srcSlot, itemGuid, &targetMap](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::UseItem);
			packet
				<< io::write<uint8>(srcBag)
				<< io::write<uint8>(srcSlot)
				<< io::write<uint64>(itemGuid)
				<< targetMap
			;
			packet.Finish();
			});
	}

	void RealmConnector::SetActionBarButton(uint8 index, const ActionButton& button)
	{
		sendSinglePacket([index, &button](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::SetActionBarButton);
			packet << io::write<uint8>(index) << button;
			packet.Finish();
			});
	}

	void RealmConnector::CancelCast()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CancelCast);
			packet.Finish();
			});
	}

	void RealmConnector::CancelAura(const uint32 spellId)
	{
		sendSinglePacket([spellId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CancelAura);
			packet << io::write<uint32>(spellId);
			packet.Finish();
			});
	}

	void RealmConnector::TrainerBuySpell(uint64 trainerGuid, uint32 spellId)
	{
		sendSinglePacket([trainerGuid, spellId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::TrainerBuySpell);
			packet << io::write<uint64>(trainerGuid) << io::write<uint32>(spellId);
			packet.Finish();
			});
	}

	void RealmConnector::UpdateQuestStatus(const uint64 questGiverGuid)
	{
		sendSinglePacket([questGiverGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::QuestGiverStatusQuery);
			packet << io::write<uint64>(questGiverGuid);
			packet.Finish();
			});
	}

	void RealmConnector::AcceptQuest(uint64 questGiverGuid, uint32 questId)
	{
		sendSinglePacket([questGiverGuid, questId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::AcceptQuest);
			packet << io::write<uint64>(questGiverGuid) << io::write<uint32>(questId);
			packet.Finish();
			});
	}

	void RealmConnector::QuestGiverQueryQuest(uint64 questGiverGuid, uint32 questId)
	{
		sendSinglePacket([questGiverGuid, questId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::QuestGiverQueryQuest);
			packet << io::write<uint64>(questGiverGuid) << io::write<uint32>(questId);
			packet.Finish();
			});
	}

	void RealmConnector::QuestGiverCompleteQuest(uint64 questGiverGuid, uint32 questId)
	{
		sendSinglePacket([questGiverGuid, questId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::QuestGiverCompleteQuest);
			packet << io::write<uint64>(questGiverGuid) << io::write<uint32>(questId);
			packet.Finish();
			});
	}

	void RealmConnector::AbandonQuest(uint32 questId)
	{
		sendSinglePacket([questId](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::AbandonQuest);
			packet << io::write<uint32>(questId);
			packet.Finish();
			});
	}

	void RealmConnector::QuestGiverChooseQuestReward(uint64 questGiverGuid, uint32 questId, uint32 rewardChoice)
	{
		sendSinglePacket([questGiverGuid, questId, rewardChoice](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::QuestGiverChooseQuestReward);
			packet << io::write<uint64>(questGiverGuid);
			packet << io::write<uint32>(questId);
			packet << io::write<uint32>(rewardChoice);
			packet.Finish();
			});
	}

	void RealmConnector::SendMoveWorldPortAck()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::MoveWorldPortAck);
			packet.Finish();
			});
	}

	void RealmConnector::InviteByName(const String& playerName)
	{
		sendSinglePacket([playerName](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GroupInvite);
			packet << io::write_dynamic_range<uint8>(playerName);
			packet.Finish();
			});
	}

	void RealmConnector::UninviteByName(const String& playerName)
	{
		sendSinglePacket([playerName](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GroupUninvite);
			packet << io::write_dynamic_range<uint8>(playerName);
			packet.Finish();
			});
	}

	void RealmConnector::AcceptGroup()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GroupAccept);
			packet.Finish();
			});
	}

	void RealmConnector::DeclineGroup()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GroupDecline);
			packet.Finish();
			});
	}

	void RealmConnector::LeaveGroup()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GroupLeave);
			packet.Finish();
			});
	}

	void RealmConnector::DisbandGroup()
	{
		sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GroupDisband);
			packet.Finish();
			});
	}

	void RealmConnector::SendPartyPingPosition(const float x, const float y, const float z)
	{
		sendSinglePacket([x, y, z](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::PartyPing);
			packet << io::write<uint8>(0)  // type: position
				   << io::write<float>(x)
				   << io::write<float>(y)
				   << io::write<float>(z);
			packet.Finish();
		});
	}

	void RealmConnector::SendPartyPingUnit(const uint64 unitGuid)
	{
		sendSinglePacket([unitGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::PartyPing);
			packet << io::write<uint8>(1)  // type: unit
				   << io::write_packed_guid(unitGuid);
			packet.Finish();
		});
	}

	void RealmConnector::RandomRoll(int32 min, int32 max)
	{
		sendSinglePacket([min, max](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::RandomRoll);
			packet << io::write<int32>(std::min(min, max)) << io::write<int32>(std::max(min, max));
			packet.Finish();
			});
	}

	void RealmConnector::SendChatMessage(const String& message, ChatType chatType, const String& target)
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

	void RealmConnector::ExecuteGossipAction(uint64 npcGuid, uint32 gossipMenu, uint32 gossipIndex)
	{
		sendSinglePacket([npcGuid, gossipMenu, gossipIndex](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::GossipAction);
				packet
					<< io::write<uint64>(npcGuid)
					<< io::write<uint32>(gossipMenu)
					<< io::write<uint32>(gossipIndex);
				packet.Finish();
			});
	}

	void RealmConnector::CreateGuild(const String& guildName)
	{
		sendSinglePacket([&guildName](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::GuildCreate);
				packet
					<< io::write_dynamic_range<uint8>(guildName);
				packet.Finish();
			});
	}

	void RealmConnector::Logout()
	{
		sendSinglePacket([](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::LogoutRequest);
				packet.Finish();
			});
	}

	void RealmConnector::GuildRoster()
	{
		sendSinglePacket([](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::GuildRoster);
				packet.Finish();
			});
	}

	void RealmConnector::LearnTalent(uint32 talentId, uint8 rank)
	{
		sendSinglePacket([talentId, rank](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::LearnTalent);
				packet << io::write<uint32>(talentId)
					<< io::write<uint8>(rank);
				packet.Finish();
			});
	}

	void RealmConnector::SendTimePlayedRequest()
	{
		sendSinglePacket([](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::TimePlayedRequest);
				packet.Finish();
			});
	}

	void RealmConnector::SendTimeSyncResponse(uint32 syncIndex, GameTime clientTimestamp)
	{
		sendSinglePacket([syncIndex, clientTimestamp](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::TimeSyncResponse);
				packet << io::write<uint32>(syncIndex) << io::write<uint64>(clientTimestamp);
				packet.Finish();
			});
	}

	void RealmConnector::SendAreaTriggerTriggered(const uint32 triggerId)
	{
		sendSinglePacket([triggerId](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::AreaTriggerTriggered);
				packet << io::write<uint32>(triggerId);
				packet.Finish();
			});
	}

	void RealmConnector::SendAreaTriggerLeft(const uint32 triggerId)
	{
		sendSinglePacket([triggerId](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::AreaTriggerLeft);
				packet << io::write<uint32>(triggerId);
				packet.Finish();
			});
	}

	void RealmConnector::InitiateTrade(const uint64 targetGuid)
	{
		sendSinglePacket([targetGuid](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::TradeInitiate);
				packet << io::write<uint64>(targetGuid);
				packet.Finish();
			});
	}

	void RealmConnector::AcceptTradeInvite()
	{
		sendSinglePacket([](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::TradeInviteAccept);
				packet.Finish();
			});
	}

	void RealmConnector::DeclineTradeInvite()
	{
		sendSinglePacket([](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::TradeInviteDecline);
				packet.Finish();
			});
	}

	void RealmConnector::CancelTrade()
	{
		sendSinglePacket([](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::TradeCancelRequest);
				packet.Finish();
			});
	}

	void RealmConnector::TradeAddItem(const uint8 tradeSlot, const uint16 inventorySlot)
	{
		sendSinglePacket([tradeSlot, inventorySlot](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::TradeAddItem);
				packet << io::write<uint8>(tradeSlot);
				packet << io::write<uint16>(inventorySlot);
				packet.Finish();
			});
	}

	void RealmConnector::TradeRemoveItem(const uint8 tradeSlot)
	{
		sendSinglePacket([tradeSlot](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::TradeRemoveItem);
				packet << io::write<uint8>(tradeSlot);
				packet.Finish();
			});
	}

	void RealmConnector::TradeSetMoney(const uint32 amount)
	{
		sendSinglePacket([amount](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::TradeSetMoney);
				packet << io::write<uint32>(amount);
				packet.Finish();
			});
	}

	void RealmConnector::AcceptTrade()
	{
		sendSinglePacket([](game::OutgoingPacket& packet)
			{
				packet.Start(game::client_realm_packet::TradeAccept);
				packet.Finish();
			});
	}
}
