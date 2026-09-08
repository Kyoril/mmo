// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "bot_login_connector.h"

#include "base/typedefs.h"

#include "asio/io_service.hpp"

#include <memory>
#include <string>

namespace mmo
{
	/// A second, login-server-only session for the same account, used by scenarios that need to
	/// observe what happens to the first one when an account is signed in again.
	///
	/// It deliberately stops at the login server: the duplicate-login displacement fires as soon as
	/// LogonProof succeeds, so there is nothing to gain from picking a realm and a character -- and
	/// not doing so keeps the scenario fast and free of the character-creation machinery.
	///
	/// Owns its own io service. Scenarios drive it through Pump() alongside the main session, so no
	/// extra thread is involved.
	class SecondaryLoginSession final
	{
	public:
		explicit SecondaryLoginSession(const std::string& host, uint16 port, std::string username, std::string password);

		/// Starts the async login flow.
		void Start();

		/// Runs one io service iteration. Does not sleep -- the caller is already pacing itself by
		/// pumping the main session.
		void Pump();

		/// Whether the login server has accepted this session's credentials.
		[[nodiscard]] bool IsAuthenticated() const { return m_authenticated; }

		/// Whether the login server rejected this session, so callers can stop waiting.
		[[nodiscard]] bool HasFailed() const { return m_failed; }

		/// Closes the connection. Called from the destructor so a scenario cannot leave the socket
		/// open behind it.
		void Shutdown();

		~SecondaryLoginSession();

	private:
		asio::io_service m_io;
		std::shared_ptr<BotLoginConnector> m_login;
		std::string m_username;
		std::string m_password;
		bool m_authenticated { false };
		bool m_failed { false };
	};
}
