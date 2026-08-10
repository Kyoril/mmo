# Server Network Robustness & Scalability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close a remote memory-exhaustion hole, three dead/broken safety mechanisms, and a cross-thread lifetime race in the login server, and give all three server tiers a graceful shutdown — with a regression test for every one of them.

**Architecture:** All changes are in `src/shared/network/`, `src/shared/auth_protocol/`, `src/shared/game_protocol/`, and the three `src/*_server/` programs. The design follows patterns proven in the ROSE prototype (`D:/Src/rose`): bound every buffer that a peer can grow, keep per-connection state on that connection's strand and hop onto it with an explicit `Post` rather than reaching across threads, let the io_service *drain* at shutdown rather than being stopped, and never hand out a raw pointer to an object whose lifetime another thread controls.

**Tech Stack:** C++20 (MSVC `/std:c++latest`, gcc `-std=c++2a`), standalone ASIO (header-only, `deps/asio`), Catch2 unit tests, PowerShell E2E harness (`tools/e2e/e2e_run.ps1`).

## Global Constraints

- **Copyright header** on every new or modified source file: `// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.`
- **Braces:** Allman style — every `{` and `}` on its own line. Always brace `if` bodies, even single-line.
- **Indentation:** Tabs.
- **Naming:** members `m_camelCase`; methods `PascalCase`; locals and anonymous-namespace free functions `camelCase`; files `snake_case`.
- **Headers:** `#pragma once`; Doxygen comments on all public members.
- **Namespace:** root `mmo`; enum pseudo-namespaces as `namespace receive_state { enum Enum { ... }; }`.
- **No exceptions** (`SIMPLE_NO_EXCEPTIONS`). Use `ASSERT` / `VERIFY` / `UNREACHABLE` from `src/shared/base/macros.h` and `DLOG` / `ILOG` / `WLOG` / `ELOG` for output.
- **Servers stay single-threaded except the login server**, which runs 2 io threads (`src/login_server/program.cpp:281`). Do not change any tier's thread count in this plan.
- **Build config for this branch:** `cmake -S . -B build -DMMO_BUILD_CLIENT=ON -DMMO_BUILD_EDITOR=OFF -DMMO_BUILD_TOOLS=OFF -DMMO_WITH_DEV_COMMANDS=ON -DMMO_BUILD_TESTS=ON`. The client must stay in the build: it consumes `game_protocol` and `network`, so a shared-layer change can break it.
- **Every task ends green.** A task is not done until its own tests pass *and* `powershell -File tools/gate/verify.ps1 -SkipE2E` passes. The full gate (with E2E) runs at the phase boundaries called out below.

## Out of Scope — Deferred to a Follow-On Plan

The **database connection pool** (replacing the single `mysql::Connection` + single `dbService` thread with a pool, as `rose::database::pool` does) is deliberately **not** in this plan. It is the largest throughput win available but carries an ordering hazard that needs its own design: today's single DB thread gives implicit FIFO ordering that call sites rely on — e.g. `SetCharacterActionButtons` at `src/realm_server/player.cpp:3873` followed by `GetActionButtons` at `src/realm_server/player.cpp:3901` on class change. A pool breaks that silently. That work gets its own plan once this one is merged green.

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `src/shared/auth_protocol/auth_protocol.h` | Auth protocol constants | Add `MaxIncomingPacketSize` |
| `src/shared/auth_protocol/auth_incoming_packet.cpp` | Auth packet framing | Reject oversized size field |
| `src/shared/game_protocol/game_protocol.h` | Game protocol constants | Add `MaxIncomingPacketSize` |
| `src/shared/game_protocol/game_incoming_packet.cpp` | Game packet framing | Reject oversized size field |
| `src/shared/network/connection.h` | Plaintext framed connection | Receive-buffer cap; `Post`; send-buffer swap |
| `src/shared/game_protocol/game_connection.h` | Encrypted framed connection | Receive-buffer cap; `Post` |
| `src/shared/network/server.h` | TCP acceptor | Retry on error; full backlog; `Stop()` |
| `src/shared/network/shutdown_signals.h` | **New.** Signal handler install helper | Created in Task 7 |
| `src/login_server/player.h` / `.cpp` | Login session | Real `IsAuthenticated`; `destroy` closes socket; `PostKick` |
| `src/login_server/player_manager.h` / `.cpp` | Login session registry | Return `shared_ptr`; strand-safe kick; `DisconnectAll` |
| `src/login_server/realm_manager.h` / `.cpp` | Realm link registry | `DisconnectAll` |
| `src/realm_server/player_manager.h` / `.cpp`, `world_manager.*` | Realm registries | `DisconnectAll` |
| `src/world_server/program.cpp`, `src/realm_server/program.cpp`, `src/login_server/program.cpp` | Program wiring | Capacity checks; shutdown sequence |
| `src/unit_tests/test_network_connection.cpp` | **New.** Socket-level connection tests | Created in Task 2 |
| `src/unit_tests/test_auth_protocol.cpp`, `test_game_protocol.cpp` | Protocol framing tests | Add oversize-rejection cases |
| `src/login_server_tests/test_player_lifecycle.cpp` | **New.** Login session lifecycle tests | Created in Task 4 |
| `cmake/mmo_options.cmake` | Build options | Add `MMO_ENABLE_ASAN` |
| `tools/gate/verify.ps1` | Quality gate | Add `realm_server_tests` |
| `tools/e2e/e2e_down.ps1` | E2E teardown | Graceful stop before force-kill |

---

## Phase 1 — Bound Every Peer-Growable Buffer

### Task 1: Reject impossible packet sizes at the protocol layer

Both incoming-packet decoders read a `uint32` size straight off the wire and never validate it. Any value larger than what is buffered returns `Incomplete`, so the connection waits for a body that will never arrive while its receive buffer grows. This task adds the cheap, precise half of the fix; Task 3 adds the protocol-agnostic backstop.

**Files:**
- Modify: `src/shared/auth_protocol/auth_protocol.h` (add constant near `ProtocolVersion`, line 19)
- Modify: `src/shared/auth_protocol/auth_incoming_packet.cpp:18-40`
- Modify: `src/shared/game_protocol/game_protocol.h` (add constant near the top of `namespace game`)
- Modify: `src/shared/game_protocol/game_incoming_packet.cpp:19-42`
- Test: `src/unit_tests/test_auth_protocol.cpp`, `src/unit_tests/test_game_protocol.cpp`

**Interfaces:**
- Produces: `mmo::auth::MaxIncomingPacketSize` and `mmo::game::MaxIncomingPacketSize`, both `constexpr uint32`, value `16 * 1024 * 1024`. Task 3 references these as the default connection cap.

**Why 16 MiB:** it is far above anything this engine sends (the largest client→server packet is a chat message; the largest server→server payload is a character blob) and far below a value that can exhaust memory before the connection cap in Task 5 bites. It is an absolute garbage filter, not a tuning knob — the per-connection cap in Task 3 is the knob.

- [ ] **Step 1: Write the failing auth test**

Append to `src/unit_tests/test_auth_protocol.cpp`:

```cpp
// A peer that announces a payload larger than the protocol ceiling must be reported as
// malformed immediately. Returning Incomplete here is what let a 5-byte header pin the
// receiving connection into buffering until it ran out of memory.
TEST_CASE("AuthPacketRejectsOversizedPayload", "[auth_protocol]")
{
	std::vector<char> buffer;
	buffer.push_back(static_cast<char>(auth::client_login_packet::LogonChallenge));

	const uint32 announcedSize = auth::MaxIncomingPacketSize + 1;
	const char* const announcedBytes = reinterpret_cast<const char*>(&announcedSize);
	buffer.insert(buffer.end(), announcedBytes, announcedBytes + sizeof(announcedSize));

	io::MemorySource src{ buffer };
	auth::IncomingPacket packet;

	CHECK(auth::IncomingPacket::Start(packet, src) == ReceiveState::Malformed);
}

// The largest legal packet must still be accepted, so the ceiling cannot silently become
// a functional limit if a payload ever grows towards it.
TEST_CASE("AuthPacketAcceptsMaximumSizeAnnouncement", "[auth_protocol]")
{
	std::vector<char> buffer;
	buffer.push_back(static_cast<char>(auth::client_login_packet::LogonChallenge));

	const uint32 announcedSize = auth::MaxIncomingPacketSize;
	const char* const announcedBytes = reinterpret_cast<const char*>(&announcedSize);
	buffer.insert(buffer.end(), announcedBytes, announcedBytes + sizeof(announcedSize));

	io::MemorySource src{ buffer };
	auth::IncomingPacket packet;

	// Incomplete, not Malformed: the size is legal, the body simply has not arrived.
	CHECK(auth::IncomingPacket::Start(packet, src) == ReceiveState::Incomplete);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build --config Debug -t unit_tests && ./bin/Debug/unit_tests.exe "[auth_protocol]"
```

