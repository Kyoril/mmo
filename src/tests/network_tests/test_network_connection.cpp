// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Socket-level tests for Connection<P>. These run a real io_service over the loopback
// interface rather than mocking asio, because every behaviour under test here -- buffer
// growth, close ordering, strand dispatch -- is a property of how the connection drives asio,
// and a mock would only assert that the code calls what the mock expects.

#include "catch.hpp"

#include "network/connection.h"
#include "network/server.h"
#include "network/shutdown_signals.h"
#include "base/signal.h"
#include "auth_protocol/auth_protocol.h"
#include "auth_protocol/auth_connection.h"
#include "binary_io/writer.h"

#include "asio/io_service.hpp"
#include "asio/ip/tcp.hpp"
#include "asio/write.hpp"

#include <csignal>
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

// A peer that announces a legal size and then never finishes the body must not be able to
// grow the receive buffer without limit. Before the cap, m_received grew until the process ran
// out of memory -- a remote denial of service costing the attacker one open socket.
TEST_CASE("ConnectionDropsPeerThatExceedsReceiveBufferCap", "[network_connection]")
{
	asio::io_service ioService;
	RecordingListener serverListener;
	RecordingListener clientListener;
	ConnectedPair pair = MakeConnectedPair(ioService, serverListener, clientListener);

	pair.server->SetMaxReceiveBufferSize(64 * 1024);

	// A header announcing a body well under the protocol ceiling -- so the Task 1 check passes
	// -- followed by more filler than the cap allows. The body is never completed.
	std::vector<char> garbage;
	garbage.push_back(static_cast<char>(auth::client_login_packet::LogonChallenge));

	const uint32 announcedSize = 1024 * 1024;
	const char* const announcedBytes = reinterpret_cast<const char*>(&announcedSize);
	garbage.insert(garbage.end(), announcedBytes, announcedBytes + sizeof(announcedSize));
	garbage.resize(garbage.size() + 96 * 1024, 'x');

	asio::error_code writeError;
	asio::write(pair.client->getSocket(), asio::buffer(garbage), writeError);

	CHECK(PumpUntil(ioService, [&serverListener]()
	{
		return serverListener.malformedCount > 0;
	}));

	// The half-delivered packet must never reach a handler.
	CHECK(serverListener.receivedOpCodes.empty());
}

// The cap must not fire on ordinary traffic that happens to arrive in many small segments.
TEST_CASE("ConnectionAcceptsPacketBelowReceiveBufferCap", "[network_connection]")
{
	asio::io_service ioService;
	RecordingListener serverListener;
	RecordingListener clientListener;
	ConnectedPair pair = MakeConnectedPair(ioService, serverListener, clientListener);

	pair.server->SetMaxReceiveBufferSize(64 * 1024);

	pair.client->sendSinglePacket([](auth::OutgoingPacket& packet)
	{
		packet.Start(auth::client_login_packet::LogonChallenge);
		packet << io::write_dynamic_range<uint16>(std::string(32 * 1024, 'a'));
		packet.Finish();
	});

	CHECK(PumpUntil(ioService, [&serverListener]()
	{
		return !serverListener.receivedOpCodes.empty();
	}));

	CHECK(serverListener.malformedCount == 0);
}

// Work handed to Post must run on the connection's strand, not on the calling thread.
//
// This is what code that does not own a connection -- another session's handler, or a web
// request handler on a different io thread -- uses to reach it. Running synchronously would
// defeat the point: the caller would be touching connection state concurrently with the
// handlers asio dispatches on the strand.
TEST_CASE("ConnectionPostDefersWorkToTheStrand", "[network_connection]")
{
	asio::io_service ioService;
	RecordingListener listener;
	auto connection = TestConnection::create(ioService, &listener);

	bool ran = false;
	connection->Post([&ran]() { ran = true; });

	// Not yet: nothing has pumped the service.
	CHECK_FALSE(ran);

	CHECK(PumpUntil(ioService, [&ran]() { return ran; }));
}

// Post must keep the connection alive until the work runs, so a caller that drops its last
// reference in between does not leave the handler pointing at a destroyed object.
TEST_CASE("ConnectionPostKeepsConnectionAlive", "[network_connection]")
{
	asio::io_service ioService;
	RecordingListener listener;

	std::weak_ptr<TestConnection> weak;
	bool ran = false;
	{
		auto connection = TestConnection::create(ioService, &listener);
		weak = connection;
		connection->Post([&ran]() { ran = true; });
	}

	// The local shared_ptr is gone, but the posted work still holds one.
	CHECK_FALSE(weak.expired());

	CHECK(PumpUntil(ioService, [&ran]() { return ran; }));
	CHECK(weak.expired());
}

