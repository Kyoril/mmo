// Copyright (C) 2021, Robin Klimonow. All rights reserved.

#include "player.h"
#include "player_manager.h"
#include "world_manager.h"
#include "world.h"
#include "login_connector.h"
#include "database.h"
#include "version.h"

#include "base/random.h"
#include "base/sha1.h"
#include "log/default_log_levels.h"
#include "math/vector3.h"
#include "math/degree.h"

#include <functional>


namespace mmo
{
	Player::Player(
		PlayerManager& playerManager,
		WorldManager& worldManager,
		LoginConnector &loginConnector,
		AsyncDatabase& database, 
		std::shared_ptr<Client> connection, 
		const String & address)
		: m_manager(playerManager)
		, m_worldManager(worldManager)
		, m_loginConnector(loginConnector)
		, m_database(database)
		, m_connection(std::move(connection))
		, m_address(address)
		, m_accountId(0)
	{
		// Generate random seed for packet header encryption & decryption
		std::uniform_int_distribution<uint32> dist;
		m_seed = dist(RandomGenerator);

		m_connection->setListener(*this);
	}

	void Player::Destroy()
	{
		m_connection->resetListener();
		m_connection.reset();

		m_manager.PlayerDisconnected(*this);
	}

	void Player::DoCharEnum()
	{
		// RequestHandler
		std::weak_ptr<Player> weakThis{ shared_from_this() };
		auto handler = [weakThis](std::optional<std::vector<CharacterView>> result) {
			if (auto strongThis = weakThis.lock())
			{
				// Lock around m_characterViews
				{
					std::scoped_lock<std::mutex> lock{ strongThis->m_charViewMutex };

					// Copy character view data
					strongThis->m_characterViews.clear();
					for (const auto& charView : result.value())
					{
						strongThis->m_characterViews[charView.GetGuid()] = charView;
					}
				}

				// Send result data to requesting client
				strongThis->GetConnection().sendSinglePacket([&result](game::OutgoingPacket& outPacket)
					{
						outPacket.Start(game::realm_client_packet::CharEnum);
						outPacket << io::write_dynamic_range<uint8>(*result);
						outPacket.Finish();
					});
			}
			else
			{
				WLOG("Could not send char list (client no longer available!)");
			}
		};

		// Testing...
		DLOG("Requesting char list for account " << m_accountId << "...");

		// Execute
		ASSERT(m_accountId != 0);
		m_database.asyncRequest(std::move(handler), &IDatabase::GetCharacterViewsByAccountId, m_accountId);
	}

	void Player::connectionLost()
	{
		ILOG("Client " << m_address << " disconnected");
		Destroy();
	}

	void Player::connectionMalformedPacket()
	{
		ILOG("Client " << m_address << " sent malformed packet");
		Destroy();
	}

	PacketParseResult Player::connectionPacketReceived(game::IncomingPacket & packet)
	{
		const auto packetId = packet.GetId();
		bool isValid = true;

		PacketHandler handler = nullptr;

		{
			// Lock packet handler access
			std::scoped_lock lock{ m_packetHandlerMutex };

			// Check for packet handlers
			auto handlerIt = m_packetHandlers.find(packetId);
			if (handlerIt == m_packetHandlers.end())
			{
				WLOG("Packet 0x" << std::hex << packetId << " is either unhandled or simply currently not handled");
				return PacketParseResult::Disconnect;
			}

			handler = handlerIt->second;
		}

		// Execute the packet handler and return the result
		return handler(packet);
	}