Expected: compile error `'MaxIncomingPacketSize': is not a member of 'mmo::auth'`.

- [ ] **Step 3: Add the auth constant**

In `src/shared/auth_protocol/auth_protocol.h`, directly below `constexpr uint32 ProtocolVersion = 0x00000003;`:

```cpp
		/// Largest payload, in bytes, that a single incoming auth packet may announce.
		///
		/// The size field is read straight off the wire before any of it is trusted, so
		/// without a ceiling a peer can announce an arbitrary length and the receiving
		/// connection will buffer indefinitely waiting for a body that never arrives. The
		/// value is deliberately far above any packet this protocol actually carries: it is
		/// a garbage filter, not a tuning knob. Per-connection tightening is done with
		/// AbstractConnection::SetMaxReceiveBufferSize.
		constexpr uint32 MaxIncomingPacketSize = 16 * 1024 * 1024;
```

- [ ] **Step 4: Enforce it in the auth decoder**

In `src/shared/auth_protocol/auth_incoming_packet.cpp`, inside `Start`, immediately after the `if (streamReader >> ... >> io::read<uint32>(packet.m_size))` condition opens its block:

```cpp
				if (packet.m_size > MaxIncomingPacketSize)
				{
					// Not Incomplete: no amount of further data can make this packet valid,
					// and treating it as "still arriving" is what let the receive buffer grow
					// without bound.
					return receive_state::Malformed;
				}

```

- [ ] **Step 5: Run the auth test to verify it passes**

```bash
cmake --build build --config Debug -t unit_tests && ./bin/Debug/unit_tests.exe "[auth_protocol]"
```

Expected: `All tests passed`.

- [ ] **Step 6: Repeat for the game protocol**

In `src/shared/game_protocol/game_protocol.h`, inside `namespace game`, above `namespace client_realm_packet`:

```cpp
		/// Largest payload, in bytes, that a single incoming game packet may announce.
		/// See mmo::auth::MaxIncomingPacketSize — same reasoning, same value.
		constexpr uint32 MaxIncomingPacketSize = 16 * 1024 * 1024;
```

In `src/shared/game_protocol/game_incoming_packet.cpp`, inside `Start`, immediately after the `if (streamReader >> ... >> io::read<uint32>(packet.m_size))` condition opens its block:

```cpp
				if (packet.m_size > MaxIncomingPacketSize)
				{
					// See auth::IncomingPacket::Start — an announced size beyond the ceiling
					// can never become a valid packet, so it must not be reported as still
					// arriving.
					return receive_state::Malformed;
				}

```

Append to `src/unit_tests/test_game_protocol.cpp`:

```cpp
// See AuthPacketRejectsOversizedPayload — the encrypted client link needs the same ceiling.
TEST_CASE("GamePacketRejectsOversizedPayload", "[game_protocol]")
{
	std::vector<char> buffer;

	const uint16 opCode = static_cast<uint16>(game::client_realm_packet::ChatMessage);
	const char* const opCodeBytes = reinterpret_cast<const char*>(&opCode);
	buffer.insert(buffer.end(), opCodeBytes, opCodeBytes + sizeof(opCode));

	const uint32 announcedSize = game::MaxIncomingPacketSize + 1;
	const char* const announcedBytes = reinterpret_cast<const char*>(&announcedSize);
	buffer.insert(buffer.end(), announcedBytes, announcedBytes + sizeof(announcedSize));

	io::MemorySource src{ buffer };
	game::IncomingPacket packet;

	CHECK(game::IncomingPacket::Start(packet, src) == ReceiveState::Malformed);
}
```

- [ ] **Step 7: Run the full unit suite**

```bash
cmake --build build --config Debug -t unit_tests && ./bin/Debug/unit_tests.exe
```

Expected: `All tests passed`. If `test_game_protocol.cpp` lacks `#include <vector>` or the `game_protocol.h` include, add them.

- [ ] **Step 8: Commit**

```bash
git add src/shared/auth_protocol src/shared/game_protocol src/unit_tests/test_auth_protocol.cpp src/unit_tests/test_game_protocol.cpp && git commit -m "fix(network): reject packets announcing an impossible payload size"
```

---

### Task 2: Socket-level test harness for `Connection<P>`

Nothing in the tree currently exercises `Connection<P>` over a real socket, so every behaviour in Tasks 3, 4, 6 and 10 would otherwise be untestable. This task adds the harness and one sanity test that proves the harness itself works. No production code changes.

**Files:**
- Create: `src/unit_tests/test_network_connection.cpp`

**Interfaces:**
- Produces, for Tasks 3, 6 and 10:
  - `RecordingListener` — an `auth::IConnectionListener` with public members `receivedOpCodes` (`std::vector<uint8>`), `lostCount` (`uint32`), `malformedCount` (`uint32`), and `nextResult` (`PacketParseResult`, default `Pass`).
  - `ConnectedPair MakeConnectedPair(asio::io_service&, RecordingListener& serverListener, RecordingListener& clientListener)` returning `struct ConnectedPair { std::shared_ptr<Connection<auth::Protocol>> server; std::shared_ptr<Connection<auth::Protocol>> client; }`, both already started.
  - `template <class Predicate> bool PumpUntil(asio::io_service&, Predicate, std::chrono::milliseconds timeout = std::chrono::seconds(5))` returning whether the predicate became true.

- [ ] **Step 1: Write the harness and its sanity test**

Create `src/unit_tests/test_network_connection.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Socket-level tests for Connection<P>. These run a real io_service over the loopback
// interface rather than mocking asio, because every behaviour under test here -- buffer
// growth, close ordering, strand dispatch -- is a property of how the connection drives
// asio, and a mock would only assert that the code calls what the mock expects.

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
	/// happened. Tests assert on the return value rather than looping forever, so a
	/// regression shows up as a failure instead of a hung suite.
	template <class Predicate>
	bool PumpUntil(asio::io_service& ioService, Predicate predicate,
		std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (!predicate() && std::chrono::steady_clock::now() < deadline)
		{
			ioService.run_for(std::chrono::milliseconds(10));

			// run_for leaves the context in the stopped state, so it has to be reset before
			// it will dispatch anything again.
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

// Two packets in one TCP segment must both be parsed. The parse loop advances by the
// consumed length rather than clearing the buffer, and this is what pins that behaviour.
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
```

- [ ] **Step 2: Build and run**

```bash
cmake --build build --config Debug -t unit_tests && ./bin/Debug/unit_tests.exe "[network_connection]"
```

Expected: both tests pass. `unit_tests` already links `network_hdrs`, `auth_protocol` and `binary_io_hdrs`, and `add_exe_recurse` picks up the new file automatically — no CMake change is needed. If the file is not compiled, re-run `cmake -S . -B build` to regenerate.

- [ ] **Step 3: Commit**

```bash
git add src/unit_tests/test_network_connection.cpp && git commit -m "test(network): add socket-level Connection harness"
```

---

### Task 3: Cap the receive buffer per connection

The protocol ceiling from Task 1 rejects an absurd *announced* size. It does not stop a peer from announcing a legal 1 MiB packet, sending 900 KiB, and stalling — repeated across many connections that is still memory the server cannot reclaim. This cap bounds the buffer directly and is protocol-agnostic.

**Files:**
- Modify: `src/shared/network/connection.h` — add `SetMaxReceiveBufferSize` to `AbstractConnection` (near line 71) and the member + check to `Connection` (parse loop ends at line 449)
- Modify: `src/shared/game_protocol/game_connection.h` — same, in `EncryptedConnection`
- Modify: `src/login_server/program.cpp`, `src/realm_server/program.cpp` — tighten the cap on client-facing connections
- Test: `src/unit_tests/test_network_connection.cpp`

**Interfaces:**
- Consumes: `RecordingListener`, `MakeConnectedPair`, `PumpUntil` from Task 2; `auth::MaxIncomingPacketSize` from Task 1.
- Produces: `virtual void AbstractConnection<P>::SetMaxReceiveBufferSize(std::size_t)`, implemented by both `Connection` and `EncryptedConnection`. Default is `16 * 1024 * 1024`.

- [ ] **Step 1: Write the failing test**

Append to `src/unit_tests/test_network_connection.cpp`:

```cpp
// A peer that announces a legal size and then never finishes the body must not be able to
// grow the receive buffer without limit. Before the cap, m_received grew until the process
// ran out of memory -- a remote denial of service costing the attacker one open socket.
TEST_CASE("ConnectionDropsPeerThatExceedsReceiveBufferCap", "[network_connection]")
{
	asio::io_service ioService;
	RecordingListener serverListener;
	RecordingListener clientListener;
	ConnectedPair pair = MakeConnectedPair(ioService, serverListener, clientListener);

	pair.server->SetMaxReceiveBufferSize(64 * 1024);

	// A header announcing a body well under the protocol ceiling -- so Task 1's check passes
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
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build --config Debug -t unit_tests && ./bin/Debug/unit_tests.exe "[network_connection]"
```

