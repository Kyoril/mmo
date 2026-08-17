// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// The login server runs two io threads, and its database result dispatcher is a bare
// ioService.post() -- so a completion lands on whichever thread is free, which is not
// necessarily the one that owns the session the completion belongs to.
//
// That matters because every one of these completions ends in a packet. sendSinglePacket
// builds the packet straight into the connection's send buffer and records byte offsets into
// it (the packet's own size field is patched in at Finish()), while the connection's send
// completion runs on the strand and does `m_sending.swap(m_sendBuffer); m_sendBuffer.clear()`.
// Run those two concurrently and the buffer is swapped away under a half-written packet:
// Finish() then patches a size into a buffer that no longer holds the packet, which is the
// `(position + size) <= m_buffer.size()` assert in StringSink::Overwrite that took the login
// server down mid-realm-list. In a release build there is no assert, just a memcpy past the
// end of a std::string.
//
// So the invariant these tests hold: a session's database results run on that session's
// strand, never inline on whatever thread completed the request.

#include "catch.hpp"

#include "login_server/player.h"
#include "login_server/player_manager.h"
#include "login_server/realm.h"
#include "login_server/realm_manager.h"
#include "auth_protocol/auth_connection.h"
#include "binary_io/memory_source.h"
#include "binary_io/string_sink.h"
#include "binary_io/writer.h"
#include "base/timer_queue.h"
#include "mock_database.h"

// For game::ProtocolVersion only -- a challenge that misreports it never reaches the database.
#include "game_protocol/game_protocol.h"

