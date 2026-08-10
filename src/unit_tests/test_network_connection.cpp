// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Socket-level tests for Connection<P>. These run a real io_service over the loopback
// interface rather than mocking asio, because every behaviour under test here -- buffer
// growth, close ordering, strand dispatch -- is a property of how the connection drives asio,
// and a mock would only assert that the code calls what the mock expects.

#include "catch.hpp"

#include "network/connection.h"
#include "auth_protocol/auth_protocol.h"
#include "auth_protocol/auth_connection.h"
#include "binary_io/writer.h"

#include "asio/io_service.hpp"
#include "asio/ip/tcp.hpp"
#include "asio/write.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

using namespace mmo;

namespace
{
	typedef Connection<auth::Protocol> TestConnection;

	/// Records everything a connection reports so a test can assert on it directly.
	class RecordingListener final : public auth::IConnectionListener
	{
	public:
		std::vector<uint8> receivedOpCodes;
		uint32 lostCount = 0;
		uint32 malformedCount = 0;

		/// What connectionPacketReceived returns. Tests that need the connection to close
		/// itself from inside a handler set this to Disconnect.
		PacketParseResult nextResult = PacketParseResult::Pass;

		void connectionLost() override
		{
			++lostCount;
		}

		void connectionMalformedPacket() override
		{
			++malformedCount;
		}

		PacketParseResult connectionPacketReceived(auth::IncomingPacket& packet) override
		{
			receivedOpCodes.push_back(packet.GetId());
			return nextResult;
		}
	};

	struct ConnectedPair
	{
		std::shared_ptr<TestConnection> server;
		std::shared_ptr<TestConnection> client;
	};

	/// Runs the io_service until `predicate` holds or `timeout` elapses, and reports which
	/// happened. Tests assert on the return value rather than looping forever, so a regression
	/// shows up as a failure instead of a hung suite.
	template <class Predicate>
	bool PumpUntil(asio::io_service& ioService, Predicate predicate,
		std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (!predicate() && std::chrono::steady_clock::now() < deadline)
		{
			ioService.run_for(std::chrono::milliseconds(10));

			// run_for leaves the context stopped, so it has to be reset before it will dispatch
			// anything again.
			ioService.restart();
		}

		return predicate();
	}

	/// Establishes a connected pair over loopback on an ephemeral port. Both connections are
	/// started; the caller pumps the io_service.
	ConnectedPair MakeConnectedPair(asio::io_service& ioService,
		RecordingListener& serverListener, RecordingListener& clientListener)
	{
		asio::ip::tcp::acceptor acceptor(ioService,
			asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
		const uint16 port = acceptor.local_endpoint().port();

		auto server = TestConnection::create(ioService, &serverListener);
		auto client = TestConnection::create(ioService, &clientListener);

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

		const bool established = PumpUntil(ioService, [&accepted, &connected]()
		{
			return accepted && connected;
		});
		REQUIRE(established);

		acceptor.close();

		server->startReceiving();
		client->startReceiving();

		return ConnectedPair{ server, client };
	}
}

// Proves the harness itself works: without this, a later test that observes "nothing was
// received" cannot distinguish a real regression from a broken fixture.
TEST_CASE("ConnectionDeliversCompletePacket", "[network_connection]")
{
	asio::io_service ioService;
	RecordingListener serverListener;
	RecordingListener clientListener;
	ConnectedPair pair = MakeConnectedPair(ioService, serverListener, clientListener);

	pair.client->sendSinglePacket([](auth::OutgoingPacket& packet)
	{
		packet.Start(auth::client_login_packet::LogonChallenge);
		packet << io::write<uint32>(0x12345678);
		packet.Finish();
	});

	CHECK(PumpUntil(ioService, [&serverListener]()
	{
		return !serverListener.receivedOpCodes.empty();
	}));

	REQUIRE(serverListener.receivedOpCodes.size() == 1);
	CHECK(serverListener.receivedOpCodes[0] == auth::client_login_packet::LogonChallenge);
	CHECK(serverListener.malformedCount == 0);
}

// Two packets in one TCP segment must both be parsed. The parse loop advances by the consumed
// length rather than clearing the buffer, and this is what pins that behaviour.
TEST_CASE("ConnectionParsesTwoPacketsFromOneSegment", "[network_connection]")
{
	asio::io_service ioService;
	RecordingListener serverListener;
	RecordingListener clientListener;
	ConnectedPair pair = MakeConnectedPair(ioService, serverListener, clientListener);

	pair.client->sendSinglePacket([](auth::OutgoingPacket& packet)
	{
		packet.Start(auth::client_login_packet::LogonChallenge);
		packet << io::write<uint32>(1);
		packet.Finish();
	}, false);

	pair.client->sendSinglePacket([](auth::OutgoingPacket& packet)
	{
		packet.Start(auth::client_login_packet::LogonProof);
		packet << io::write<uint32>(2);
		packet.Finish();
	}, true);

	CHECK(PumpUntil(ioService, [&serverListener]()
	{
		return serverListener.receivedOpCodes.size() >= 2;
	}));

	REQUIRE(serverListener.receivedOpCodes.size() == 2);
	CHECK(serverListener.receivedOpCodes[0] == auth::client_login_packet::LogonChallenge);
	CHECK(serverListener.receivedOpCodes[1] == auth::client_login_packet::LogonProof);
}
