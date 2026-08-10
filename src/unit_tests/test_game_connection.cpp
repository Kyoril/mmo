// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Socket-level tests for EncryptedConnection<P> -- the realm server's client-facing link.
//
// The packet codec no-ops until a session key is installed (Crypt::DecryptReceive returns
// early when uninitialised), so these tests drive it with plaintext bytes and exercise the
// framing and teardown paths without standing up a full SRP handshake.

#include "catch.hpp"

#include "game_protocol/game_protocol.h"
#include "game_protocol/game_connection.h"
#include "binary_io/writer.h"

#include "asio/io_service.hpp"
#include "asio/ip/tcp.hpp"
#include "asio/write.hpp"

#include <chrono>
#include <memory>
#include <vector>

using namespace mmo;

namespace
{
	class GameRecordingListener final : public game::IConnectionListener
	{
	public:
		std::vector<uint16> receivedOpCodes;
		uint32 lostCount = 0;
		uint32 malformedCount = 0;

		void connectionLost() override
		{
			++lostCount;
		}

		void connectionMalformedPacket() override
		{
			++malformedCount;
		}

		PacketParseResult connectionPacketReceived(game::IncomingPacket& packet) override
		{
			receivedOpCodes.push_back(packet.GetId());
			return PacketParseResult::Pass;
		}
	};

	struct GameConnectedPair
	{
		std::shared_ptr<game::Connection> server;
		std::shared_ptr<game::Connection> client;
	};

	template <class Predicate>
	bool PumpGameUntil(asio::io_service& ioService, Predicate predicate,
		std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (!predicate() && std::chrono::steady_clock::now() < deadline)
		{
			ioService.run_for(std::chrono::milliseconds(10));
			ioService.restart();
		}

		return predicate();
	}

	GameConnectedPair MakeGameConnectedPair(asio::io_service& ioService,
		GameRecordingListener& serverListener, GameRecordingListener& clientListener)
	{
		asio::ip::tcp::acceptor acceptor(ioService,
			asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
		const uint16 port = acceptor.local_endpoint().port();

		auto server = game::Connection::Create(ioService, &serverListener);
		auto client = game::Connection::Create(ioService, &clientListener);

		bool accepted = false;
		acceptor.async_accept(server->getSocket(), [&accepted](const asio::error_code& error)
		{
			REQUIRE(!error);
			accepted = true;
		});

		bool connected = false;
		client->getSocket().async_connect(
			asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), port),
			[&connected](const asio::error_code& error)
		{
			REQUIRE(!error);
			connected = true;
		});

		REQUIRE(PumpGameUntil(ioService, [&accepted, &connected]()
		{
			return accepted && connected;
		}));

		acceptor.close();

		server->startReceiving();
		client->startReceiving();

		return GameConnectedPair{ server, client };
	}
}

// Harness sanity, so a later "nothing arrived" assertion means something.
TEST_CASE("GameConnectionDeliversCompletePacket", "[game_connection]")
{
	asio::io_service ioService;
	GameRecordingListener serverListener;
	GameRecordingListener clientListener;
	GameConnectedPair pair = MakeGameConnectedPair(ioService, serverListener, clientListener);

	pair.client->sendSinglePacket([](game::OutgoingPacket& packet)
	{
		packet.Start(game::client_realm_packet::NameQuery);
		packet << io::write<uint64>(1234);
		packet.Finish();
	});

	CHECK(PumpGameUntil(ioService, [&serverListener]()
	{
		return !serverListener.receivedOpCodes.empty();
	}));

	REQUIRE(serverListener.receivedOpCodes.size() == 1);
	CHECK(serverListener.receivedOpCodes[0] == game::client_realm_packet::NameQuery);
}