#include "asio/io_service.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mmo
{
	namespace
	{
		/// A connection that records what a session does to it instead of touching a socket.
		///
		/// Nothing here runs work on its own: `Post` collects rather than dispatches, so a test
		/// can ask the question that matters -- did this reach the connection directly, or did it
		/// come through the strand? -- without threads or timing.
		class RecordingConnection final : public AbstractConnection<auth::Protocol>
		{
		public:
			Listener* listener = nullptr;

			/// The buffer a session writes its packets into, exactly as the real connection's is.
			Buffer sendBuffer;

			/// One entry per flush: the bytes that would have gone on the wire.
			std::vector<std::string> flushed;

			/// Work handed to the strand and not yet run, in the order it was posted.
			std::vector<std::function<void()>> postedToStrand;

			void setListener(Listener& listener_) override { listener = &listener_; }
			void resetListener() override { listener = nullptr; }
			asio::ip::address getRemoteAddress() const override { return asio::ip::address_v4::loopback(); }
			void startReceiving() override {}
			void resumeParsing() override {}
			void close() override {}
			Buffer& getSendBuffer() override { return sendBuffer; }
			void SetMaxReceiveBufferSize(std::size_t) override {}

			void flush() override
			{
				if (sendBuffer.empty())
				{
					return;
				}

				flushed.push_back(sendBuffer);
				sendBuffer.clear();
			}

			void Post(std::function<void()> work) override
			{
				postedToStrand.push_back(std::move(work));
			}

			/// Runs everything currently queued on the strand, in order.
			///
			/// Swapped out first because the work may post more -- a kick posted from inside a
			/// completion, for one -- and appending to the vector being iterated is undefined.
			void RunStrand()
			{
				std::vector<std::function<void()>> work;
				work.swap(postedToStrand);

				for (const auto& item : work)
				{
					item();
				}
			}

			/// Whether anything at all has been written towards the wire.
			[[nodiscard]] bool WroteAnything() const { return !sendBuffer.empty() || !flushed.empty(); }
		};

		/// An AsyncDatabase that holds the request instead of running it.
		///
		/// Production hands the request to a pool thread and the result back through a dispatcher.
		/// Holding the request lets a test decide when that pool thread returns; running the result
		/// inline on the caller models the essential property of the real dispatcher -- the
		/// completion runs on a thread that is not this session's strand.
		struct HeldDatabase
		{
			std::function<void(IDatabase&)> pendingWork;

			AsyncDatabase async{
				[this](uint64, std::function<void(IDatabase&)> work) { pendingWork = std::move(work); },
				[](std::function<void()> action) { action(); } };

			/// Completes the outstanding request, as the database pool thread would.
			void Complete(IDatabase& database)
			{
				REQUIRE(pendingWork);

				auto work = std::move(pendingWork);
				pendingWork = nullptr;
				work(database);
			}
		};

		/// Wraps a body in the auth protocol's framing, ready to be parsed as an incoming packet.
		std::string framePacket(const uint8 opCode, const std::string& body)
		{
			std::string framed;
			io::StringSink sink{ framed };
			auth::OutgoingPacket out{ sink };
			out.Start(opCode);
			out << io::write_range(body.begin(), body.end());
			out.Finish();

			return framed;
		}

		/// The version and protocol prologue both the client's and the realm's challenge open with.
		std::string challengePrologue()
		{
			std::string body;
			io::StringSink sink{ body };
			io::Writer writer{ sink };
			writer
				<< io::write<uint8>(1)
				<< io::write<uint8>(0)
				<< io::write<uint8>(0)
				<< io::write<uint16>(1)
				<< io::write<uint32>(auth::ProtocolVersion)
				<< io::write<uint32>(game::ProtocolVersion);

			return body;
		}
	}

	// The path that actually crashed: a client's account lookup completes off-strand and the
	// session answers it with a packet. Before the fix that packet was built on the completing
	// thread, straight into a send buffer the strand is free to swap away at any moment.
	TEST_CASE("PlayerDatabaseResultsRunOnTheConnectionStrand", "[session_strand]")
	{
		auto connection = std::make_shared<RecordingConnection>();

		PlayerManager playerManager{ 16 };
		RealmManager realmManager{ 16 };
		MockDatabase mockDatabase;
		HeldDatabase database;

		auto player = std::make_shared<Player>(playerManager, realmManager, database.async,
			connection, "127.0.0.1");
		playerManager.AddPlayer(player);

		// A logon challenge, which is what makes the session ask the database for the account.
		std::string body = challengePrologue();
		{
			io::StringSink sink{ body };
			io::Writer writer{ sink };
			writer
				<< io::write<uint32>(0x656e5553)	// 'enUS'
				<< io::write_dynamic_range<uint8>(std::string("TESTACCOUNT"));
		}

		const std::string framed = framePacket(auth::client_login_packet::LogonChallenge, body);
		io::MemorySource source{ framed.data(), framed.data() + framed.size() };
		auth::IncomingPacket challenge;
		REQUIRE(auth::IncomingPacket::Start(challenge, source) == ReceiveState::Complete);

		REQUIRE(connection->listener != nullptr);
		REQUIRE(connection->listener->connectionPacketReceived(challenge) == PacketParseResult::Pass);

		// The challenge itself answers nothing -- the session is waiting on the database.
		REQUIRE_FALSE(connection->WroteAnything());

		// The account is unknown, so the completion takes the rejection path. Which answer it
		// sends does not matter here; that it sends one from this thread is the whole point.
		mockDatabase.accountData = std::nullopt;
		database.Complete(mockDatabase);

		// Nothing may have touched the connection yet.
		CHECK(connection->sendBuffer.empty());
		CHECK(connection->flushed.empty());
		REQUIRE(connection->postedToStrand.size() == 1);

		// ...and the answer still goes out, once the strand gets to it.
		connection->RunStrand();
		CHECK(connection->flushed.size() == 1);
	}

	// The realm link carries the same hazard and is exercised far harder: the login server runs
	// one account lookup per player entering a realm, all of it on a single long-lived connection
	// that is simultaneously carrying pings.
	TEST_CASE("RealmDatabaseResultsRunOnTheConnectionStrand", "[session_strand]")
	{
		asio::io_service ioService;
		TimerQueue timerQueue{ ioService };

		auto connection = std::make_shared<RecordingConnection>();

		RealmManager realmManager{ 16 };
		MockDatabase mockDatabase;
		HeldDatabase database;

		auto realm = std::make_shared<Realm>(realmManager, database.async, connection,
			"127.0.0.1", timerQueue);

		std::string body = challengePrologue();
		{
			io::StringSink sink{ body };
			io::Writer writer{ sink };
			writer << io::write_dynamic_range<uint8>(std::string("TESTREALM"));
		}

		const std::string framed = framePacket(auth::realm_login_packet::LogonChallenge, body);
		io::MemorySource source{ framed.data(), framed.data() + framed.size() };
		auth::IncomingPacket challenge;
		REQUIRE(auth::IncomingPacket::Start(challenge, source) == ReceiveState::Complete);

		REQUIRE(connection->listener != nullptr);
		REQUIRE(connection->listener->connectionPacketReceived(challenge) == PacketParseResult::Pass);

		REQUIRE_FALSE(connection->WroteAnything());

		// An unknown realm name, so the completion answers with a rejection.
		mockDatabase.realmAuthData = std::nullopt;
		database.Complete(mockDatabase);

		CHECK(connection->sendBuffer.empty());
		CHECK(connection->flushed.empty());
		REQUIRE(connection->postedToStrand.size() == 1);

		connection->RunStrand();
		CHECK(connection->flushed.size() == 1);
	}
}
