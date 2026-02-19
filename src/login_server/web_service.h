// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "web_services/web_service.h"
#include "web_client.h"
#include "base/constants.h"
#include "base/clock.h"

namespace mmo
{
	class RealmManager;
	class PlayerManager;
	struct IDatabase;

	class WebService 
		: public web::WebService
	{
	public:

		/// Creates a web service for login server administration.
		explicit WebService(
		    asio::io_service &service,
		    uint16 port,
		    String password,
		    PlayerManager &playerManager,
			RealmManager &realmManager,
			IDatabase &database
		);

		/// Gets the player manager.
		PlayerManager &GetPlayerManager() const { return m_playerManager; }
		/// Gets the realm manager.
		RealmManager& GetRealmManager() const { return m_realmManager; }
		/// Gets the database instance.
		IDatabase &GetDatabase() const { return m_database; }
		/// Returns the time this service started.
		GameTime GetStartTime() const;
		/// Gets the configured admin password.
		const String &GetPassword() const;

		/// Creates a web client instance for a new connection.
		virtual web::WebService::WebClientPtr createClient(std::shared_ptr<Client> connection) override;

	private:

		PlayerManager &m_playerManager;
		RealmManager& m_realmManager;
		IDatabase &m_database;
		const GameTime m_startTime;
		const String m_password;
	};
}
