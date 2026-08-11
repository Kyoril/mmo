// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "secondary_login_session.h"

#include "log/default_log_levels.h"

namespace mmo
{
	SecondaryLoginSession::SecondaryLoginSession(const std::string& host, const uint16 port, std::string username, std::string password)
		: m_io()
		, m_login(std::make_shared<BotLoginConnector>(m_io, host, port))
		, m_username(std::move(username))
		, m_password(std::move(password))
	{
		m_login->AuthenticationResult.connect([this](const auth::AuthResult result)
			{
				m_result = result;

				if (result != auth::auth_result::Success)
				{
					ELOG("Second session failed to authenticate at the login server, code " << static_cast<int32>(result));
					m_failed = true;
					return;
				}

				ILOG("Second session authenticated at the login server.");
				m_authenticated = true;
			});
	}

	void SecondaryLoginSession::Start()
	{
		ILOG("Opening a second login session for account " << m_username);
		m_login->Connect(m_username, m_password);
	}

	void SecondaryLoginSession::Pump()
	{
		m_io.poll();
	}

	void SecondaryLoginSession::Shutdown()
	{
		m_login->close();
		m_io.poll();
	}
}
