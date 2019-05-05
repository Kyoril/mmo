
#include "player.h"
#include "database.h"

#include "base/constants.h"
#include "base/sha1.h"
#include "base/clock.h"
#include "base/weak_ptr_function.h"
#include "log/default_log_levels.h"

#include <iomanip>
#include <functional>

namespace mmo
{
	Player::Player(
		PlayerManager& playerManager, 
		AsyncDatabase& database, 
		std::shared_ptr<Client> connection, 
		const String & address)
		: m_manager(playerManager)
		, m_database(database)
		, m_connection(std::move(connection))
		, m_address(address)
	{
		m_connection->setListener(*this);

		// Listen for connect packets
		registerPacketHandler(auth::client_packet::LogonChallenge, std::bind(&Player::handleLogonChallenge, this, std::placeholders::_1));
		registerPacketHandler(auth::client_packet::ReconnectChallenge, std::bind(&Player::handleLogonChallenge, this, std::placeholders::_1));
	}

	void Player::destroy()
	{
		m_connection->resetListener();
		m_connection.reset();

		m_manager.playerDisconnected(*this);
	}

	void Player::connectionLost()
	{
		ILOG("Client " << m_address << " disconnected");
		destroy();
	}

	void Player::connectionMalformedPacket()
	{
		ILOG("Client " << m_address << " sent malformed packet");
		destroy();
	}