Expected: compile error — `SetMaxReceiveBufferSize` is not a member of `AbstractConnection`.

- [ ] **Step 3: Add the interface method**

In `src/shared/network/connection.h`, in `class AbstractConnection`, after `virtual Buffer& getSendBuffer() = 0;` (line 73):

```cpp
			/// Sets how many bytes may accumulate in the receive buffer before the peer is
			/// dropped as malformed.
			///
			/// The buffer holds whatever has arrived but not yet formed a complete packet, so
			/// it is exactly the quantity a peer controls: announce a large body, send most of
			/// it, stall. Bounding it is what makes that harmless. Set this well above the
			/// largest packet the link legitimately carries -- a value below it turns ordinary
			/// traffic into a disconnect.
			virtual void SetMaxReceiveBufferSize(std::size_t size) = 0;
```

- [ ] **Step 4: Implement it in `Connection`**

In `src/shared/network/connection.h`, in `class Connection`, public section (after `getSendBuffer`, line 148):

```cpp
			void SetMaxReceiveBufferSize(std::size_t size) override
			{
				m_maxReceiveBufferSize = size;
			}
```

Add to the private member block (after `bool m_isReceiving;`, line 263):

```cpp
			/// Defaults to the protocol ceiling (see auth::MaxIncomingPacketSize /
			/// game::MaxIncomingPacketSize, both 16 MiB). Named here as a literal rather than
			/// by including a protocol header, which would invert the dependency: the protocols
			/// include this file, not the other way round.
			std::size_t m_maxReceiveBufferSize = 16 * 1024 * 1024;
```

In `parsePackets()`, immediately after the `if (parsedUntil) { ... }` erase block and *before* `beginReceive();` (line 449):

```cpp
			// Whatever is left is a single incomplete packet. If that alone is over the cap it
			// can only grow further, so there is nothing to wait for.
			if (m_received.size() > m_maxReceiveBufferSize)
			{
				ELOG("Peer exceeded the maximum receive buffer size (" << m_received.size()
					<< " > " << m_maxReceiveBufferSize << " bytes) - dropping connection");

				if (m_listener)
				{
					m_listener->connectionMalformedPacket();
					m_listener = nullptr;
				}

				m_received.clear();

				if (m_socket && m_socket->is_open())
				{
					asio::error_code error;
					m_socket->close(error);
				}

				return;
			}

```

- [ ] **Step 5: Run the tests**

```bash
cmake --build build --config Debug -t unit_tests && ./bin/Debug/unit_tests.exe "[network_connection]"
```

Expected: all four `[network_connection]` tests pass.

- [ ] **Step 6: Mirror it in `EncryptedConnection`**

In `src/shared/game_protocol/game_connection.h`, add the same public override and the same member (line 185 area, next to `m_strand`), and the same guard at the end of `ParsePackets()` before its `BeginReceive()` call. Use `m_socket.reset()` instead of `m_socket->close(error)` — that class already tears down that way in its `Malformed` branch.

- [ ] **Step 7: Tighten the cap on client-facing connections**

In `src/login_server/program.cpp`, inside `createPlayer` immediately after the `address` try/catch block (line 216):

```cpp
			// Client connections are anonymous and reachable from the internet, so they get a
			// far tighter bound than the 16 MiB protocol ceiling. The largest packet a login
			// client sends is the logon proof at ~52 bytes; 64 KiB leaves several orders of
			// magnitude of headroom while making the buffer irrelevant as an attack surface.
			connection->SetMaxReceiveBufferSize(64 * 1024);
```

In `src/realm_server/program.cpp`, inside `createPlayer` at the equivalent point, with the same call and this comment:

```cpp
			// See the login server: the largest client->realm packet is a chat message, so
			// 64 KiB is generous. Server<->server links keep the 16 MiB default.
			connection->SetMaxReceiveBufferSize(64 * 1024);
```

- [ ] **Step 8: Run the full suite and the E2E gate**

```bash
powershell -File tools/gate/verify.ps1
```

Expected: `Gate result: GREEN`. The E2E run is the check that 64 KiB does not break real client traffic.

- [ ] **Step 9: Commit**

```bash
git add src/shared/network/connection.h src/shared/game_protocol/game_connection.h src/login_server/program.cpp src/realm_server/program.cpp src/unit_tests/test_network_connection.cpp && git commit -m "fix(network): bound the per-connection receive buffer"
```

---

## Phase 2 — Revive the Broken Safety Mechanisms

### Task 4: Make login sessions identifiable and actually kickable

`Player::IsAuthenticated()` at `src/login_server/player.h:56` is hardcoded `return false;`. Every `PlayerManager` lookup filters on it, so `GetPlayerByAccountName`, `GetPlayerByAccountID` and `KickPlayerByAccountId` can never find anyone: banning an account through the REST API writes the ban and silently fails to disconnect the session. Separately, `Player::destroy()` at `src/login_server/player.cpp:44` drops the connection `shared_ptr` without closing the socket, so even once the lookup works the peer is never disconnected — the connection stays alive in asio's pending read with a null listener.

**Files:**
- Modify: `src/login_server/player.h:56` and the `m_connection` member declaration
- Modify: `src/login_server/player.cpp:38-50`
- Test: `src/login_server_tests/test_player_lifecycle.cpp` (create)

**Interfaces:**
- Produces: `bool Player::IsAuthenticated() const` returning `!m_sessionKey.isZero()`; `Player::destroy()` gains the documented invariant that it runs on the connection's strand and leaves `m_connection` non-null for the object's lifetime. Task 8 depends on `m_connection` being immutable after construction.

- [ ] **Step 1: Write the failing test**

Create `src/login_server_tests/test_player_lifecycle.cpp`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

// Lifecycle tests for the login server's Player: that an authenticated session can be found
// by the manager, and that kicking one actually closes its socket.

#include "catch.hpp"

#include "login_server/player.h"
#include "login_server/player_manager.h"
#include "login_server/realm_manager.h"
#include "auth_protocol/auth_connection.h"
#include "mock_database.h"

#include "asio/io_service.hpp"
#include "asio/ip/tcp.hpp"

#include <chrono>
#include <memory>

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
		/// Player stores AsyncDatabase& and only uses it from packet handlers, none of which
		/// these lifecycle tests drive. A real object over a MockDatabase is used rather than a
		/// reference bound from a null pointer, which would be undefined behaviour even though
		/// nothing dereferences it.
		struct DiscardingDatabase
		{
			MockDatabase mock;
			AsyncDatabase async{ mock,
				[](const std::function<void()>&) {},
				[](const std::function<void()>&) {} };
		};
	}

	// Kicking must close the socket. Dropping the connection shared_ptr is not enough: the
	// pending async_read holds the connection alive, so the peer saw a session that simply
	// stopped answering rather than a closed connection.
	TEST_CASE("KickClosesTheClientSocket", "[player_lifecycle]")
	{
		asio::io_service ioService;

		asio::ip::tcp::acceptor acceptor(ioService,
			asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0));
		const uint16 port = acceptor.local_endpoint().port();

		auto serverSide = auth::Connection::create(ioService, nullptr);
		auto peerSide = auth::Connection::create(ioService, nullptr);

		bool accepted = false;
		acceptor.async_accept(serverSide->getSocket(), [&accepted](const asio::error_code& error)
		{
			REQUIRE(!error);
			accepted = true;
		});

		bool connected = false;
		peerSide->getSocket().async_connect(
			asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), port),
			[&connected](const asio::error_code& error)
		{
			REQUIRE(!error);
			connected = true;
		});

		REQUIRE(pumpUntil(ioService, [&accepted, &connected]() { return accepted && connected; }));
		acceptor.close();

		PlayerManager playerManager{ 16 };
		RealmManager realmManager{ 16 };
		DiscardingDatabase database;

		auto player = std::make_shared<Player>(playerManager, realmManager, database.async,
			serverSide, "127.0.0.1");
		playerManager.AddPlayer(player);
		serverSide->startReceiving();

		// The peer must observe EOF once the session is kicked.
		bool peerSawClose = false;
		std::array<char, 16> readBuffer{};
		peerSide->getSocket().async_read_some(asio::buffer(readBuffer),
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
}
```

**Note on `*noDatabase`:** `Player` stores `AsyncDatabase&` and only dereferences it inside packet handlers, none of which this test drives. Binding a reference to a null pointer is technically UB, so if this trips a debug check, replace it with a default-constructed stub: add `AsyncDatabase stub{ *static_cast<IDatabase*>(nullptr), [](auto){}, [](auto){} };` guarded the same way, or promote `mock_database.h`'s `MockDatabase` into an `AsyncDatabase` built over two no-op dispatchers. Prefer the `MockDatabase` route if either form is rejected by the compiler.

- [ ] **Step 2: Register the test file with the target**

In `src/login_server_tests/CMakeLists.txt` the sources are listed explicitly, but test `.cpp` files in the directory are picked up by `add_exe`. Verify by rebuilding; if the file is not compiled, add it to a `target_sources` line.

- [ ] **Step 3: Run to verify it fails**

```bash
cmake --build build --config Debug -t login_server_tests && ./bin/Debug/login_server_tests.exe "[player_lifecycle]"
```

Expected: FAIL — `peerSawClose` stays false, because `destroy()` never closes the socket.

- [ ] **Step 4: Fix `destroy()`**

Replace `src/login_server/player.cpp:44-50` with:

```cpp
	void Player::destroy()
	{
		// Must run on the connection's strand: it tears down state that the connection's own
		// handlers touch. PlayerManager::KickPlayerByAccountId posts here rather than calling
		// directly for exactly that reason.
		m_connection->resetListener();

		// close(), not reset(). Dropping the shared_ptr leaves the connection alive -- the
		// outstanding async_read holds its own reference -- so the socket stayed open with a
		// null listener and the peer never saw a disconnect. The pointer itself is kept for
		// the object's lifetime so it can be read from any thread without racing.
		m_connection->close();

		m_manager.PlayerDisconnected(*this);
	}
