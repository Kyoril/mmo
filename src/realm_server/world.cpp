// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "world.h"
#include "database.h"

#include "base/random.h"
#include "base/weak_ptr_function.h"
#include "log/default_log_levels.h"
#include "auth_protocol/auth_protocol.h"

#include <functional>

#include "base/big_number.h"
#include "base/constants.h"


namespace mmo
{
	World::World(
		WorldManager& worldManager,
		AsyncDatabase& database, 
		std::shared_ptr<Client> connection, 
		const String & address)
		: m_manager(worldManager)
		, m_database(database)
		, m_connection(std::move(connection))
		, m_address(address)
	{
		m_connection->setListener(*this);

		RegisterPacketHandler(auth::world_realm_packet::LogonChallenge, *this, &World::OnLogonChallenge);
	}

	void World::Destroy()
	{
		m_connection->resetListener();
		m_connection.reset();

		m_manager.WorldDisconnected(*this);
	}

	void World::connectionLost()
	{
		ILOG("World node " << m_address << " disconnected");
		Destroy();
	}

	void World::connectionMalformedPacket()
	{
		ILOG("World node " << m_address << " sent malformed packet");
		Destroy();
	}

	PacketParseResult World::connectionPacketReceived(auth::IncomingPacket & packet)
	{
		const auto packetId = packet.GetId();
		bool isValid = true;

		PacketHandler handler = nullptr;

		{
			// Lock packet handler access
			std::scoped_lock<std::mutex> lock{ m_packetHandlerMutex };

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

	PacketParseResult World::OnLogonChallenge(auth::IncomingPacket& packet)
	{
		ClearPacketHandler(auth::world_realm_packet::LogonChallenge);

		// Read the packet data
		if (!(packet
			>> io::read<uint8>(m_version1)
			>> io::read<uint8>(m_version2)
			>> io::read<uint8>(m_version3)
			>> io::read<uint16>(m_build)
			>> io::read_container<uint8>(m_worldName)))
		{
			return PacketParseResult::Disconnect;
		}

		// Write the login attempt to the logs
		ILOG("Received logon challenge for world " << m_worldName << "...");

		// RequestHandler
		std::weak_ptr<World> weakThis{ shared_from_this() };
		auto handler = [weakThis](std::optional<WorldAuthData> result) {
			if (auto strongThis = weakThis.lock())
			{
				// The temporary result
				auth::AuthResult authResult = auth::auth_result::FailWrongCredentials;
				if (result)
				{
					strongThis->m_s.setHexStr(result->s);
					strongThis->m_v.setHexStr(result->v);

					// Store realm infos
					strongThis->m_worldId = result->id;
					strongThis->m_worldName = result->name;

					// We are NOT banned so continue
					authResult = auth::auth_result::Success;

					strongThis->m_b.setRand(19 * 8);
					const auto gmod = constants::srp::g.modExp(strongThis->m_b, constants::srp::N);
					strongThis->m_B = ((strongThis->m_v * 3) + gmod) % constants::srp::N;

					assert(gmod.getNumBytes() <= 32);
					strongThis->m_unk3.setRand(16 * 8);

					// Allow handling the logon proof packet now
					strongThis->RegisterPacketHandler(auth::world_realm_packet::LogonProof, *strongThis.get(), &World::OnLogonProof);
				}
				else
				{
					// Invalid account name display
					WLOG("Invalid world name " << strongThis->GetWorldName());
				}

				// Send packet with result
				strongThis->m_connection->sendSinglePacket([authResult, &strongThis](auth::OutgoingPacket& outPacket) {
					outPacket.Start(auth::realm_world_packet::LogonChallenge);
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
		m_database.asyncRequest(std::move(handler), &IDatabase::GetWorldAuthData, std::cref(m_worldName));
		return PacketParseResult::Pass;
	}

	PacketParseResult World::OnLogonProof(auth::IncomingPacket& packet)
	{
		ClearPacketHandler(auth::world_realm_packet::LogonProof);

		// Read packet data
		std::array<uint8, 32> rec_A{};
		std::array<uint8, 20> rec_M1{};
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
		const auto t = S.asByteArray(32);
		std::array<uint8, 16> t1{};
		for (size_t i = 0; i < t1.size(); ++i)
		{
			t1[i] = t[i * 2];
		}
		hash = sha1(reinterpret_cast<const char*>(t1.data()), t1.size());

		std::array<uint8, 40> vK{};
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
		const auto t4 = sha1(reinterpret_cast<const char*>(m_worldName.data()), m_worldName.size());
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
			m_m2 = Sha1_BigNumbers({ A, M1, K });
			m_sessionKey = 0;
			
			// Handler method
			std::weak_ptr<World> weakThis{ shared_from_this() };
			auto handler = [weakThis, K](bool success)
			{
				if (auto strongThis = weakThis.lock())
				{
					if (success)
					{
						// Add log entry about successful login as the hashes do indeed mach (and thus, so
						// do the passwords)
						ILOG("World node " << strongThis->m_worldName << " successfully authenticated");

						// Store the calculated session key value internally for later use, also store it in the 
						// database maybe.
						strongThis->m_sessionKey = K;
						strongThis->RegisterPacketHandler(auth::world_realm_packet::PropagateMapList, *strongThis, &World::OnPropagateMapList);

						// If the login attempt succeeded, then we will accept RealmList request packets from now
						// on to send the realm list to the client on manual request
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
				std::bind(&IDatabase::WorldLogin, std::placeholders::_1, m_worldId, K.asHexStr(), m_address, versionBuilder.str()),
				std::move(handler));

			// Stop here since we wait for the database callback
			return PacketParseResult::Pass;
		}
		else
		{
			// Log error
			WLOG("Invalid password for world " << m_worldName);
		}

		// Send proof result
		SendAuthProof(proofResult);
		return PacketParseResult::Pass;
	}

	void World::SendAuthProof(auth::AuthResult result)
	{
		// Send proof packet to the client
		m_connection->sendSinglePacket([result, this](auth::OutgoingPacket& packet) {
			packet.Start(auth::realm_world_packet::LogonProof);
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

	PacketParseResult World::OnPropagateMapList(auth::IncomingPacket& packet)
	{
		std::scoped_lock<std::mutex> lock{ m_hostedMapIdMutex };
		
		m_hostedMapIds.clear();
		if (!(packet >> io::read_container<uint16>(m_hostedMapIds)))
		{
			return PacketParseResult::Disconnect;
		}

		DLOG("Received new list of hosted map ids from world node, containing " << m_hostedMapIds.size() << " map ids");
		for(const auto& mapId : m_hostedMapIds)
		{
			DLOG("\tMap: " << mapId);
		}
		
		return PacketParseResult::Pass;
	}

	void World::RegisterPacketHandler(uint16 opCode, PacketHandler && handler)
	{
		std::scoped_lock<std::mutex> lock{ m_packetHandlerMutex };

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

	void World::ClearPacketHandler(uint16 opCode)
	{
		std::scoped_lock<std::mutex> lock{ m_packetHandlerMutex };

		auto it = m_packetHandlers.find(opCode);
		if (it != m_packetHandlers.end())
		{
			m_packetHandlers.erase(it);
		}
	}

	bool World::IsHostingMapId(uint64 mapId)
	{
		std::scoped_lock<std::mutex> lock{ m_hostedMapIdMutex };
		return std::find(m_hostedMapIds.begin(), m_hostedMapIds.end(), mapId) != m_hostedMapIds.end();
	}

	void World::RequestMapInstanceCreation(uint64 mapId)
	{
		// TODO: Implement this method. This should notify the connected world node that it should
		// create an instance for a given map id for us and notify us as soon as this has happened.
		// Instance creation might fail, in which case we also want to be notified about failure and
		// the reason of failure!

		GetConnection().sendSinglePacket([](auth::OutgoingPacket& outPacket)
			{
				outPacket.Start(auth::realm_world_packet::PlayerCharacterJoin);
				outPacket.Finish();
			});
	}
}