	PacketParseResult Player::connectionPacketReceived(auth::IncomingPacket & packet)
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
				WLOG("Packet 0x" << std::hex << (uint16)packetId << " is either unhandled or simply currently not handled");
				return PacketParseResult::Disconnect;
			}

			handler = handlerIt->second;
		}

		// Execute the packet handler and return the result
		return handler(packet);
	}

	void Player::registerPacketHandler(uint8 opCode, PacketHandler && handler)
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

	void Player::clearPacketHandler(uint8 opCode)
	{
		std::scoped_lock lock{ m_packetHandlerMutex };

		auto it = m_packetHandlers.find(opCode);
		if (it != m_packetHandlers.end())
		{
			m_packetHandlers.erase(it);
		}
	}

	PacketParseResult Player::handleLogonChallenge(auth::IncomingPacket & packet)
	{
		// No longer handle these packets!
		clearPacketHandler(auth::client_packet::LogonChallenge);
		clearPacketHandler(auth::client_packet::ReconnectChallenge);

		// Read the packet data
		uint16 contentSize = 0;
		uint32 timezone = 0, ip = 0;
		if (!(packet
			>> io::read<uint16>(contentSize)
			>> io::read<uint8>(m_version1)
			>> io::read<uint8>(m_version2)
			>> io::read<uint8>(m_version3)
			>> io::read<uint16>(m_build)
			>> m_platform
			>> m_system
			>> m_locale
			>> io::read<uint32>(timezone)
			>> io::read<uint32>(ip)
			>> io::read_container<uint8>(m_userName)))
		{
			return PacketParseResult::Disconnect;
		}

		// Verify packet size
		if (contentSize != packet.getSource()->size() - 2)
		{
			return PacketParseResult::Disconnect;
		}

		// Write the login attempt to the logs
		ILOG("Received logon challenge for account " << m_userName << "...");
		
		// RequestHandler
		std::weak_ptr<Player> weakThis{ shared_from_this() };
		auto handler = [weakThis](std::optional<AccountData> result) {
			if (auto strongThis = weakThis.lock())
			{
				// The temporary result
				auth::AuthResult authResult = auth::auth_result::FailWrongCredentials;
				if (result)
				{
					// Generate s and v bignumber values to calculate with
					strongThis->m_s.setHexStr(result->s);
					strongThis->m_v.setHexStr(result->v);

					// Store account id
					strongThis->m_accountId = result->id;

					// We are NOT banned so continue
					authResult = auth::auth_result::Success;

					strongThis->m_b.setRand(19 * 8);
					BigNumber gmod = constants::srp::g.modExp(strongThis->m_b, constants::srp::N);
					strongThis->m_B = ((strongThis->m_v * 3) + gmod) % constants::srp::N;

					assert(gmod.getNumBytes() <= 32);
					strongThis->m_unk3.setRand(16 * 8);

					// Allow handling the logon proof packet now
					strongThis->registerPacketHandler(auth::client_packet::LogonProof, std::bind(&Player::handleLogonProof, strongThis.get(), std::placeholders::_1));
				}
				else
				{
					// Invalid account name display
					WLOG("Invalid account name " << strongThis->getAccountName());
				}

				// Send packet with result
				strongThis->m_connection->sendSinglePacket([authResult, &strongThis](auth::OutgoingPacket& packet) {
					packet.Start(auth::server_packet::LogonChallenge);
					packet << io::write<uint8>(authResult);

					// On success, there are more data values to write
					if (authResult == auth::auth_result::Success)
					{
						// Write B with 32 byte length and g
						std::vector<uint8> B_ = strongThis->m_B.asByteArray(32);
						packet
							<< io::write_range(B_.begin(), B_.end())
							<< io::write<uint8>(constants::srp::g.asUInt32());

						// Write N with 32 byte length
						const std::vector<uint8> N_ = constants::srp::N.asByteArray(32);
						packet << io::write_range(N_.begin(), N_.end());

						// Write s
						const std::vector<uint8> s_ = strongThis->m_s.asByteArray();
						packet << io::write_range(s_.begin(), s_.end());
					}

					packet.Finish();
				});
			}
		};

		// Execute
		m_database.asyncRequest(std::move(handler), &IDatabase::getAccountDataByName, std::cref(m_userName));
		return PacketParseResult::Pass;
	}

	PacketParseResult Player::handleLogonProof(auth::IncomingPacket & packet)
	{
		// No longer handle proof packet
		clearPacketHandler(auth::client_packet::LogonProof);

		// Read packet data
		std::array<uint8, 32> rec_A;
		std::array<uint8, 20> rec_M1;
		if (!(packet
			>> io::read_range(rec_A.begin(), rec_A.end())
			>> io::read_range(rec_M1.begin(), rec_M1.end())
			))
		{
			return PacketParseResult::Disconnect;
		}

		// Continue the SRP6 calculation based on data received from the client
		BigNumber A{ rec_A.data(), rec_A.size() };

		// SRP safeguard: abort if A % N == 0
		if ((A % constants::srp::N).isZero())
		{
			ELOG("[Logon Proof] SRP safeguard failed");
			return PacketParseResult::Disconnect;
		}
		
		// Build hash
		SHA1Hash hash = Sha1_BigNumbers({ A, m_B });

		// Calculate u and S
		BigNumber u{ hash.data(), hash.size() };
		BigNumber S = (A * (m_v.modExp(u, constants::srp::N))).modExp(m_b, constants::srp::N);

		// Build t
		const std::vector<uint8> t = S.asByteArray(32);
		std::array<uint8, 16> t1;
		for (size_t i = 0; i < t1.size(); ++i)
		{
			t1[i] = t[i * 2];
		}
		hash = sha1(reinterpret_cast<const char*>(t1.data()), t1.size());

		std::array<uint8, 40> vK;
		for (size_t i = 0; i < 20; ++i)
		{
			vK[i * 2] = hash[i];
		}
		for (size_t i = 0; i < 16; ++i)
		{
			t1[i] = t[i * 2 + 1];
		}

		hash = sha1(reinterpret_cast<const char*>(t1.data()), t1.size());
		for (size_t i = 0; i < 20; ++i)
		{
			vK[i * 2 + 1] = hash[i];
		}

		BigNumber K{ vK.data(), vK.size() };

		SHA1Hash h;
		h = Sha1_BigNumbers({ constants::srp::N });
		hash = Sha1_BigNumbers({ constants::srp::g });
		for (size_t i = 0; i < h.size(); ++i)
		{
			h[i] ^= hash[i];
		}

		BigNumber t3{ h.data(), h.size() };

		HashGeneratorSha1 sha;
		Sha1_Add_BigNumbers(sha, { t3 });
		const auto t4 = sha1(reinterpret_cast<const char*>(m_userName.data()), m_userName.size());
		sha.update(reinterpret_cast<const char*>(t4.data()), t4.size());
		Sha1_Add_BigNumbers(sha, { m_s, A, m_B, K });
		hash = sha.finalize();

		// Proof result which will be sent to the client
		auth::AuthResult proofResult = auth::auth_result::FailWrongCredentials;

		// Calculate M1 hash on server using values sent by the client and compare it against
		// the M1 hash sent by the client to see if the passwords do match.
		const BigNumber M1{ hash.data(), hash.size() };
		auto sArr = M1.asByteArray(20);
		if (std::equal(sArr.begin(), sArr.end(), rec_M1.begin()))
		{
			// Addd log entry about successful login as the hashes do indeed mach (and thus, so
			// do the passwords)
			ILOG("User " << m_userName << " successfully authenticated");

			// Finish SRP6 by calculating the M2 hash value that is sent back to the client for
			// verification as well.
			hash = Sha1_BigNumbers({ A, M1, K});

			// Store the caluclated session key value internally for later use, also store it in the 
			// database maybe.
			m_sessionKey = K;

			// Store session key in account database
			m_database.asyncRequest<void>(
				std::bind(&IDatabase::setAccountSessionKey, std::placeholders::_1, 
					m_accountId, 
					K.asHexStr()), 
				dbNullHandler);

			// Create session
			//m_session = m_createSession(K, m_accountId, m_userName, m_v, m_s);
			//m_database.setKey(m_accountId, K);

			// Send proof
			proofResult = auth::auth_result::Success;
		}
		else
		{
			// Log error
			WLOG("Invalid password for account " << m_userName);
		}

		// If the login attempt succeeded, then we will accept RealmList request packets from now
		// on to send the realm list to the client on manual request
		if (proofResult == auth::auth_result::Success)
		{
			registerPacketHandler(auth::client_packet::RealmList, std::bind(&Player::handleRealmList, this, std::placeholders::_1));
		}

		// Send proof packet to the client
		m_connection->sendSinglePacket([proofResult, &hash](auth::OutgoingPacket& packet) {
			packet.Start(auth::server_packet::LogonProof);
			packet << io::write<uint8>(proofResult);

			// If the login attempt succeeded, we also send the calculated M2 hash value to the
			// client, which will compare it against it's own calculated M2 hash just in case.
			if (proofResult == auth::auth_result::Success)
			{
				// Send server-calculated M2 hash value so that the client can verify this as well
				packet << io::write_range(hash.begin(), hash.end());
			}

			packet.Finish();
		});

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::handleReconnectChallenge(auth::IncomingPacket & packet)
	{
		// No longer handle proof packet
		clearPacketHandler(auth::client_packet::LogonChallenge);
		clearPacketHandler(auth::client_packet::ReconnectChallenge);

		// Handle reconnect proof packet now
		registerPacketHandler(auth::client_packet::ReconnectProof, std::bind(&Player::handleLogonProof, this, std::placeholders::_1));

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::handleReconnectProof(auth::IncomingPacket & packet)
	{
		// Handle realm list packet now
		registerPacketHandler(auth::client_packet::RealmList, std::bind(&Player::handleRealmList, this, std::placeholders::_1));

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::handleRealmList(auth::IncomingPacket & packet)
	{
		// TODO: Use a cache to not load realms from the database too frequently

		// Query all realms from the database
#if 0
		std::weak_ptr<Player> weakThis{ shared_from_this() };
		auto handler = [weakThis](std::vector<binary::realm_login::RealmData> realms) {
			if (auto strongThis = weakThis.lock())
			{
				// Send the realm list packet
				strongThis->m_connection->sendSinglePacket([strongThis, &realms](auth::OutgoingPacket &packet) {
					packet.Start(auth::server_packet::RealmList);
					const auto sizePos = packet.sink().position();
					packet << io::write<uint16>(0);	// size placeholder

					const auto contentPos = packet.sink().position();
					packet
						<< io::write<uint32>(0)
						<< io::write<uint8>(realms.size());

					for (const auto& realm : realms)
					{
						// Check if there is such a realm by id
						const bool realmIsOnline = (strongThis->m_realmManager.getRealmById(realm.id) != nullptr);

						// Build realm flags
						uint16 realmFlags = 0;
						if (!realmIsOnline) realmFlags |= 2;

						const std::string realmAddr = realm.address + ":" + std::to_string(realm.port);
						packet
							<< io::write<uint32>(0)				// type
							<< io::write<uint8>(realmFlags)		// flags
							<< io::write_range(realm.name) << io::write<uint8>(0)
							<< io::write_range(realmAddr) << io::write<uint8>(0)
							<< io::write<float>(0.0f)			// population
							<< io::write<uint8>(0)				// amount of chars
							<< io::write<uint8>(1)				// time zone (language)
							<< io::write<uint8>(0);
					}

					packet << io::write<uint16>(2);

					// Overwrite packet size field
					const uint16 packetSize = static_cast<uint16>(packet.sink().position() - contentPos);
					packet.sink().overwrite(sizePos, reinterpret_cast<const char*>(&packetSize), sizeof(uint16));

					packet.Finish();
				});
			}
		};
		
		m_database.asyncRequest<std::vector<binary::realm_login::RealmData>>(&IDatabase::getRealms, std::move(handler));
#endif

		return PacketParseResult::Pass;
	}
}
