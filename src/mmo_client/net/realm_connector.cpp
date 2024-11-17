// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "realm_connector.h"
#include "version.h"
#include "game/movement_info.h"

#include "base/constants.h"
#include "base/random.h"
#include "base/sha1.h"
#include "game/spell_target_map.h"
#include "game_states/login_state.h"
#include "log/default_log_levels.h"


namespace mmo
{
	RealmConnector::RealmConnector(asio::io_service & io)
		: game::Connector(std::make_unique<asio::ip::tcp::socket>(io), nullptr)
		, m_ioService(io)
		, m_realmPort(0)
		, m_serverSeed(0)
		, m_clientSeed(0)
		, m_realmId(0)
	{
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
			packet.Start(game::client_realm_packet::AuthSession);
			packet
				<< io::write<uint32>(mmo::Revision)
				<< io::write_dynamic_range<uint8>(this->m_account)
				<< io::write<uint32>(m_clientSeed)
				<< io::write_range(hash);
			packet.Finish();
		});

		// Initialize connection encryption afterwards
		HMACHash cryptKey;
		GetCrypt().GenerateKey(cryptKey, m_sessionKey);
		GetCrypt().SetKey(cryptKey.data(), cryptKey.size());
		GetCrypt().Init();

		// Debug log
		ILOG("[Realm] Handshaking...");

		// Proceed handling network packets
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

		// Authentication has been successful!
		AuthenticationResult(result);

		// Was this a success?
		if (result == game::auth_result::Success)
		{
			// From here on, we accept CharEnum packets
			RegisterPacketHandler(game::realm_client_packet::CharEnum, *this, &RealmConnector::OnCharEnum);
			RegisterPacketHandler(game::realm_client_packet::LoginVerifyWorld, *this, &RealmConnector::OnLoginVerifyWorld);
			RegisterPacketHandler(game::realm_client_packet::EnterWorldFailed, *this, &RealmConnector::OnEnterWorldFailed);
			
			// And now, we ask for the character list
			sendSinglePacket([](game::OutgoingPacket& outPacket)
			{
				outPacket.Start(game::client_realm_packet::CharEnum);
				// This packet is empty
				outPacket.Finish();
			});
		}

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
	
	bool RealmConnector::connectionEstablished(bool success)
	{
		if (success)
		{
			// Reset server seed
			m_serverSeed = 0;

			// Generate a new client seed
			std::uniform_int_distribution<uint32> dist;
			m_clientSeed = dist(RandomGenerator);

			// Accept LogonChallenge packets from here on
			RegisterPacketHandler(game::realm_client_packet::AuthChallenge, *this, &RealmConnector::OnAuthChallenge);
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

		// Connect to the server
		connect(m_realmAddress, m_realmPort, *this, m_ioService);
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

	void RealmConnector::CreateCharacter(const std::string& name, uint8 race, uint8 characterClass, uint8 characterGender)
	{
		sendSinglePacket([&name, race, characterClass, characterGender](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CreateChar);
			packet
				<< io::write_dynamic_range<uint8>(name)
				<< io::write<uint8>(race)
				<< io::write<uint8>(characterClass)
				<< io::write<uint8>(characterGender);
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

	void RealmConnector::LevelUp(uint8 level)
	{
		sendSinglePacket([level](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::CheatLevelUp);
			packet << io::write<uint8>(level);
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

	void RealmConnector::AutoStoreLootItem(uint8 lootSlot)
	{
		sendSinglePacket([lootSlot](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::AutoStoreLootItem);
			packet
				<< io::write<uint8>(lootSlot);
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

	void RealmConnector::AutoStoreBagItem(uint8 srcBag, uint8 srcSlot, uint8 dstBag)
	{
		sendSinglePacket([srcBag, srcSlot, dstBag](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::AutoStoreBagItem);
			packet
				<< io::write<uint8>(srcBag)
				<< io::write<uint8>(srcSlot)
				<< io::write<uint8>(dstBag);
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

	void RealmConnector::AutoEquipItemSlot(uint64 itemGUID, uint8 dstSlot)
	{
		sendSinglePacket([itemGUID, dstSlot](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::AutoEquipItemSlot);
			packet
				<< io::write_packed_guid(itemGUID)
				<< io::write<uint8>(dstSlot);
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

	void RealmConnector::GossipHello(uint64 targetGuid)
	{
		sendSinglePacket([targetGuid](game::OutgoingPacket& packet) {
			packet.Start(game::client_realm_packet::GossipHello);
			packet
				<< io::write<uint64>(targetGuid);
			packet.Finish();
			});
	}
}
