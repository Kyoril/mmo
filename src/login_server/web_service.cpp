// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "web_service.h"

namespace mmo
{
	WebService::WebService(
	    asio::io_service &service,
	    uint16 port,
	    String password,
	    PlayerManager &playerManager,
		RealmManager& realmManager,
		IDatabase &database
	)
		: web::WebService(service, port)
		, m_playerManager(playerManager)
		, m_realmManager(realmManager)
		, m_database(database)
		, m_startTime(GetAsyncTimeMs())
		, m_password(std::move(password))
	{
	}

	GameTime WebService::GetStartTime() const
	{
		return m_startTime;
	}

	const String &WebService::GetPassword() const
	{
		return m_password;
	}

	web::WebService::WebClientPtr WebService::createClient(std::shared_ptr<Client> connection)
	{
		return std::make_shared<WebClient>(*this, connection);
	}
}
