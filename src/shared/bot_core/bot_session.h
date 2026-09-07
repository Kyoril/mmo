// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_context.h"
#include "bot_login_connector.h"
#include "bot_movement_controller.h"
#include "bot_nav_service.h"
#include "bot_realm_connector.h"
#include "bot_exit_code.h"
#include "bot_startup_types.h"

#include "base/typedefs.h"

#include "asio/io_service.hpp"

#include <memory>
#include <optional>

namespace mmo
{
	/// Headless client session: login -> realm -> character -> enter world, then hand
	/// control to the caller-provided world loop (smoke test, Lua scenario or bot AI).
	///
	/// The session does not own its io service or its navigation service, so that a swarm host
	/// can run many sessions on one io service and one shared set of navigation meshes. See
	/// bot_core/README.md for why sharing the navigation service is mandatory rather than an
	/// optimisation.
	class BotSession final
	{
	public:
		/// @param io The io service every connector of this session runs on. Must outlive the session.
		/// @param navService Navigation service shared with every other session in this process.
		/// @param config Account, realm and character configuration of this bot.
		BotSession(asio::io_service& io, std::shared_ptr<BotNavService> navService, BotConfig config);

		/// Starts the async login flow. Returns false on immediately detectable config errors.
		bool Start();

		/// Pumps the io service until the character is in the world or the timeout elapses.
		bot_exit_code::Type WaitForWorld(uint32 timeoutSeconds);

		/// Keeps the session alive for the given duration, verifying the connection survives
		/// (time sync responses, spawn handling). Used by --smoke.
		bot_exit_code::Type RunSmoke(uint32 seconds);

		/// Advances everything this session simulates on its own, most importantly path
		/// following. Called once per frame by the host; never polls the io service and never
		/// sleeps, so that a host driving many sessions pays for one poll and one sleep in total.
		void Update();

		/// Polls the io service, advances this session and sleeps for a short while. The single
		/// pump primitive every blocking scenario call is built on.
		///
		/// Only valid for a host running exactly one session: the sleep is per call, so a swarm
		/// pumping N sessions this way would take N sleeps per frame. Swarm hosts poll the io
		/// service themselves and call Update instead.
		void Pump();

		void Shutdown();

		/// Tells the session that losing the realm connection is the expected outcome, so that it
		/// is recorded rather than failing the scenario. Used by scenarios that deliberately
		/// provoke a disconnect, such as the duplicate-login check.
		void ExpectDisconnect() { m_disconnectExpected = true; }

		/// Whether the realm connection has been lost after ExpectDisconnect was called.
		[[nodiscard]] bool IsDisconnected() const { return m_disconnected; }

		/// Why the realm terminated this session, if it said so before closing the connection.
		[[nodiscard]] std::optional<auth::SessionKickReason> GetKickReason() const { return m_realm->GetKickReason(); }

		/// The most recent spell visualization the server told this client to play (0 = none).
		[[nodiscard]] uint32 GetLastSpellVisualId() const { return m_realm->GetLastSpellVisualId(); }

		[[nodiscard]] uint64 GetLastSpellVisualTarget() const { return m_realm->GetLastSpellVisualTarget(); }

		[[nodiscard]] uint8 GetLastSpellVisualEvent() const { return m_realm->GetLastSpellVisualEvent(); }

		void ClearLastSpellVisual() { m_realm->ClearLastSpellVisual(); }

		/// Runs the whole login -> realm -> character -> world flow again over the same connector
		/// objects, exactly as the game client does when a displaced player logs back in.
		///
		/// Reusing the connectors is the point: they carry session state that has to be cleared
		/// for the new session, and a fresh object would not test that. Call after the previous
		/// session has ended.
		///
		/// @return Success once the character is back in the world.
		bot_exit_code::Type Reconnect(uint32 timeoutSeconds);

		BotContext& GetContext() { return *m_context; }
		BotRealmConnector& GetRealm() { return *m_realm; }
		BotMovementController& GetMovementController() { return m_movementController; }
		asio::io_service& GetIoService() { return m_io; }
		const BotConfig& GetConfig() const { return m_config; }
		bool IsWorldReady() const { return m_worldReady; }
		bool IsStopRequested() const { return m_stopRequested; }
		bot_exit_code::Type GetExitCode() const { return m_exitCode; }

	private:
		void Fail(bot_exit_code::Type code);
		void BindSignals();

	private:
		BotConfig m_config;
		asio::io_service& m_io;
		std::shared_ptr<BotLoginConnector> m_login;
		std::shared_ptr<BotRealmConnector> m_realm;
		std::shared_ptr<BotNavService> m_navService;
		std::shared_ptr<BotContext> m_context;
		BotMovementController m_movementController;
		bool m_stopRequested { false };
		bool m_worldReady { false };
		bool m_realmConnectionAttempted { false };
		bool m_shuttingDown { false };
		bool m_disconnectExpected { false };
		bool m_disconnected { false };
		bot_exit_code::Type m_exitCode { bot_exit_code::Success };
	};
}
