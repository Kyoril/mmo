// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "player.h"
#include "database.h"
#include "realm_manager.h"
#include "player_manager.h"
#include "realm.h"

#include "base/constants.h"
#include "base/sha1.h"
#include "base/weak_ptr_function.h"
#include "log/default_log_levels.h"

#include <functional>

namespace mmo
{
	Player::Player(
		PlayerManager& playerManager,
		RealmManager& realmManager,
		AsyncDatabase& database, 
		std::shared_ptr<Client> connection, 
		const String & address)
		: m_manager(playerManager)
		, m_realmManager(realmManager)
		, m_database(database)
		, m_connection(std::move(connection))
		, m_address(address)
	{
		m_connection->setListener(*this);

		// Listen for connect packets
		RegisterPacketHandler(auth::client_login_packet::LogonChallenge, *this, &Player::HandleLogonChallenge);
		RegisterPacketHandler(auth::client_login_packet::ReconnectChallenge, *this, &Player::HandleLogonChallenge);
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
			const auto handlerIt = m_packetHandlers.find(packetId);
			if (handlerIt == m_packetHandlers.end())
			{
				WLOG("Packet 0x" << std::hex << static_cast<uint16>(packetId) << " is either unhandled or simply currently not handled");
				return PacketParseResult::Disconnect;
			}

			handler = handlerIt->second;
		}

