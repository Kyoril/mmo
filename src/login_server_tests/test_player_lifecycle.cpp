// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Lifecycle tests for the login server's Player: that an authenticated session can be found
// by the manager, that kicking one actually closes its socket, and that a kick requested from
// another thread runs on the connection's strand.

#include "catch.hpp"

#include "login_server/player.h"
#include "login_server/player_manager.h"
#include "login_server/realm_manager.h"
#include "auth_protocol/auth_connection.h"
#include "mock_database.h"

#include "asio/io_service.hpp"
#include "asio/ip/tcp.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace mmo
{
	namespace
	{
		/// Runs the io_service until `predicate` holds or the timeout elapses.
		template <class Predicate>
		bool pumpUntil(asio::io_service& ioService, Predicate predicate,
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

		/// An AsyncDatabase whose dispatchers drop everything.
		///
		/// Player stores AsyncDatabase& and only uses it from packet handlers, none of which these
		/// lifecycle tests drive. Since requests are now handed to a dispatcher rather than bound
		/// to an instance, dropping them needs no database object at all.
		struct DiscardingDatabase
		{
			AsyncDatabase async{
				[](uint64, std::function<void(IDatabase&)>) {},
				[](std::function<void()>) {} };
		};

		/// A connected loopback pair. `server` is the side a Player is built around; `peer` is
		/// the far end, used to observe what the client would see.
		struct LoopbackPair
		{
			std::shared_ptr<auth::Connection> server;
			std::shared_ptr<auth::Connection> peer;
		};

		LoopbackPair makeLoopbackPair(asio::io_service& ioService)
		{
			asio::ip::tcp::acceptor acceptor(ioService,
				asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
			const uint16 port = acceptor.local_endpoint().port();

			auto server = auth::Connection::create(ioService, nullptr);
			auto peer = auth::Connection::create(ioService, nullptr);

			bool accepted = false;
			acceptor.async_accept(server->getSocket(), [&accepted](const asio::error_code& error)
			{
				REQUIRE(!error);
				accepted = true;
			});

			bool connected = false;
			peer->getSocket().async_connect(
				asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), port),
				[&connected](const asio::error_code& error)
			{
				REQUIRE(!error);
				connected = true;
			});

			REQUIRE(pumpUntil(ioService, [&accepted, &connected]()
			{
				return accepted && connected;
			}));

			acceptor.close();

			return LoopbackPair{ server, peer };
		}
	}

	// Kicking must close the socket. Dropping the connection shared_ptr is not enough: the
	// pending async_read holds the connection alive, so the peer saw a session that simply
	// stopped answering rather than a closed connection.
	TEST_CASE("KickClosesTheClientSocket", "[player_lifecycle]")
	{
		asio::io_service ioService;
		LoopbackPair pair = makeLoopbackPair(ioService);

		PlayerManager playerManager{ 16 };
		RealmManager realmManager{ 16 };
		DiscardingDatabase database;

		auto player = std::make_shared<Player>(playerManager, realmManager, database.async,
			pair.server, "127.0.0.1");
		playerManager.AddPlayer(player);
		pair.server->startReceiving();

		// The far end must observe EOF once the session is kicked.
		bool peerSawClose = false;
		std::array<char, 16> readBuffer{};
		pair.peer->getSocket().async_read_some(asio::buffer(readBuffer),
			[&peerSawClose](const asio::error_code& error, std::size_t)
		{
			if (error)
			{
				peerSawClose = true;
			}
		});

		player->Kick();

		CHECK(pumpUntil(ioService, [&peerSawClose]() { return peerSawClose; }));
	}

	// A kick requested from another thread must run on the connection's strand, not on the
	// caller's. The REST ban handler runs on whichever io thread picked up the request, while
	// the session's own handlers run on its strand -- reaching in directly is a data race on
	// the listener pointer and the send buffer.
	TEST_CASE("PostKickDefersToTheConnectionStrand", "[player_lifecycle]")
	{
		asio::io_service ioService;
		LoopbackPair pair = makeLoopbackPair(ioService);

		PlayerManager playerManager{ 16 };
		RealmManager realmManager{ 16 };
		DiscardingDatabase database;

		auto player = std::make_shared<Player>(playerManager, realmManager, database.async,
			pair.server, "127.0.0.1");
		playerManager.AddPlayer(player);
		pair.server->startReceiving();

		player->PostKick();

		// Still registered: nothing has pumped the service, so the kick has not run yet.
		CHECK(playerManager.GetPlayerCount() == 1);

		CHECK(pumpUntil(ioService, [&playerManager]()
		{
			return playerManager.GetPlayerCount() == 0;
		}));
	}

	// The capacity limit is what stops an unbounded number of sessions being accepted. It
	// existed as a method with no callers anywhere in the tree, so the configured maximum had
	// no effect whatsoever -- a server would keep accepting until it ran out of descriptors.
	//
	// Checked at the boundary rather than at one arbitrary count, because an off-by-one here
	// either rejects a legitimate last slot or admits one past the limit.
	TEST_CASE("PlayerManagerReportsCapacityAtTheBoundary", "[player_lifecycle]")
	{
		asio::io_service ioService;
		PlayerManager playerManager{ 3 };
		RealmManager realmManager{ 16 };
		DiscardingDatabase database;

		const auto addPlayer = [&]()
		{
			auto connection = auth::Connection::create(ioService, nullptr);
			playerManager.AddPlayer(std::make_shared<Player>(playerManager, realmManager,
				database.async, connection, "127.0.0.1"));
		};

		CHECK_FALSE(playerManager.HasPlayerCapacityBeenReached());

		addPlayer();
		CHECK_FALSE(playerManager.HasPlayerCapacityBeenReached());

		addPlayer();
		CHECK_FALSE(playerManager.HasPlayerCapacityBeenReached());

		// The third fills the last slot: at capacity, not over it.
		addPlayer();
		CHECK(playerManager.HasPlayerCapacityBeenReached());
		CHECK(playerManager.GetPlayerCount() == 3);
	}

	// Hammers the exact pattern the REST ban handler produces: one thread looking sessions up
	// and posting kicks while the io threads dispatch the connection handlers.
	//
	// This cannot prove the absence of a race -- MSVC has no thread sanitizer. What it does is
	// give the failure a chance to happen under AddressSanitizer (-DMMO_ENABLE_ASAN=ON), where
	// the use-after-free that the old raw-pointer lookup allowed shows up as a report rather
	// than as silence. Tagged [.stress] so it is skipped unless asked for by name.
	TEST_CASE("ConcurrentKickIsSafe", "[player_lifecycle][.stress]")
	{
		asio::io_service ioService;
		PlayerManager playerManager{ 256 };
		RealmManager realmManager{ 16 };
		DiscardingDatabase database;

		std::vector<std::shared_ptr<Player>> players;
		for (int index = 0; index < 64; ++index)
		{
			auto connection = auth::Connection::create(ioService, nullptr);
			auto player = std::make_shared<Player>(playerManager, realmManager, database.async,
				connection, "127.0.0.1");
			playerManager.AddPlayer(player);
			players.push_back(std::move(player));
		}

		std::atomic<bool> running{ true };
		std::thread kicker([&playerManager, &running]()
		{
			while (running)
			{
				for (uint64 accountId = 0; accountId < 64; ++accountId)
				{
					playerManager.KickPlayerByAccountId(accountId);
				}
			}
		});

		for (int pass = 0; pass < 200; ++pass)
		{
			ioService.run_for(std::chrono::milliseconds(1));
			ioService.restart();
		}

		running = false;
		kicker.join();

		// Drain whatever the kicker posted before the fixture goes out of scope.
		ioService.restart();
		ioService.run();

		SUCCEED("no crash under concurrent lookup and kick");
	}

	// Displacing an account's other sessions must ignore sessions that have not authenticated.
	//
	// Every session starts life with account id 0 and stays there until the logon challenge
	// resolves, so a displacement that matched on the account id alone would treat every
	// half-connected client as a duplicate login of account 0 and drop them all. The
	// IsAuthenticated() filter is the only thing standing between this feature and mass
	// disconnects of clients that are still typing their password.
	TEST_CASE("DisplacementSkipsUnauthenticatedSessions", "[player_lifecycle]")
	{
		asio::io_service ioService;
		PlayerManager playerManager{ 16 };
		RealmManager realmManager{ 16 };
		DiscardingDatabase database;

		std::vector<std::shared_ptr<Player>> players;
		for (int index = 0; index < 3; ++index)
		{
			auto connection = auth::Connection::create(ioService, nullptr);
			auto player = std::make_shared<Player>(playerManager, realmManager, database.async,
				connection, "127.0.0.1");
			playerManager.AddPlayer(player);
			players.push_back(std::move(player));
		}

		REQUIRE(playerManager.GetPlayerCount() == 3);

		// Account 0 is what every one of them reports until it authenticates.
		playerManager.KickOtherSessionsForAccount(0, *players.front(),
			auth::session_kick_reason::LoggedInElsewhere);

		// Kicks are posted, so pump before concluding that none happened.
		for (int pass = 0; pass < 20; ++pass)
		{
			ioService.run_for(std::chrono::milliseconds(1));
			ioService.restart();
		}

		CHECK(playerManager.GetPlayerCount() == 3);
	}

	// The manager must hand out an owning reference. Its mutex protects the list, not the
	// lifetime of what comes out of it, so a raw pointer returned to another thread could
	// outlive the session it names.
	TEST_CASE("PlayerLookupReturnsAnOwningReference", "[player_lifecycle]")
	{
		PlayerManager playerManager{ 16 };

		const std::shared_ptr<Player> missing = playerManager.GetPlayerByAccountID(1);
		CHECK(missing == nullptr);
	}
}