// Stop() must close the listening socket, so a later connect is refused rather than queued.
// Without it there is no way to stop accepting during shutdown, and a server would keep taking
// new connections while it tears itself down.
TEST_CASE("ServerStopsAcceptingAfterStop", "[network_connection]")
{
	asio::io_service ioService;

	// Bind an ephemeral port by hand first so the test knows which port to probe, then release
	// it so the Server under test can take it.
	uint16 boundPort = 0;
	{
		asio::ip::tcp::acceptor probe(ioService,
			asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
		boundPort = probe.local_endpoint().port();
	}

	uint32 acceptedCount = 0;
	{
		Server<TestConnection> server(ioService, boundPort,
			[&ioService](asio::io_service&) { return TestConnection::create(ioService, nullptr); });

		const scoped_connection connected{ server.connected().connect(
			[&acceptedCount](const std::shared_ptr<TestConnection>&) { ++acceptedCount; }) };

		server.startAccept();
		server.Stop();

		asio::ip::tcp::socket probe(ioService);
		bool connectFinished = false;
		asio::error_code connectError;
		probe.async_connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), boundPort),
			[&connectFinished, &connectError](const asio::error_code& error)
		{
			connectError = error;
			connectFinished = true;
		});

		CHECK(PumpUntil(ioService, [&connectFinished]() { return connectFinished; }));
		CHECK(connectError);
	}

	CHECK(acceptedCount == 0);
}

// The listener must survive a transient accept failure. Returning without re-arming -- which is
// what the accept handler used to do on any error -- means one EMFILE under a connect storm
// stops the tier from ever accepting again, while it keeps running and looks healthy.
TEST_CASE("ServerKeepsAcceptingAfterTheFirstConnection", "[network_connection]")
{
	asio::io_service ioService;

	uint16 boundPort = 0;
	{
		asio::ip::tcp::acceptor probe(ioService,
			asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
		boundPort = probe.local_endpoint().port();
	}

	Server<TestConnection> server(ioService, boundPort,
		[&ioService](asio::io_service&) { return TestConnection::create(ioService, nullptr); });

	uint32 acceptedCount = 0;
	const scoped_connection connected{ server.connected().connect(
		[&acceptedCount](const std::shared_ptr<TestConnection>&) { ++acceptedCount; }) };

	server.startAccept();

	std::vector<std::shared_ptr<asio::ip::tcp::socket>> clients;
	for (int index = 0; index < 3; ++index)
	{
		auto client = std::make_shared<asio::ip::tcp::socket>(ioService);
		client->async_connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), boundPort),
			[](const asio::error_code&) {});
		clients.push_back(std::move(client));
	}

	CHECK(PumpUntil(ioService, [&acceptedCount]() { return acceptedCount >= 3; }));
	CHECK(acceptedCount == 3);

	server.Stop();
}

// The shutdown handler must fire when the signal arrives, and its wait must be cancellable.
//
// The second half is the subtle one: a pending async_wait counts as outstanding io_service work,
// so a shutdown sequence that releases its work guards and waits for the service to drain will
// hang forever if the signal wait is left armed. Every program.cpp shutdown path depends on it.
TEST_CASE("ShutdownHandlerFiresAndCanBeCancelled", "[network_connection]")
{
	SECTION("raising the signal runs the handler")
	{
		asio::io_service ioService;
		bool handled = false;
		auto signals = InstallShutdownHandler(ioService, [&handled]() { handled = true; });

		std::raise(SIGINT);

		CHECK(PumpUntil(ioService, [&handled]() { return handled; }));
	}

	SECTION("cancelling lets the service drain")
	{
		asio::io_service ioService;
		bool handled = false;
		auto signals = InstallShutdownHandler(ioService, [&handled]() { handled = true; });

		asio::error_code error;
		signals->cancel(error);
		CHECK_FALSE(error);

		// run() returns only because the cancelled wait is no longer outstanding work. If this
		// hangs, every server's shutdown would hang the same way.
		ioService.run();
		CHECK_FALSE(handled);
	}
}

// Many packets sent back to back must all arrive, in order.
//
// This is the regression guard for the buffer handoff in flush(): getting it wrong loses or
// duplicates queued bytes, and nothing else in the suite sends enough traffic to notice. It is a
// characterisation test -- it must pass both before and after that change.
TEST_CASE("ConnectionDeliversManyPacketsInOrder", "[network_connection]")
{
	asio::io_service ioService;
	RecordingListener serverListener;
	RecordingListener clientListener;
	ConnectedPair pair = MakeConnectedPair(ioService, serverListener, clientListener);

	// Alternating opcodes so the assertion catches reordering, not just loss.
	const std::size_t packetCount = 200;
	for (std::size_t index = 0; index < packetCount; ++index)
	{
		const uint8 opCode = (index % 2 == 0)
			? auth::client_login_packet::LogonChallenge
			: auth::client_login_packet::LogonProof;

		pair.client->sendSinglePacket([index, opCode](auth::OutgoingPacket& packet)
		{
			packet.Start(opCode);
			packet << io::write<uint32>(static_cast<uint32>(index));
			packet.Finish();
		});
	}

	CHECK(PumpUntil(ioService, [&serverListener, packetCount]()
	{
		return serverListener.receivedOpCodes.size() >= packetCount;
	}));

	REQUIRE(serverListener.receivedOpCodes.size() == packetCount);
	CHECK(serverListener.malformedCount == 0);

	bool orderHeld = true;
	for (std::size_t index = 0; index < packetCount; ++index)
	{
		const uint8 expected = (index % 2 == 0)
			? auth::client_login_packet::LogonChallenge
			: auth::client_login_packet::LogonProof;
		if (serverListener.receivedOpCodes[index] != expected)
		{
			orderHeld = false;
			break;
		}
	}

	CHECK(orderHeld);
}