	PacketParseResult Player::OnAuthSession(game::IncomingPacket & packet)
	{
		ClearPacketHandler(game::client_realm_packet::AuthSession);

		// Read packet data
		if (!(packet
			>> io::read<uint32>(m_build)
			>> io::read_container<uint8>(m_accountName)
			>> io::read<uint32>(m_clientSeed)
			>> io::read_range(m_clientHash)))
		{
			ELOG("Could not read LogonChallenge packet from a game client");
			return PacketParseResult::Disconnect;
		}

		// Verify the client build immediatly for validity
		if (m_build != mmo::Revision)
		{
			ELOG("Unsupported client build " << m_build);
			return PacketParseResult::Disconnect;
		}

		// Setup a weak callback handler
		std::weak_ptr<Player> weakThis{ shared_from_this() };
		auto callbackHandler = [weakThis](bool succeeded, uint64 accountId, const BigNumber& sessionKey) {
			// Obtain strong reference to see if the client connection is still valid
			auto strongThis = weakThis.lock();
			if (strongThis)
			{
				// Handle success cases
				if (succeeded)
				{
					// Store session key
					strongThis->m_accountId = accountId;
					strongThis->InitializeSession(sessionKey);
				}
				else
				{
					// TODO: Send response to the game client
					DLOG("CLIENT_AUTH_SESSION: Error");
				}
			}
		};

		// Since we can't verify the client hash, as we don't have the client's session key just yet,
		// we need to ask the login server for verification.
		if (!m_loginConnector.QueueClientAuthSession(m_accountName, m_clientSeed, m_seed, m_clientHash, callbackHandler))
		{
			// Could not queue session, there is probably something wrong with the login server 
			// connection, so we close the client connection at this point
			return PacketParseResult::Disconnect;
		}

		// Everything has been done on our end, the client will be notified once the login server
		// has processed the request we just made
		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnCharEnum(game::IncomingPacket & packet)
	{
		DoCharEnum();

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnEnterWorld(game::IncomingPacket & packet)
	{
		EnableEnterWorldPacket(false);

		uint64 guid = 0;
		if (!(packet >> io::read<uint64>(guid)))
		{
			return PacketParseResult::Disconnect;
		}

		ILOG("Client wants to enter the world with character 0x" << std::hex << guid << "...");

		// Obtain the map id of the selected character
		uint32 mapId = 0;
		{
			std::scoped_lock<std::mutex> lock{ m_charViewMutex };
			
			// Ensure that the character exists and belongs to our account
			const auto charViewIt = m_characterViews.find(guid);
			if (charViewIt == m_characterViews.end())
			{
				WLOG("Client tried to log in with a character that doesn't exist or doesn't belong to the account of that player!");
				return PacketParseResult::Disconnect;
			}
			
			// Cache the characters current map id
			mapId = charViewIt->second.GetMapId();
		}
		
		// Find a world node for the character's map id
		m_world = m_worldManager.GetWorldByMapId(mapId);

		// Check if world instance exists
		const auto strongWorld = m_world.lock();
		if (!strongWorld)
		{
			WLOG("No world node available which is able to host map " << mapId);
			EnableEnterWorldPacket(true);
			return PacketParseResult::Pass;
		}

		// Create a new map instance for the current map id of the character
		strongWorld->RequestMapInstanceCreation(mapId);
		
		// TODO: Send character data to the given world node so that the character can spawn there

		// TODO: From here on, act as proxy for the world node

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnCreateChar(game::IncomingPacket& packet)
	{
		std::string characterName;
		if (!(packet >> io::read_container<uint8>(characterName)))
		{
			return PacketParseResult::Disconnect;
		}
		
		std::weak_ptr<Player> weakThis{ shared_from_this() };
		auto handler = [weakThis](bool success) {
			if (auto strongThis = weakThis.lock())
			{
				if (success)
				{
					strongThis->DoCharEnum();
				}
				else
				{
					ELOG("Failed to create character!");
				}
			}
		};

		// TODO: Load real start values from static game data instead of hard code it here. However,
		// the infrastructure isn't read yet.
		const uint32 level = 1;
		const uint32 race = 1;
		const uint32 map = 0;
		const uint32 hp = 1;
		const uint32 gender = 0;
		const Vector3 position;
		const Degree rotation;
		
		DLOG("Creating new character named '" << characterName << "' for account 0x" << std::hex << m_accountId << "...");
		m_database.asyncRequest<void>([characterName, level, race, map, hp, gender, &position, &rotation, this](auto&& database)
		{
			database->CreateCharacter(characterName, this->m_accountId, map, level, hp, race, gender, position, rotation);
		}, std::move(handler));
		
		return PacketParseResult::Pass;
	}

	PacketParseResult Player::OnDeleteChar(game::IncomingPacket& packet)
	{
		ASSERT(IsAuthentificated());
		
		uint64 charGuid = 0;
		if (!(packet >> io::read<uint64>(charGuid)))
		{
			return PacketParseResult::Disconnect;
		}

		// Lock around m_characterViews
		std::scoped_lock<std::mutex> lock{ m_charViewMutex };
		
		// Check if such a character exists
		const auto charIt = m_characterViews.find(charGuid);
		if (charIt == m_characterViews.end())
		{
			WLOG("Tried to delete character 0x" << std::hex << charGuid << " which doesn't exist or belong to the players account!");
			return PacketParseResult::Disconnect;
		}

		// Database callback handler
		std::weak_ptr<Player> weakThis{ shared_from_this() };
		auto handler = [weakThis](bool success) {
			if (auto strongThis = weakThis.lock())
			{
				if (success)
				{
					strongThis->DoCharEnum();
				}
				else
				{
					ELOG("Failed to delete character!");
				}
			}
		};
		
		DLOG("Deleting character 0x" << std::hex << charGuid << " from account 0x" << std::hex << m_accountId << "...");
		m_database.asyncRequest<void>([charGuid](auto&& database) { database->DeleteCharacter(charGuid); }, std::move(handler));

		return PacketParseResult::Pass;
	}

	void Player::SendAuthChallenge()
	{
		// We will start accepting LogonChallenge packets from the client
		RegisterPacketHandler(game::client_realm_packet::AuthSession, *this, &Player::OnAuthSession);

		// Send the LogonChallenge packet to the client including our generated seed
		m_connection->sendSinglePacket([this](game::OutgoingPacket& packet) {
			packet.Start(game::realm_client_packet::AuthChallenge);
			packet << io::write<uint32>(m_seed);
			packet.Finish();
		});
	}

	void Player::InitializeSession(const BigNumber & sessionKey)
	{
		m_sessionKey = sessionKey;

		// Notify about success
		DLOG("CLIENT_AUTH_SESSION: Success!");

		// Initialize encryption
		HMACHash hash;
		m_connection->GetCrypt().GenerateKey(hash, m_sessionKey);
		m_connection->GetCrypt().SetKey(hash.data(), hash.size());
		m_connection->GetCrypt().Init();

		// Send the response to the client
		m_connection->sendSinglePacket([](game::OutgoingPacket& packet) {
			packet.Start(game::realm_client_packet::AuthSessionResponse);
			packet << io::write<uint8>(game::auth_result::Success);	// TODO: Write real packet content
			packet.Finish();
		});

		// Enable CharEnum packets
		RegisterPacketHandler(game::client_realm_packet::CharEnum, *this, &Player::OnCharEnum);
		RegisterPacketHandler(game::client_realm_packet::CreateChar, *this, &Player::OnCreateChar);
		RegisterPacketHandler(game::client_realm_packet::DeleteChar, *this, &Player::OnDeleteChar);
		RegisterPacketHandler(game::client_realm_packet::EnterWorld, *this, &Player::OnEnterWorld);
	}

	void Player::EnableEnterWorldPacket(bool enable)
	{
		if (enable)
		{
			RegisterPacketHandler(game::client_realm_packet::EnterWorld, *this, &Player::OnEnterWorld);
		}
		else
		{
			ClearPacketHandler(game::client_realm_packet::EnterWorld);
		}
	}

	void Player::RegisterPacketHandler(uint16 opCode, PacketHandler && handler)
	{
		std::scoped_lock lock{ m_packetHandlerMutex };

		auto it = m_packetHandlers.find(opCode);
		if (it == m_packetHandlers.end())
		{
			m_packetHandlers.emplace(std::make_pair(opCode, std::forward<PacketHandler>(handler)));
		}
		else
		{
			it->second = std::forward<PacketHandler>(handler);
		}
	}

	void Player::ClearPacketHandler(uint16 opCode)
	{
		std::scoped_lock lock{ m_packetHandlerMutex };

		auto it = m_packetHandlers.find(opCode);
		if (it != m_packetHandlers.end())
		{
			m_packetHandlers.erase(it);
		}
	}
}