```

- [ ] **Step 5: Run to verify it passes**

```bash
cmake --build build --config Debug -t login_server_tests && ./bin/Debug/login_server_tests.exe "[player_lifecycle]"
```

Expected: PASS.

- [ ] **Step 6: Write the failing lookup test**

Append to `src/login_server_tests/test_player_lifecycle.cpp`, inside `namespace mmo`:

```cpp
	// IsAuthenticated gates every PlayerManager lookup. While it was hardcoded to false the
	// REST ban handler's KickPlayerByAccountId could never match a session, so bans were
	// written to the database and the logged-in session was left untouched.
	TEST_CASE("AuthenticatedPlayerIsFoundByAccountId", "[player_lifecycle]")
	{
		PlayerManager playerManager{ 16 };

		CHECK(playerManager.GetPlayerByAccountID(1) == nullptr);
	}
```

This asserts only the negative case, because constructing a *fully authenticated* `Player` requires driving the whole SRP exchange. The positive case is covered by the E2E ban scenario added in Step 8.

- [ ] **Step 7: Fix `IsAuthenticated`**

Replace `src/login_server/player.h:56` with:

```cpp
		/// Determines whether the player has completed the SRP6 exchange.
		/// @returns true if a session key has been negotiated.
		inline bool IsAuthenticated() const { return !m_sessionKey.isZero(); }
```

Mirrors `src/realm_server/player.h:103`, which is the same predicate over the same state.

- [ ] **Step 8: Add an E2E ban scenario**

Create `e2e/scenarios/ban_disconnects_session.lua` following the conventions in `e2e/README.md`: log in, confirm the session is live, POST to the login server's ban endpoint for that account, and assert the client observes a disconnect. Read `e2e/README.md` first — the scenario API, the REST ports (login REST is 18090 in the E2E stack) and the required teardown (restore account state) are documented there.

- [ ] **Step 9: Run the full gate**

```bash
powershell -File tools/gate/verify.ps1
```

Expected: `Gate result: GREEN`.

- [ ] **Step 10: Commit**

```bash
git add src/login_server src/login_server_tests e2e/scenarios/ban_disconnects_session.lua && git commit -m "fix(login): make sessions identifiable and kicks actually close the socket"
```

---

### Task 5: Enforce the configured connection capacities

`PlayerManager::HasPlayerCapacityBeenReached()`, `RealmManager::HasCapacityBeenReached()` and the realm server's two equivalents have **zero callers**. `maxPlayers`, `maxRealms` and `maxWorlds` in the config files do nothing, so any tier will accept connections until it runs out of descriptors or memory.

**Files:**
- Modify: `src/login_server/program.cpp` (`createRealm` line 158, `createPlayer` line 204)
- Modify: `src/realm_server/program.cpp` (`createWorld`, `createPlayer`)
- Test: `src/login_server_tests/test_player_lifecycle.cpp`

**Interfaces:**
- Consumes: `pumpUntil` from Task 4.

- [ ] **Step 1: Write the failing test**

Append to `src/login_server_tests/test_player_lifecycle.cpp`, inside `namespace mmo`:

```cpp
	// The capacity limit is what stops an unbounded number of sessions from being accepted.
	// It existed as a method with no callers, so the configured maximum had no effect at all.
	TEST_CASE("PlayerManagerReportsCapacity", "[player_lifecycle]")
	{
		PlayerManager playerManager{ 2 };
		CHECK_FALSE(playerManager.HasPlayerCapacityBeenReached());

		asio::io_service ioService;
		RealmManager realmManager{ 16 };
		DiscardingDatabase database;

		for (int index = 0; index < 2; ++index)
		{
			auto connection = auth::Connection::create(ioService, nullptr);
			playerManager.AddPlayer(std::make_shared<Player>(playerManager, realmManager,
				database.async, connection, "127.0.0.1"));
		}

		CHECK(playerManager.HasPlayerCapacityBeenReached());
	}
```

- [ ] **Step 2: Run to verify it passes already**

```bash
cmake --build build --config Debug -t login_server_tests && ./bin/Debug/login_server_tests.exe "[player_lifecycle]"
```

Expected: PASS. The predicate is correct; only its *use* is missing. This test pins the predicate so the wiring in Step 3 has something to rest on.

- [ ] **Step 3: Wire the check into the login server**

In `src/login_server/program.cpp`, in `createPlayer`, immediately after the `address` try/catch block:

```cpp
			if (playerManager.HasPlayerCapacityBeenReached())
			{
				WLOG("Rejecting player connection from " << address
					<< ": the configured capacity of " << config.maxPlayers << " has been reached");
				connection->close();
				return;
			}
```

Capture `config` in the lambda: change `[&playerManager, &realmManager, &asyncDatabase]` to `[&playerManager, &realmManager, &asyncDatabase, &config]`.

In `createRealm`, at the equivalent point:

```cpp
			if (realmManager.HasCapacityBeenReached())
			{
				WLOG("Rejecting realm connection from " << address
					<< ": the configured capacity of " << config.maxRealms << " has been reached");
				connection->close();
				return;
			}
```

Capture `&config` there too.

- [ ] **Step 4: Wire the check into the realm server**

Apply the same pattern in `src/realm_server/program.cpp` to `createPlayer` (against `playerManager.HasPlayerCapacityBeenReached()` and `config.maxPlayers`) and `createWorld` (against `worldManager.HasCapacityBeenReached()` and `config.maxWorlds`, declared at `src/realm_server/configuration.h:23`).

- [ ] **Step 5: Build and run the gate**

```bash
powershell -File tools/gate/verify.ps1
```

Expected: `Gate result: GREEN`. Confirm the E2E stack's configured capacities are above the number of sessions the suite opens — check `tools/e2e/e2e_up.ps1` and the generated configs; raise them there if a scenario is rejected.

- [ ] **Step 6: Commit**

```bash
git add src/login_server/program.cpp src/realm_server/program.cpp src/login_server_tests/test_player_lifecycle.cpp && git commit -m "fix(servers): enforce the configured connection capacities"
```

---

## Phase 3 — Robustness

### Task 6: Keep accepting after a transient accept failure

`Server<C>::Accepted` at `src/shared/network/server.h:124` returns on any error without re-arming, so the first `EMFILE` under a connect storm permanently stops the tier from accepting while it keeps running and looks healthy. The backlog is also `listen(16)`, which drops connections during a login burst.

**Files:**
- Modify: `src/shared/network/server.h`
- Test: `src/unit_tests/test_network_connection.cpp`

**Interfaces:**
- Produces: `void Server<C>::Stop()` — stops accepting; safe to call more than once. Task 7 calls it.

- [ ] **Step 1: Raise the backlog**

In `src/shared/network/server.h:62`, replace `m_state->Acceptor->listen(16);` with:

```cpp
				// A backlog of 16 drops connections during a login burst; asio's maximum is
				// what the OS is willing to queue, which is the right ceiling here.
				m_state->Acceptor->listen(asio::socket_base::max_listen_connections);
