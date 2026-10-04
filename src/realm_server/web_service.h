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
	class MOTDManager;
	class TimeOfDayManager;
	class WorldManager;

	class WebService 
		: public web::WebService
	{
	public:

		/// Creates a web service for realm server administration.
		explicit WebService(
		    asio::io_service &service,
		    uint16 port,
		    String password,
		    PlayerManager &playerManager,
			IDatabase &database,
			MOTDManager &motdManager,
			TimeOfDayManager &timeOfDayManager
		);

		/// Gets the player manager.
		PlayerManager &GetPlayerManager() const { return m_playerManager; }
		/// Gets the database instance.
		IDatabase &GetDatabase() const { return m_database; }
		/// Gets the MOTD manager instance.
		MOTDManager &GetMOTDManager() const { return m_motdManager; }
		/// Gets the realm-wide time of day manager.
		TimeOfDayManager &GetTimeOfDayManager() const { return m_timeOfDayManager; }
		/// Returns the time this service started.
		GameTime GetStartTime() const;
		/// Gets the configured admin password.
		const String &GetPassword() const;

		/// Sets the world manager (needed to relay subsystem toggles to world nodes).
		void SetWorldManager(WorldManager &worldManager) { m_worldManager = &worldManager; }
		/// Gets the world manager, or nullptr if none was set.
		WorldManager *GetWorldManager() const { return m_worldManager; }

		/// Creates a web client instance for a new connection.
		virtual web::WebService::WebClientPtr createClient(std::shared_ptr<Client> connection) override;

	private:

		PlayerManager &m_playerManager;
		IDatabase &m_database;
		MOTDManager &m_motdManager;
		TimeOfDayManager &m_timeOfDayManager;
		const GameTime m_startTime;
		const String m_password;
		WorldManager *m_worldManager = nullptr;
	};
}