		// Execute the packet handler and return the result
		return handler(packet);
	}

	void Player::SendAuthProof(auth::AuthResult result)
	{
		// Send proof packet to the client
		m_connection->sendSinglePacket([result, this](auth::OutgoingPacket& packet) {
			packet.Start(auth::login_client_packet::LogonProof);
			packet << io::write<uint8>(result);

			// If the login attempt succeeded, we also send the calculated M2 hash value to the
			// client, which will compare it against it's own calculated M2 hash just in case.
			if (result == auth::auth_result::Success)
			{
				// Send server-calculated M2 hash value so that the client can verify this as well
				packet << io::write_range(this->m_m2.begin(), this->m_m2.end());
			}

			packet.Finish();
		});
	}

	void Player::SendRealmList()
	{
		m_connection->sendSinglePacket([this](auth::OutgoingPacket& packet) {
			packet.Start(mmo::auth::login_client_packet::RealmList);

			// Remember realm count position and write placeholder value
			const size_t realmCountPos = packet.Sink().Position();
			packet << io::write<uint16>(0);

			// The realm counter
			uint16 realmCount = 0;

			// Iterate through every realm and write it's data to the outgoing packet
			m_realmManager.ForEachRealm([&realmCount, &packet](const Realm& realm) {
				// Skip this realm if it is not authenticated
				if (!realm.IsAuthentificated())
				{
					return;
				}

				// TODO: Probably check if the realm is otherwise not eligible? Account level / security groups / supported client versions?

				// Write realm data
				packet 
					<< io::write<uint32>(realm.GetRealmId())
					<< io::write_dynamic_range<uint8>(realm.GetRealmName())
					<< io::write_dynamic_range<uint8>(realm.GetRealmListAddress())
					<< io::write<uint16>(realm.GetRealmListPort())
					;

				// Increase counter
				realmCount++;
			});

			// Now overwrite realm count with the actual realm count
			packet.Sink().Overwrite(realmCountPos, reinterpret_cast<const char*>(&realmCount), sizeof(realmCount));

			// Finish the packet
			packet.Finish();
		});
	}

	void Player::RegisterPacketHandler(uint8 opCode, PacketHandler && handler)
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

	void Player::ClearPacketHandler(const uint8 opCode)
	{
		std::scoped_lock lock{ m_packetHandlerMutex };

		auto it = m_packetHandlers.find(opCode);
		if (it != m_packetHandlers.end())
		{
			m_packetHandlers.erase(it);
		}
	}

	PacketParseResult Player::HandleLogonChallenge(auth::IncomingPacket & packet)
	{
		// No longer handle these packets!
		ClearPacketHandler(auth::client_login_packet::LogonChallenge);
		ClearPacketHandler(auth::client_login_packet::ReconnectChallenge);

		// Read the packet data
		if (!(packet
			>> io::read<uint8>(m_version1)
			>> io::read<uint8>(m_version2)
			>> io::read<uint8>(m_version3)
			>> io::read<uint16>(m_build)
			>> m_locale
			>> io::read_container<uint8>(m_accountName)))
		{
			return PacketParseResult::Disconnect;
		}

		// Write the login attempt to the logs
		ILOG("Received logon challenge for account " << m_accountName << "...");
		
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
					const BigNumber gmod = constants::srp::g.modExp(strongThis->m_b, constants::srp::N);
					strongThis->m_B = ((strongThis->m_v * 3) + gmod) % constants::srp::N;

					assert(gmod.getNumBytes() <= 32);
					strongThis->m_unk3.setRand(16 * 8);

					// Allow handling the logon proof packet now
					strongThis->RegisterPacketHandler(auth::client_login_packet::LogonProof, *strongThis.get(), &Player::HandleLogonProof);
				}
				else
				{
					// Invalid account name display
					WLOG("Invalid account name " << strongThis->getAccountName());
				}

				// Send packet with result
				strongThis->m_connection->sendSinglePacket([authResult, &strongThis](auth::OutgoingPacket& packet) {
					packet.Start(auth::login_client_packet::LogonChallenge);
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
		m_database.asyncRequest(std::move(handler), &IDatabase::getAccountDataByName, std::cref(m_accountName));
		return PacketParseResult::Pass;
	}

	PacketParseResult Player::HandleLogonProof(auth::IncomingPacket & packet)
	{
		// No longer handle proof packet
		ClearPacketHandler(auth::client_login_packet::LogonProof);

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
		const auto t4 = sha1(reinterpret_cast<const char*>(m_accountName.data()), m_accountName.size());
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
			// Finish SRP6 by calculating the M2 hash value that is sent back to the client for
			// verification as well.
			m_m2 = Sha1_BigNumbers({ A, M1, K});

			// Store the calculated session key value internally for later use, also store it in the 
			// database maybe.
			m_sessionKey = K;

			// Handler method
			std::weak_ptr<Player> weakThis{ shared_from_this() };
			auto handler = [weakThis](const bool success)
			{
				if (const auto strongThis = weakThis.lock())
				{
					if (success)
					{
						// Add log entry about successful login as the hashes do indeed mach (and thus, so
						// do the passwords)
						ILOG("User " << strongThis->m_accountName << " successfully authenticated");

						// If the login attempt succeeded, then we will accept RealmList request packets from now
						// on to send the realm list to the client on manual request
						strongThis->RegisterPacketHandler(auth::client_login_packet::RealmList, *strongThis.get(), &Player::OnRealmList);
						strongThis->SendAuthProof(auth::AuthResult::Success);

						// Send the realm list as well
						strongThis->SendRealmList();
					}
					else
					{
						strongThis->SendAuthProof(auth::AuthResult::FailDbBusy);
					}
				}
			};

			// Store session key in account database
			m_database.asyncRequest<void>(
				std::bind(&IDatabase::playerLogin, std::placeholders::_1, m_accountId, K.asHexStr(), m_address),
				std::move(handler));

			// Stop here since we wait for the database callback
			return PacketParseResult::Pass;
		}

		// Log error
		WLOG("Invalid password for account " << m_accountName);
		
		std::weak_ptr<Player> weakThis{ shared_from_this() };
		auto loginFailedDbHandler = [weakThis, proofResult](const bool)
			{
				if (const auto strongThis = weakThis.lock())
				{
					strongThis->SendAuthProof(proofResult);
				}
			};

		// Store session key in account database
		m_database.asyncRequest<void>(
			[this, address = std::cref(m_address)](auto&& database) { database->playerLoginFailed(m_accountId, address); },
			std::move(loginFailedDbHandler));

		return PacketParseResult::Pass;
	}

	PacketParseResult Player::HandleReconnectChallenge(auth::IncomingPacket & packet)
	{
		// No longer handle proof packet
		ClearPacketHandler(auth::client_login_packet::LogonChallenge);
		ClearPacketHandler(auth::client_login_packet::ReconnectChallenge);

		// TODO: Handle this packet properly

		// Handle reconnect proof packet now
		RegisterPacketHandler(auth::client_login_packet::ReconnectProof, *this, &Player::HandleLogonProof);

		return PacketParseResult::Disconnect;
	}

	PacketParseResult Player::HandleReconnectProof(auth::IncomingPacket & packet)
	{
		// TODO: Handle this packet properly

		// Handle realm list packet now
		RegisterPacketHandler(auth::client_login_packet::RealmList, *this, &Player::OnRealmList);

		return PacketParseResult::Disconnect;
	}

	PacketParseResult Player::OnRealmList(auth::IncomingPacket & packet)
	{
		// TODO: apply a rate limiter so the client cannot spam this packet (DDOS attack)

		// Send the realm list with all currently connected realms in there
		SendRealmList();

		return PacketParseResult::Pass;
	}
}