```

- [ ] **Step 2: Add retry state to `State`**

In `src/shared/network/server.h`, extend `struct State` (line 104):

```cpp
			struct State
			{
				std::unique_ptr<AcceptorType> Acceptor;
				ConnectionSignal Connected;

				/// Delays the next accept after a failure. Retrying immediately would spin a
				/// core for as long as the fault lasts -- descriptor exhaustion, typically.
				asio::steady_timer RetryTimer;

				/// Set by Stop(). Read in the accept and retry handlers.
				bool Stopped = false;

				explicit State(std::unique_ptr<AcceptorType> Acceptor_, asio::io_service &IOService)
					: Acceptor(std::move(Acceptor_))
					, RetryTimer(IOService)
				{
				}
			};
```

Update the constructor at line 49 to `m_state(new State(std::unique_ptr<AcceptorType>(new AcceptorType(IOService)), IOService))`.

Add `#include "asio/steady_timer.hpp"` and `#include <chrono>` to the includes at the top.

- [ ] **Step 3: Rewrite `Accepted` and add `Stop`**

Replace `Server<C>::Accepted` (line 119) with:

```cpp
			void Accepted(std::shared_ptr<Connection> Conn, const asio::system_error &Error)
			{
				assert(Conn);
				assert(m_state);

				if (m_state->Stopped)
				{
					return;
				}

				if (Error.code())
				{
					if (Error.code() == asio::error::operation_aborted)
					{
						return;
					}

					// Transient failures -- descriptor exhaustion above all -- must not take the
					// listener down permanently. Returning here without re-arming is what let one
					// EMFILE stop a tier from ever accepting again while it kept running and
					// looked healthy.
					ELOG("Accept failed (" << Error.code().message() << "), retrying shortly");

					m_state->RetryTimer.expires_after(std::chrono::milliseconds(100));
					m_state->RetryTimer.async_wait([this](const asio::error_code &timerError)
					{
						if (!timerError && m_state && !m_state->Stopped)
						{
							startAccept();
						}
					});

					return;
				}

				m_state->Connected(Conn);
				startAccept();
			}
```

Add a public `Stop()` after `startAccept()` (line 100):

```cpp
			/// Stops accepting new connections. Connections already handed out are unaffected.
			/// Safe to call more than once.
			void Stop()
			{
				if (!m_state || m_state->Stopped)
				{
					return;
				}

				m_state->Stopped = true;

				asio::error_code error;
				m_state->RetryTimer.cancel(error);
				m_state->Acceptor->close(error);
			}
```

Add `#include "log/default_log_levels.h"` if not already present.

- [ ] **Step 4: Guard `startAccept` against a stopped server**

At the top of `startAccept()` (line 92), after the assert:

```cpp
				if (m_state->Stopped)
				{
					return;
				}
```

- [ ] **Step 5: Write the test**

Append to `src/unit_tests/test_network_connection.cpp`:

```cpp
// Stop() must close the listening socket, so a later connect is refused rather than queued.
TEST_CASE("ServerStopsAcceptingAfterStop", "[network_connection]")
{
	asio::io_service ioService;

	uint16 boundPort = 0;
	{
		// Bind an ephemeral port by hand first so the test knows which port to probe.
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
	CHECK(acceptedCount == 0);
}
```

Add `#include "network/server.h"` and `#include "base/signal.h"` to the file's includes.

- [ ] **Step 6: Build and run**

```bash
cmake --build build --config Debug -t unit_tests && ./bin/Debug/unit_tests.exe "[network_connection]"
```

Expected: all `[network_connection]` tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/shared/network/server.h src/unit_tests/test_network_connection.cpp && git commit -m "fix(network): survive transient accept failures and add Server::Stop"
```

---

### Task 7: Graceful shutdown on SIGINT / SIGTERM / SIGBREAK

No tier installs a signal handler. `docker stop` sends SIGTERM, gets no response, and SIGKILLs after 10 seconds — mid-query, mid-write. The E2E teardown at `tools/e2e/e2e_down.ps1:34` uses `Stop-Process -Force`, which cannot be caught at all.

**Files:**
- Create: `src/shared/network/shutdown_signals.h`
- Modify: `src/login_server/program.cpp`, `src/realm_server/program.cpp`, `src/world_server/program.cpp`
- Modify: `src/login_server/player_manager.*`, `src/login_server/realm_manager.*`, `src/realm_server/player_manager.*`, `src/realm_server/world_manager.*` — add `DisconnectAll()`
- Modify: `tools/e2e/e2e_down.ps1`

**Interfaces:**
- Produces: `std::unique_ptr<asio::signal_set> mmo::InstallShutdownHandler(asio::io_service&, std::function<void()>)`; `void PlayerManager::DisconnectAll()` and the equivalent on each manager.
- Consumes: `Server<C>::Stop()` from Task 6; `Player::destroy()`'s strand invariant from Task 4.

**The critical detail (learned from ROSE):** never call `ioService.stop()`. It discards queued handlers rather than running them, so the connection closes posted during shutdown would never execute. Release the work guard and let `run()` drain instead. The pending `async_wait` on the signal set also counts as outstanding work, so it must be cancelled or the service never drains.

- [ ] **Step 1: Create the helper**

Create `src/shared/network/shutdown_signals.h`:

```cpp
// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "asio/io_service.hpp"
#include "asio/signal_set.hpp"

#include <csignal>
#include <functional>
#include <memory>
#include <utility>

namespace mmo
{
	/// Installs a handler for the signals that mean "shut down cleanly".
	///
	/// The returned signal_set must be kept alive for as long as the handler should stay
	/// armed. It is also the handle the shutdown path needs: a pending async_wait counts as
	/// outstanding io_service work, so a shutdown that waits for the service to drain would
	/// otherwise wait forever. Cancel it as part of shutting down.
	///
	/// SIGINT and SIGTERM are registered on every platform. On Windows SIGBREAK is added as
	/// well, because CTRL_BREAK_EVENT is the only way one process can ask another to stop
	/// cleanly there -- Windows never raises SIGTERM itself, and TerminateProcess (which is
	/// what PowerShell's Stop-Process -Force does) gives the target no notification at all.
	inline std::unique_ptr<asio::signal_set> InstallShutdownHandler(
		asio::io_service& ioService, std::function<void()> handler)
	{
		auto signals = std::make_unique<asio::signal_set>(ioService, SIGINT, SIGTERM);

#ifdef SIGBREAK
		signals->add(SIGBREAK);
#endif

		signals->async_wait([handler = std::move(handler)](const asio::error_code& error, int)
		{
			// operation_aborted means the wait was cancelled as part of shutting down, which
			// is not itself a request to shut down again.
			if (!error)
			{
				handler();
			}
		});

		return signals;
	}
}
```

- [ ] **Step 2: Add `DisconnectAll` to the login managers**

In `src/login_server/player_manager.h`, after `KickPlayerByAccountId`:

```cpp
		/// Disconnects every managed player. Used at shutdown so peers see a closed connection
		/// rather than a socket that stops answering.
		void DisconnectAll();
```

In `src/login_server/player_manager.cpp`:

```cpp
	void PlayerManager::DisconnectAll()
	{
		// Copy the list out under the lock first: Kick() removes the player from this manager,
		// which takes the same mutex.
		std::vector<std::shared_ptr<Player>> players;
		{
			std::scoped_lock playerLock{ m_playerMutex };
			players.assign(m_players.begin(), m_players.end());
		}

		for (const auto& player : players)
		{
			player->Kick();
		}
	}
```

Add `#include <vector>` to the header. Add the same method to `RealmManager` (closing realm links), `src/realm_server/player_manager.*` and `src/realm_server/world_manager.*`, matching each manager's existing removal method.

- [ ] **Step 3: Wire shutdown into the login server**

In `src/login_server/program.cpp`, add `#include "network/shutdown_signals.h"`.

Replace the "Launch worker threads" section (line 276 onwards) so shutdown is installed before the threads start:

```cpp
		/////////////////////////////////////////////////////////////////////////////////////////////////
		// Graceful shutdown
		/////////////////////////////////////////////////////////////////////////////////////////////////

		auto shutdownSignals = InstallShutdownHandler(ioService,
			[&realmServer, &playerServer, &playerManager, &realmManager, &shutdownSignals,
			 &playerCountSampleCountdown, &dbWork]()
		{
			ILOG("Shutdown signal received - stopping cleanly");

			// Stop taking new work first, so nothing arrives while everything else winds down.
			playerServer->Stop();
			realmServer->Stop();
			playerCountSampleCountdown.Cancel();

			// Then close what is already connected. Kick() posts to each connection's strand,
			// so these run as the io_service continues to dispatch -- which is exactly why the
			// service must be allowed to drain rather than stopped.
			playerManager.DisconnectAll();
			realmManager.DisconnectAll();

			// The pending async_wait on the signal set is itself outstanding io_service work.
			// Left armed, the service would never run dry and ioService.run() would never
			// return.
			asio::error_code error;
			shutdownSignals->cancel(error);

			// Releasing the database work guard lets the db thread finish its queue and exit.
			// Note this is the ONLY thing that stops these services: ioService.stop() would
			// discard the closes just posted above instead of running them.
			dbWork.reset();
		});
```

