// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_context.h"
#include "bot_login_connector.h"
#include "bot_movement_controller.h"
#include "bot_nav_service.h"
#include "bot_realm_connector.h"
#include "bot_startup_types.h"

#include "base/typedefs.h"

#include "asio/io_service.hpp"

#include <memory>
#include <optional>

namespace mmo
{
	/// Exit codes of the e2e_client, kept stable so that agents and scripts can rely on them.
	namespace e2e_exit_code
	{
		enum Type
		{
			Success = 0,
			ScenarioFailed = 1,
			SetupFailed = 2,
			Timeout = 3,
			Disconnected = 4,
		};
	}

	/// Headless client session: login -> realm -> character -> enter world, then hand
	/// control to the caller-provided world loop (smoke test or Lua scenario).
	class E2eSession final
	{
	public:
		explicit E2eSession(BotConfig config);

		/// Starts the async login flow. Returns false on immediately detectable config errors.
		bool Start();

		/// Pumps the io service until the character is in the world or the timeout elapses.
		e2e_exit_code::Type WaitForWorld(uint32 timeoutSeconds);

		/// Keeps the session alive for the given duration, verifying the connection survives
		/// (time sync responses, spawn handling). Used by --smoke.
		e2e_exit_code::Type RunSmoke(uint32 seconds);

		/// Runs one io service iteration followed by a short sleep. The single pump primitive
		/// every blocking scenario call is built on.
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

		void ClearLastSpellVisual() const { m_realm->ClearLastSpellVisual(); }

		/// Runs the whole login -> realm -> character -> world flow again over the same connector
		/// objects, exactly as the game client does when a displaced player logs back in.
		///
		/// Reusing the connectors is the point: they carry session state that has to be cleared
		/// for the new session, and a fresh object would not test that. Call after the previous
		/// session has ended.
		///
		/// @return Success once the character is back in the world.
		e2e_exit_code::Type Reconnect(uint32 timeoutSeconds);

		BotContext& GetContext() { return *m_context; }
		BotRealmConnector& GetRealm() { return *m_realm; }
		BotMovementController& GetMovementController() { return m_movementController; }
		asio::io_service& GetIoService() { return m_io; }
		const BotConfig& GetConfig() const { return m_config; }
		bool IsWorldReady() const { return m_worldReady; }
		bool IsStopRequested() const { return m_stopRequested; }
		e2e_exit_code::Type GetExitCode() const { return m_exitCode; }

	private:
		void Fail(e2e_exit_code::Type code);
		void BindSignals();

	private:
		BotConfig m_config;
		asio::io_service m_io;
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
		e2e_exit_code::Type m_exitCode { e2e_exit_code::Success };
	};
}
