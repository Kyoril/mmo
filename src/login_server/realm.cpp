// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "realm.h"
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
	Realm::Realm(
		RealmManager& realmManager, 
		AsyncDatabase& database, 
		std::shared_ptr<Client> connection, 
		const String & address)
		: m_manager(realmManager)
		, m_database(database)
		, m_connection(std::move(connection))
		, m_address(address)
		, m_authenticated(false)
	{
		// Reminder: constructor might be called by multiple threads

		m_connection->setListener(*this);

		// Listen for connect packets
		RegisterPacketHandler(auth::realm_login_packet::LogonChallenge, *this, &Realm::HandleLogonChallenge);
	}

	void Realm::Destroy()
	{
		m_authenticated = false;

		m_connection->resetListener();
		m_connection.reset();

		m_manager.RealmDisconnected(*this);
	}

	void Realm::connectionLost()
	{
		ILOG("Realm server " << m_address << " disconnected");
		Destroy();
	}

	void Realm::connectionMalformedPacket()
	{
		ILOG("Realm server " << m_address << " sent malformed packet");
		Destroy();
	}

	PacketParseResult Realm::connectionPacketReceived(auth::IncomingPacket & packet)
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

	void Realm::SendAuthProof(auth::AuthResult result)
	{
		// Send proof packet to the client
		m_connection->sendSinglePacket([result, this](auth::OutgoingPacket& packet) {
			packet.Start(auth::login_realm_packet::LogonProof);
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

	void Realm::SendAuthSessionResult(uint64 requestId, auth::AuthResult result, uint64 accountId, BigNumber sessionKey)
	{
		if (result == auth::auth_result::Success)
		{
			// Hash matched, client has a valid session key and thus is able to sign in on the realm
			ILOG("Client successfully signed in on realm " << m_realmName << "...");
		}
		else
		{
			// Warn about this, as this might mean that someone is trying to log in on a realm without 
			// having a valid session key.
			WLOG("Auth session hash mismatch, client could not sign in on realm " << m_realmName << "!");
		}

		// Send response packet to the realm server
		m_connection->sendSinglePacket([requestId, result, accountId, &sessionKey](auth::OutgoingPacket& packet) {
			packet.Start(auth::login_realm_packet::ClientAuthSessionResponse);
			packet
				<< io::write<uint64>(requestId)
				<< io::write<uint8>(result);

			if (result == auth::auth_result::Success)
			{
				packet
					<< io::write<uint64>(accountId)
					<< io::write_dynamic_range<uint16>(sessionKey.asByteArray());
			}

			packet.Finish();
		});
	}

	void Realm::RegisterPacketHandler(uint8 opCode, PacketHandler && handler)
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

	void Realm::ClearPacketHandler(uint8 opCode)
	{
		std::scoped_lock lock{ m_packetHandlerMutex };

		auto it = m_packetHandlers.find(opCode);
		if (it != m_packetHandlers.end())
		{
			m_packetHandlers.erase(it);
		}
	}

	PacketParseResult Realm::HandleLogonChallenge(auth::IncomingPacket & packet)
	{
		// No longer handle these packets!
		ClearPacketHandler(auth::realm_login_packet::LogonChallenge);

		// Read the packet data
		if (!(packet
			>> io::read<uint8>(m_version1)
			>> io::read<uint8>(m_version2)
			>> io::read<uint8>(m_version3)
			>> io::read<uint16>(m_build)
			>> io::read_container<uint8>(m_realmName)))
		{
			return PacketParseResult::Disconnect;
		}

		// Write the login attempt to the logs
		ILOG("Received logon challenge for realm " << m_realmName << "...");
		
		// RequestHandler
		std::weak_ptr<Realm> weakThis{ shared_from_this() };
		auto handler = [weakThis](std::optional<RealmAuthData> result) {
			if (auto strongThis = weakThis.lock())
			{
				// The temporary result
				auth::AuthResult authResult = auth::auth_result::FailWrongCredentials;
				if (result)
				{
					// Generate s and v bignumber values to calculate with
					strongThis->m_s.setHexStr(result->s);
					strongThis->m_v.setHexStr(result->v);

					// Store realm infos
					strongThis->m_realmId = result->id;
					strongThis->m_realmName = result->name;
					strongThis->m_realmListAddress = result->ipAddress;
					strongThis->m_realmListPort = result->port;

					// We are NOT banned so continue
					authResult = auth::auth_result::Success;

					strongThis->m_b.setRand(19 * 8);
					BigNumber gmod = constants::srp::g.modExp(strongThis->m_b, constants::srp::N);
					strongThis->m_B = ((strongThis->m_v * 3) + gmod) % constants::srp::N;

					assert(gmod.getNumBytes() <= 32);
					strongThis->m_unk3.setRand(16 * 8);

					// Allow handling the logon proof packet now
					strongThis->RegisterPacketHandler(auth::realm_login_packet::LogonProof, *strongThis.get(), &Realm::HandleLogonProof);
				}
				else
				{
					// Invalid account name display
					WLOG("Invalid realm name " << strongThis->GetRealmName());
				}

				// Send packet with result
				strongThis->m_connection->sendSinglePacket([authResult, &strongThis](auth::OutgoingPacket& outPacket) {
					outPacket.Start(auth::login_realm_packet::LogonChallenge);
					outPacket << io::write<uint8>(authResult);

					// On success, there are more data values to write
					if (authResult == auth::auth_result::Success)
					{
						// Write B with 32 byte length and g
						std::vector<uint8> B_ = strongThis->m_B.asByteArray(32);
						outPacket
							<< io::write_range(B_.begin(), B_.end())
							<< io::write<uint8>(constants::srp::g.asUInt32());

						// Write N with 32 byte length
						const std::vector<uint8> N_ = constants::srp::N.asByteArray(32);
						outPacket << io::write_range(N_.begin(), N_.end());

						// Write s
						const std::vector<uint8> s_ = strongThis->m_s.asByteArray();
						outPacket << io::write_range(s_.begin(), s_.end());
					}

					outPacket.Finish();
				});
			}
		};

		// Execute
		m_database.asyncRequest(std::move(handler), &IDatabase::getRealmAuthData, std::cref(m_realmName));
		return PacketParseResult::Pass;
	}

	PacketParseResult Realm::HandleLogonProof(auth::IncomingPacket & packet)
	{
		// No longer handle proof packets from the realm server
		ClearPacketHandler(auth::realm_login_packet::LogonProof);

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
		const auto t4 = sha1(reinterpret_cast<const char*>(m_realmName.data()), m_realmName.size());
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

			// Store the caluclated session key value internally for later use, also store it in the 
			// database maybe.
			m_sessionKey = K;

			// Handler method
			std::weak_ptr<Realm> weakThis{ shared_from_this() };
			auto handler = [weakThis](bool success)
			{
				if (auto strongThis = weakThis.lock())
				{
					if (success)
					{
						// Add log entry about successful login as the hashes do indeed mach (and thus, so
						// do the passwords)
						ILOG("Realm server " << strongThis->m_realmName << " successfully authenticated");
						strongThis->m_authenticated = true;

						// From here on, accept ClientAuthSession packets
						strongThis->RegisterPacketHandler(auth::realm_login_packet::ClientAuthSession, *strongThis.get(), &Realm::OnClientAuthSession);

						// If the login attempt succeeded, then we will accept RealmList request packets from now
						// on to send the realm list to the client on manual request
						//strongThis->RegisterPacketHandler(auth::client_packet::RealmList, std::bind(&Realm::HandleRealmList, strongThis.get(), std::placeholders::_1));
						strongThis->SendAuthProof(auth::AuthResult::Success);
					}
					else
					{
						strongThis->SendAuthProof(auth::AuthResult::FailDbBusy);
					}
				}
			};

			// Build version string for database (cast to uint16 as ostringstream would otherwise treat uint8
			// numbers as ascii character letters!)
			std::ostringstream versionBuilder;
			versionBuilder 
				<< static_cast<uint16>(m_version1) 
				<< "." 
				<< static_cast<uint16>(m_version2)
				<< "." 
				<< static_cast<uint16>(m_version3)
				<< "." 
				<< m_build;

			// Store session key in account database
			m_database.asyncRequest<void>(
				std::bind(&IDatabase::realmLogin, std::placeholders::_1, m_realmId, K.asHexStr(), m_address, versionBuilder.str()),
				std::move(handler));

			// Stop here since we wait for the database callback
			return PacketParseResult::Pass;
		}
		else
		{
			// Log error
			WLOG("Invalid password for realm " << m_realmName);
		}

		// Send proof result
		SendAuthProof(proofResult);

		return PacketParseResult::Pass;
	}

	PacketParseResult Realm::OnClientAuthSession(auth::IncomingPacket & packet)
	{
		// Read the packet data
		uint64 requestId;
		std::string accountName;
		uint32 clientSeed;
		uint32 serverSeed;
		SHA1Hash clientHash;

		if (!(packet
			>> io::read<uint64>(requestId)
			>> io::read_container<uint8>(accountName)
			>> io::read<uint32>(clientSeed)
			>> io::read<uint32>(serverSeed)
			>> io::read_range(clientHash)))
		{
			// Failed to read packet correctly, disconnect
			return PacketParseResult::Disconnect;
		}

		// Database handler method will validate the client hash
		std::weak_ptr<Realm> weakThis{ shared_from_this() };
		auto handler = [weakThis, requestId, accountName, clientSeed, serverSeed, clientHash](std::optional<std::pair<uint64, std::string>> result)
		{
			if (auto strongThis = weakThis.lock())
			{
				// This will store the session key
				BigNumber sessionKey;

				// The result that will be sent
				auth::AuthResult authResult = auth::auth_result::Success;
				if (result.has_value())
				{
					// Reconstruct the client hash to verify that the data sent is valid
					HashGeneratorSha1 gen;
					gen.update(accountName.data(), accountName.length());
					gen.update(serverSeed);
					gen.update(clientSeed);
					Sha1_Add_BigNumbers(gen, { BigNumber(result->second) });
					SHA1Hash checkHash = gen.finalize();

					// Verify that both hashes match
					if (checkHash != clientHash)
					{
						authResult = auth::auth_result::FailNoAccess;
					}
					else
					{
						// Store the session key on match
						sessionKey.setHexStr(result->second);
					}
				}
				else
				{
					authResult = auth::auth_result::FailWrongCredentials;
				}

				// Send the response
				strongThis->SendAuthSessionResult(requestId, authResult, result->first, sessionKey);
			}
		};

		// Now do a database request by account name to retrieve the session key
		m_database.asyncRequest(std::move(handler), &IDatabase::getAccountSessionKey, std::move(accountName));

		return PacketParseResult::Pass;
	}
}