// The realm's client-facing link must survive dropping a peer that overruns the cap.
//
// This is the path a hostile client reaches directly, so getting the teardown wrong here turns
// a memory-exhaustion DoS into a crash DoS. Specifically: tearing down by releasing the socket
// leaves the in-flight async_read_some to complete with zero bytes, which routes into
// Disconnected() -- and that dereferenced m_socket without checking it.
TEST_CASE("GameConnectionSurvivesDroppingAnOverrunningPeer", "[game_connection]")
{
	asio::io_service ioService;
	GameRecordingListener serverListener;
	GameRecordingListener clientListener;
	GameConnectedPair pair = MakeGameConnectedPair(ioService, serverListener, clientListener);

	pair.server->SetMaxReceiveBufferSize(16 * 1024);

	// Announce a body under the protocol ceiling, then send more filler than the cap allows and
	// never complete it.
	std::vector<char> garbage;

	const uint16 opCode = static_cast<uint16>(game::client_realm_packet::ChatMessage);
	const char* const opCodeBytes = reinterpret_cast<const char*>(&opCode);
	garbage.insert(garbage.end(), opCodeBytes, opCodeBytes + sizeof(opCode));

	const uint32 announcedSize = 1024 * 1024;
	const char* const announcedBytes = reinterpret_cast<const char*>(&announcedSize);
	garbage.insert(garbage.end(), announcedBytes, announcedBytes + sizeof(announcedSize));
	garbage.resize(garbage.size() + 32 * 1024, 'x');

	asio::error_code writeError;
	asio::write(pair.client->getSocket(), asio::buffer(garbage), writeError);

	CHECK(PumpGameUntil(ioService, [&serverListener]()
	{
		return serverListener.malformedCount > 0;
	}));

	CHECK(serverListener.receivedOpCodes.empty());

	// Keep pumping after the drop: this is where the aborted read completes and reaches
	// Disconnected(). Reaching the end of this test at all is the assertion.
	CHECK(PumpGameUntil(ioService, []() { return false; }, std::chrono::milliseconds(200)) == false);
	SUCCEED("connection tore down without dereferencing a released socket");
}

// The same drop, but with a write in flight.
//
// This is the ordering that actually reaches the hazard: the overrun tears the connection down
// from inside the read handler, and the outstanding write then completes with an error and
// routes into Disconnected(). If teardown released the socket, that handler dereferences a
// pointer that no longer names anything.
TEST_CASE("GameConnectionSurvivesOverrunWithWriteInFlight", "[game_connection]")
{
	asio::io_service ioService;
	GameRecordingListener serverListener;
	GameRecordingListener clientListener;
	GameConnectedPair pair = MakeGameConnectedPair(ioService, serverListener, clientListener);

	pair.server->SetMaxReceiveBufferSize(16 * 1024);

	// Queue enough outbound data that it cannot all be handed to the kernel at once, so the
	// write is still outstanding when the inbound overrun is detected.
	for (int index = 0; index < 64; ++index)
	{
		pair.server->sendSinglePacket([](game::OutgoingPacket& packet)
		{
			packet.Start(game::realm_client_packet::ChatMessage);
			packet << io::write_dynamic_range<uint16>(std::string(16 * 1024, 'p'));
			packet.Finish();
		});
	}

	std::vector<char> garbage;

	const uint16 opCode = static_cast<uint16>(game::client_realm_packet::ChatMessage);
	const char* const opCodeBytes = reinterpret_cast<const char*>(&opCode);
	garbage.insert(garbage.end(), opCodeBytes, opCodeBytes + sizeof(opCode));

	const uint32 announcedSize = 1024 * 1024;
	const char* const announcedBytes = reinterpret_cast<const char*>(&announcedSize);
	garbage.insert(garbage.end(), announcedBytes, announcedBytes + sizeof(announcedSize));
	garbage.resize(garbage.size() + 32 * 1024, 'x');

	asio::error_code writeError;
	asio::write(pair.client->getSocket(), asio::buffer(garbage), writeError);

	CHECK(PumpGameUntil(ioService, [&serverListener]()
	{
		return serverListener.malformedCount > 0;
	}));

	// Drain: the in-flight write's completion handler runs in here. Reaching the end without
	// faulting is the assertion.
	CHECK(PumpGameUntil(ioService, []() { return false; }, std::chrono::milliseconds(500)) == false);
	SUCCEED("teardown with a write in flight did not dereference a released socket");
}