`Countdown::Cancel()` is declared at `src/shared/base/countdown.h:32` and is `const`, so calling it on the captured reference compiles as written. Without it the sampler reschedules itself every minute and the io_service never runs dry.

After `ioService.run()` returns and the threads are joined, before `return 0;`:

```cpp
		ILOG("Login server stopped cleanly");
```

- [ ] **Step 4: Wire shutdown into the realm server**

Same pattern in `src/realm_server/program.cpp`. Additionally, the stack work guard at line 240 (`asio::io_context::work work{ ioService };`) must become releasable:

```cpp
		// Keeps the io_service alive while the server has no outstanding io of its own. Held
		// by pointer so the shutdown handler can release it -- a stack object could not be.
		auto ioWork = std::make_shared<asio::io_context::work>(ioService);
```

and the shutdown handler calls `ioWork.reset();` alongside `dbWork.reset();`.

- [ ] **Step 5: Wire shutdown into the world server**

Same pattern in `src/world_server/program.cpp`, closing its realm connector and any managed connections.

- [ ] **Step 6: Add an automated test for the handler itself**

The E2E teardown cannot exercise this. `tools/e2e/e2e_common.psm1:273` launches every server with `Start-Process -WindowStyle Hidden -RedirectStandardOutput ...`, which leaves the process with no window for `CloseMainWindow` and no shared console for `GenerateConsoleCtrlEvent`. Making it graceful would mean restructuring how the stack is launched — out of scope here. **Leave `e2e_down.ps1` as a force-kill** and add this comment above the `Stop-Process` line so the next reader does not mistake it for an oversight:

```powershell
			# TerminateProcess: no graceful path exists here. These servers are launched hidden
			# with redirected stdio (see Start-E2eServer), so they have neither a window to
			# close nor a shared console to signal. The graceful shutdown path is covered by
			# the InstallShutdownHandler unit test and by the manual Ctrl+C check in
			# docs/testing-servers.md; in production it is reached via SIGTERM from Docker.
```

Instead, test the handler directly. Append to `src/unit_tests/test_network_connection.cpp`:

```cpp
// The shutdown handler must actually fire, and its wait must be cancellable -- a pending
// async_wait counts as outstanding io_service work, so a shutdown path that waits for the
// service to drain hangs forever if the wait is left armed.
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

		// run() returns only because the cancelled wait is no longer outstanding work. If it
		// hangs here, the shutdown sequence in every program.cpp would hang too.
		ioService.run();
		CHECK_FALSE(handled);
	}
}
```

Add `#include "network/shutdown_signals.h"` and `#include <csignal>` to the file's includes.

- [ ] **Step 7: Verify manually on Windows**

```bash
cmake --build build --config Debug -t login_server
```

Run `./bin/Debug/login_server.exe` in a terminal, connect nothing, press Ctrl+C, and confirm the log ends with `Login server stopped cleanly` and the process exits 0 rather than being killed. Repeat with a client connected, and confirm the client observes a closed connection rather than a hang.

- [ ] **Step 8: Run the full gate**

```bash
powershell -File tools/gate/verify.ps1
```

Expected: `Gate result: GREEN`.

- [ ] **Step 9: Commit**

```bash
git add src/shared/network/shutdown_signals.h src/login_server src/realm_server src/world_server tools/e2e/e2e_down.ps1 && git commit -m "feat(servers): graceful shutdown on SIGINT/SIGTERM/SIGBREAK"
```

---

## Phase 4 — Cross-Thread Safety on the Login Server

The login server is the only tier running more than one io thread. Task 4 just made `KickPlayerByAccountId` functional, which turns a previously dead code path into a live cross-thread one: `LoginHttpHandlers` calls it at `src/login_server/login_http_handlers.cpp:403` and `:691` from a web handler that may run on either io thread, while the target player's own handlers run on its connection strand.

### Task 8: Hop onto the connection strand instead of reaching across threads

**Files:**
- Modify: `src/shared/network/connection.h` — add `Post` to `AbstractConnection` and `Connection`
- Modify: `src/shared/game_protocol/game_connection.h` — implement `Post` in `EncryptedConnection`
- Modify: `src/login_server/player.h` / `.cpp` — add `PostKick`
- Modify: `src/login_server/player_manager.h` / `.cpp` — return `shared_ptr`, post the kick
- Modify: `src/login_server/login_http_handlers.cpp` — call sites
- Test: `src/login_server_tests/test_player_lifecycle.cpp`

**Interfaces:**
- Produces:
  - `virtual void AbstractConnection<P>::Post(std::function<void()> work) = 0;`
  - `void Player::PostKick();`
  - `std::shared_ptr<Player> PlayerManager::GetPlayerByAccountName(const String&);`
  - `std::shared_ptr<Player> PlayerManager::GetPlayerByAccountID(uint64);`
- Consumes: `Player::destroy()`'s "`m_connection` stays non-null" invariant from Task 4 — `PostKick` reads `m_connection` from another thread, which is only safe because nothing ever reassigns it.

- [ ] **Step 1: Add `Post` to the connection interface**

In `src/shared/network/connection.h`, in `class AbstractConnection`, after `SetMaxReceiveBufferSize`:

```cpp
			/// Runs `work` on this connection's strand, keeping the connection alive until it
			/// does. Safe to call from any thread.
			///
			/// This is how code that does not own this connection -- another connection's
			/// handler, or a web request handler on a different io thread -- reaches this
			/// connection's state without racing the handlers asio dispatches on the strand.
			/// Without it the only options are a lock around every member or an unsynchronised
			/// write, and the second is what this codebase had.
			virtual void Post(std::function<void()> work) = 0;
```

In `class Connection`, public section:

```cpp
			void Post(std::function<void()> work) override
			{
				auto self = this->shared_from_this();
				asio::post(m_strand, [self, work = std::move(work)]() { work(); });
			}
```

Add `#include "asio/post.hpp"` to the includes.

- [ ] **Step 2: Implement `Post` in `EncryptedConnection`**

In `src/shared/game_protocol/game_connection.h`, add the same override using that class's `m_strand` and `shared_from_this()`. Note its `enable_shared_from_this` is parameterised on `EncryptedConnection<P, MySocket>`.

- [ ] **Step 3: Build to confirm both implementors are covered**

```bash
cmake --build build --config Debug -t login_server realm_server world_server unit_tests
```

`AbstractConnection<P>` has exactly two subclasses in the tree — `Connection` (`src/shared/network/connection.h:94`) and `EncryptedConnection` (`src/shared/game_protocol/game_connection.h:23`) — both covered by Steps 1 and 2, so this should build clean. If the compiler names a third, it was added after this plan was written; implement `Post` and `SetMaxReceiveBufferSize` there the same way.

- [ ] **Step 4: Write the failing test**

Append to `src/login_server_tests/test_player_lifecycle.cpp`, inside `namespace mmo`:

```cpp
	// A kick requested from another thread must run on the connection's strand, not on the
	// caller's. The REST ban handler runs on whichever io thread picked up the request, while
	// the session's own handlers run on its strand -- reaching in directly is a data race on
	// the listener pointer and the send buffer.
	TEST_CASE("PostRunsWorkOnTheConnectionStrand", "[player_lifecycle]")
	{
		asio::io_service ioService;
		auto connection = auth::Connection::create(ioService, nullptr);

		bool ran = false;
		connection->Post([&ran]() { ran = true; });

		CHECK_FALSE(ran);
		CHECK(pumpUntil(ioService, [&ran]() { return ran; }));
	}
```

- [ ] **Step 5: Run to verify it passes**

```bash
cmake --build build --config Debug -t login_server_tests && ./bin/Debug/login_server_tests.exe "[player_lifecycle]"
```

Expected: PASS.

- [ ] **Step 6: Add `Player::PostKick`**

In `src/login_server/player.h`, next to `Kick()`:

```cpp
		/// Requests a kick from any thread. The kick itself runs on the connection's strand.
		///
		/// Kick() tears down state that the connection's handlers also touch, so calling it
		/// directly is only safe from the strand. Callers that are not on it -- the REST ban
		/// handler, above all -- must come through here.
		void PostKick();
```

In `src/login_server/player.cpp`:

```cpp
	void Player::PostKick()
	{
		// m_connection is assigned once in the constructor and never reassigned (destroy()
		// closes it rather than releasing it), which is what makes reading it from another
		// thread safe.
		auto self = shared_from_this();
		m_connection->Post([self]() { self->Kick(); });
	}
```

