// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "web_services/web_service.h"
#include "web_client.h"
#include "base/constants.h"
#include "base/clock.h"

namespace mmo
{
	class PlayerManager;
	struct IDatabase;

	class WebService 
		: public web::WebService
	{
	public:

		explicit WebService(
		    asio::io_service &service,
		    uint16 port,
		    String password,
		    PlayerManager &playerManager,
			IDatabase &database
		);

		PlayerManager &GetPlayerManager() const { return m_playerManager; }
		IDatabase &GetDatabase() const { return m_database; }
		GameTime GetStartTime() const;
		const String &GetPassword() const;

		virtual web::WebService::WebClientPtr createClient(std::shared_ptr<Client> connection) override;

	private:

		PlayerManager &m_playerManager;
		IDatabase &m_database;
		const GameTime m_startTime;
		const String m_password;
	};
}