- [ ] **Step 7: Make the manager hand out owning references**

In `src/login_server/player_manager.h`, change the two lookup signatures:

```cpp
		/// Gets a player by account name.
		///
		/// Returns an owning reference rather than a raw pointer: the manager's mutex protects
		/// the list, not the lifetime of what is taken out of it, so a caller on another thread
		/// could otherwise be left holding a pointer to a session that has since disconnected.
		std::shared_ptr<Player> GetPlayerByAccountName(const String &accountName);

		/// Gets a player by account id. See GetPlayerByAccountName for why this owns.
		std::shared_ptr<Player> GetPlayerByAccountID(uint64 accountId);
```

In `src/login_server/player_manager.cpp`, change both bodies to return `*p` instead of `(*p).get()` and `nullptr` unchanged, and rewrite `KickPlayerByAccountId`:

```cpp
	void PlayerManager::KickPlayerByAccountId(uint64 accountId)
	{
		const auto player = GetPlayerByAccountID(accountId);
		if (!player)
		{
			return;
		}

		// Posted rather than called: this runs on whichever thread served the REST request,
		// and Kick() may only run on the connection's strand.
		player->PostKick();
	}
```

The comment about deadlock on the old implementation no longer applies — `GetPlayerByAccountID` takes and releases the lock itself, and `PostKick` takes no lock.

- [ ] **Step 8: Fix the call sites**

Build and fix wherever `Player*` was expected:

```bash
cmake --build build --config Debug -t login_server login_server_tests
```

`src/login_server/login_http_handlers.cpp:403` and `:691` already call `KickPlayerByAccountId`, so they need no change. Any site that assigned the lookup result to `Player*` becomes `const auto`.

- [ ] **Step 9: Add a concurrency stress test**

Append to `src/login_server_tests/test_player_lifecycle.cpp`, inside `namespace mmo`:

```cpp
	// Hammers the exact pattern the REST ban handler produces: one thread looking players up
	// and posting kicks while the io threads dispatch the connection handlers. This cannot
	// prove the absence of a race -- MSVC has no thread sanitizer -- but under the ASan build
	// (MMO_ENABLE_ASAN) it reliably catches the use-after-free the raw-pointer lookup allowed.
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

		for (int pass = 0; pass < 100; ++pass)
		{
			ioService.run_for(std::chrono::milliseconds(1));
			ioService.restart();
		}

		running = false;
		kicker.join();

		// Drain whatever the kicker posted.
		ioService.restart();
		ioService.run();

		SUCCEED("no crash under concurrent lookup and kick");
	}
```

Add `#include <atomic>`, `#include <thread>` and `#include <vector>`. The `[.stress]` tag means Catch2 does not run it by default; it is run explicitly in Task 9.

- [ ] **Step 10: Run the gate**

```bash
powershell -File tools/gate/verify.ps1
```

Expected: `Gate result: GREEN`.

- [ ] **Step 11: Commit**

```bash
git add src/shared/network/connection.h src/shared/game_protocol/game_connection.h src/login_server src/login_server_tests && git commit -m "fix(login): post kicks onto the connection strand and hand out owning references"
```

---

### Task 9: AddressSanitizer build option and a sanitized run of the network tests

MSVC has no thread sanitizer, so the races addressed in Task 8 cannot be proven absent. What they *manifest* as — use-after-free and heap corruption — is exactly what AddressSanitizer detects, and MSVC supports `/fsanitize=address`.

**Files:**
- Modify: `cmake/mmo_options.cmake`
- Modify: `cmake/mmo_compilers/msvc.cmake`, `cmake/mmo_compilers/gcc.cmake`
- Modify: `docs/` — add a short note on running the sanitized suite

**Interfaces:**
- Produces: CMake option `MMO_ENABLE_ASAN`, default `OFF`.

- [ ] **Step 1: Add the option**

In `cmake/mmo_options.cmake`, alongside the existing options:

```cmake
option(MMO_ENABLE_ASAN "Build with AddressSanitizer. Used to run the stress tests -- MSVC has no thread sanitizer, so use-after-free is the observable form of the races these tests provoke." OFF)
```

In `cmake/mmo_compilers/msvc.cmake`:

```cmake
if (MMO_ENABLE_ASAN)
	add_compile_options(/fsanitize=address)
	# ASan is incompatible with incremental linking and with edit-and-continue debug info.
	add_link_options(/INCREMENTAL:NO)
endif()
```

In `cmake/mmo_compilers/gcc.cmake`:

```cmake
if (MMO_ENABLE_ASAN)
	add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
	add_link_options(-fsanitize=address)
endif()
```

- [ ] **Step 2: Build a sanitized tree**

```bash
cmake -S . -B build-asan -DMMO_BUILD_CLIENT=OFF -DMMO_BUILD_EDITOR=OFF -DMMO_WITH_DEV_COMMANDS=ON -DMMO_ENABLE_ASAN=ON
```

A separate build directory keeps the sanitized objects out of the normal one. `MMO_BUILD_CLIENT=OFF` here means `unit_tests` will fail to link (it links `graphics_d3d11` unconditionally on WIN32); build only the server test targets:

```bash
cmake --build build-asan --config Debug -t login_server_tests realm_server_tests game_server_unit_tests
```

- [ ] **Step 3: Run the stress tests under ASan**

```bash
./bin/Debug/login_server_tests.exe "[.stress]"
```

Expected: `All tests passed`, with no ASan report on stderr. Run it at least five times — a race that reproduces once in five runs is still a race.

- [ ] **Step 4: Document it**

Create `docs/testing-servers.md` recording: what the gate covers, how to run the `[.stress]` tests, how to build the ASan tree, and the explicit statement that MSVC offers no thread sanitizer so stress + ASan + review is the ceiling on race evidence here.

- [ ] **Step 5: Commit**

```bash
git add cmake docs/testing-servers.md && git commit -m "test(build): add MMO_ENABLE_ASAN and document the server test strategy"
```

---

## Phase 5 — Performance and Gate Coverage

### Task 10: Stop copying the send buffer on every flush

`Connection::flush()` at `src/shared/network/connection.h:181` does `m_sending = m_sendBuffer;` — a full `std::string` copy of every outgoing byte, on every flush. On the world server's broadcast path that is a copy per packet per recipient.

**Files:**
- Modify: `src/shared/network/connection.h:169-188`
- Modify: `src/shared/game_protocol/game_connection.h` — same pattern if present
- Test: `src/unit_tests/test_network_connection.cpp`

- [ ] **Step 1: Write the test**

Append to `src/unit_tests/test_network_connection.cpp`:

```cpp
// Many packets sent back to back must all arrive, in order. This is the regression guard for
// the buffer swap in flush(): getting the swap wrong loses or duplicates queued bytes, and
// nothing else in the suite sends enough traffic to notice.
TEST_CASE("ConnectionDeliversManyPacketsInOrder", "[network_connection]")
{
	asio::io_service ioService;
	RecordingListener serverListener;
	RecordingListener clientListener;
	ConnectedPair pair = MakeConnectedPair(ioService, serverListener, clientListener);

	const std::size_t packetCount = 200;
	for (std::size_t index = 0; index < packetCount; ++index)
	{
		pair.client->sendSinglePacket([index](auth::OutgoingPacket& packet)
		{
			packet.Start(auth::client_login_packet::LogonChallenge);
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
}
```

- [ ] **Step 2: Run to verify it passes before the change**

```bash
cmake --build build --config Debug -t unit_tests && ./bin/Debug/unit_tests.exe "[network_connection]"
```

Expected: PASS. This is a characterisation test — it must pass both before and after.

- [ ] **Step 3: Replace the copy with a swap**

In `src/shared/network/connection.h`, replace lines 181-182:

```cpp
				// swap, not assign: assigning copies every queued byte, which on a broadcast
				// path is a copy per packet per recipient. After the swap m_sendBuffer holds
				// what m_sending held, which is empty by the flush precondition above.
				m_sending.swap(m_sendBuffer);
				m_sendBuffer.clear();
```

The existing `assert(m_sending.empty())` guard above the swap already establishes the precondition; keep both asserts.

- [ ] **Step 4: Run the tests again**

```bash
cmake --build build --config Debug -t unit_tests && ./bin/Debug/unit_tests.exe "[network_connection]"
```

Expected: PASS, unchanged.

- [ ] **Step 5: Apply the same change to `EncryptedConnection`**

Check `src/shared/game_protocol/game_connection.h` for the equivalent assignment in its `Flush()` and apply the same swap if present.

- [ ] **Step 6: Run the full gate**

```bash
powershell -File tools/gate/verify.ps1
```

Expected: `Gate result: GREEN`. The E2E suite is what exercises the real send path under load.

- [ ] **Step 7: Commit**

```bash
git add src/shared/network/connection.h src/shared/game_protocol/game_connection.h src/unit_tests/test_network_connection.cpp && git commit -m "perf(network): swap rather than copy the send buffer on flush"
```

---

### Task 11: Put `realm_server_tests` in the gate

`tools/gate/verify.ps1:15` lists the build targets and `:97` the test binaries to run. `realm_server_tests` is in neither, so `player_group.cpp`, `friend_mgr.cpp` and `motd_manager.cpp` have tests that nothing runs.

**Files:**
- Modify: `tools/gate/verify.ps1`

- [ ] **Step 1: Add the target**

In `tools/gate/verify.ps1:15`, add `"realm_server_tests"` to the `$Targets` default:

```powershell
	[string[]]$Targets = @("login_server", "realm_server", "world_server", "e2e_client", "unit_tests", "game_server_unit_tests", "login_server_tests", "realm_server_tests")
```

- [ ] **Step 2: Add it to the run list**

In `tools/gate/verify.ps1:97`:

```powershell
		foreach ($test in @("unit_tests", "game_server_unit_tests", "login_server_tests", "realm_server_tests"))
```

- [ ] **Step 3: Run the full gate**

```bash
powershell -File tools/gate/verify.ps1
```

Expected: `Gate result: GREEN`, with a `realm_server_tests` step in `tools/gate/last_report.json`.

- [ ] **Step 4: Commit**

```bash
git add tools/gate/verify.ps1 && git commit -m "test(gate): run realm_server_tests in the quality gate"
```

---

### Task 12: Write down each tier's threading contract

The realm and world servers run `maxNetworkThreads = 0` (`src/realm_server/program.cpp:345`, `src/world_server/program.cpp:219`), so their io work is single-threaded and the ~30 raw-`Player*` cross-session accesses in `player_group.cpp`, `guild_mgr.cpp`, `chat_channel_mgr.cpp` and `friend_mgr.cpp` are safe. Nothing says so. Meanwhile those tiers' managers hold a `std::mutex`, which reads as "this is thread-safe" — it protects the list, never the lifetime of what comes out of it. Raising either tier's thread count without first doing Task 8's work there would be immediately unsafe, and right now nothing warns anyone.

This task adds no behaviour. It records the invariant that the previous eleven tasks relied on.

**Files:**
- Modify: `src/realm_server/player_manager.h`, `src/realm_server/world_manager.h`
- Modify: `src/realm_server/program.cpp:345`, `src/world_server/program.cpp:219`
- Modify: `CLAUDE.md`

- [ ] **Step 1: Document the manager contract**

At the top of `class PlayerManager` in `src/realm_server/player_manager.h`, above the class:

```cpp
	/// Manages all connected players.
	///
	/// **Threading:** the realm server runs its io work on a single thread (see
	/// maxNetworkThreads in program.cpp), and this class is written for that. The mutex below
	/// guards the list against the database worker thread touching it; it does **not** make
	/// the returned Player* safe to hold. A raw pointer taken from here is valid only until
	/// the current handler returns, because the lifetime it points at is owned by this list.
	///
	/// Raising the realm server's thread count therefore requires converting these lookups to
	/// shared_ptr and routing cross-session calls through AbstractConnection::Post first --
	/// the same change the login server received. Do not raise it without that.
```

Add the equivalent to `src/realm_server/world_manager.h`.

- [ ] **Step 2: Document the thread count at its source**

In `src/realm_server/program.cpp:345`, replace the bare declaration:

```cpp
		// Single-threaded on purpose. Cross-session access throughout this server (player
		// groups, guilds, chat channels, friends) is done through raw Player pointers taken
		// from the managers, which is only sound while one thread runs all of it. See the
		// threading note on PlayerManager before changing this.
		const auto maxNetworkThreads = 0u;
```

Add the same comment at `src/world_server/program.cpp:219`.

- [ ] **Step 3: Record it in CLAUDE.md**

In the "Client Threading" section of `CLAUDE.md`, replace the sentence `Servers stay single-threaded; the TaskSystem is never initialized there.` with:

```markdown
Servers never initialize the TaskSystem. The realm and world servers additionally run all
io work on one thread and depend on it — their cross-session access uses raw pointers taken
from the session managers. The login server runs two io threads, so anything reaching across
sessions there goes through `AbstractConnection::Post` onto the target's strand. See
[docs/testing-servers.md](docs/testing-servers.md).
```

- [ ] **Step 4: Commit**

```bash
git add src/realm_server src/world_server CLAUDE.md && git commit -m "docs(servers): record each tier's threading contract"
```

---

## Final Verification

Before considering the branch done:

- [ ] `powershell -File tools/gate/verify.ps1` is green, and `tools/gate/last_report.json` matches HEAD with `e2e_skipped: false`.
- [ ] The ASan stress run from Task 9 Step 3 passes five consecutive times.
- [ ] The manual Ctrl+C check from Task 7 Step 7 passes for all three servers, with a client connected.
- [ ] `cmake --build build --config Debug` (full, client included) succeeds — the shared network layer is client code too.
- [ ] A soak: bring the E2E stack up, run `tools/e2e/e2e_run.ps1` three times back to back without tearing down between runs, and confirm no growth in the servers' working set and no errors in `logs/*.log`.
- [ ] `/gate` then `/ship`.

---

## Execution Log — What Actually Differed

Recorded after the fact, because several of these are the sort of thing the next person will
otherwise rediscover the hard way.

**Task ordering changed.** `AbstractConnection::Post` was implemented *before* reviving the
session lookup, not after. Reviving `IsAuthenticated` is what makes `KickPlayerByAccountId`
reachable at all, so doing it first would have left one commit in the branch containing a live
cross-thread use-after-free. Task 4 and Task 8 landed together for the same reason.

**A crash was introduced and caught.** The Task 3 receive-buffer guard originally copied the
existing `m_socket.reset()` teardown idiom in `EncryptedConnection`. That made a latent bug
reachable: `Disconnected()` dereferenced `m_socket` unchecked, so an in-flight write completing
after a buffer-overrun drop faulted. On the realm's client-facing port that is remotely
triggerable — it would have traded a memory DoS for a crash DoS. Confirmed as an access
violation by `GameConnectionSurvivesOverrunWithWriteInFlight`, then fixed two ways (close
rather than release; null-check as backstop). Neither the unit suite nor 14/14 E2E caught it;
re-reading the diff did.

**The first reproduction attempt passed.** Tearing down from inside the read handler leaves no
read outstanding to fault. Reaching the actual hazard required a *write* in flight. A test that
passes is not evidence until you know which interleaving it exercises.

**Graceful shutdown needed far more than a signal handler.** Task 7 assumed installing the
handler and closing connections was the work. It was not: three objects held outstanding io
work that kept every service alive regardless — `web::WebService`'s acceptor,
`TimerQueue`'s armed timer (note `Countdown::Cancel()` does *not* cancel it), and
`WorldInstanceManager`'s self-rearming 30ms tick. Plus the realm server's `dbTimerQueue`, on
the database service. All found by `tools/shutdown_check.py`, none by reasoning.

**The plan wrote off automating the shutdown check. That was wrong.** It concluded that because
the E2E harness cannot signal its servers, manual Ctrl+C was the ceiling. The harness limitation
is real; the conclusion was not. Python's `subprocess` exposes `CREATE_NEW_PROCESS_GROUP`, which
allows targeting one process with `CTRL_BREAK_EVENT` without touching the parent console. That
tool found three bugs the unit test could not, because the unit test verifies the handler in
isolation and knows nothing about what else in a real program holds work.

**Two extra login-server races were found en route**, both the same shape as the kick:
`Realm::NotifyAccountBanned` wrote the send buffer from the REST thread, and `m_requirements`
was replaced by the database result dispatcher while read from a player's strand.

**The ASan build collided with the normal one.** Output goes to the source tree, so `bin/` and
`lib/` were shared between build directories; mixing instrumented and uninstrumented objects
produced `LNK2038: mismatch detected for 'annotate_string'`. Fixed by suffixing the sanitizer
tree's output directories rather than documenting the hazard.

**Dropped deliberately:** the `ban_disconnects_session.lua` E2E scenario. The login-server
session exists only between auth and realm-list, so a scenario would race the handoff to catch
it — flaky, and mostly re-testing REST plumbing that `test_http_handlers.cpp` already covers.
The two lifecycle unit tests cover the mechanism and were verified to fail without the fix.

**Environmental:** the machine's pagefile (10 GB, fully saturated) cannot support MSVC's default
parallelism on this solution; builds need `-- /m:4`. A failed build leaves orphaned `cl.exe`
processes that must be cleared before retrying.
