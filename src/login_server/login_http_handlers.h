// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "database.h"
#include "http/http_incoming_request.h"
#include "http/http_outgoing_answer.h"

namespace web
{
	using WebResponse = mmo::net::http::OutgoingAnswer;
}

namespace mmo
{
	class RealmManager;
	class PlayerManager;

	/// Handles all HTTP business-logic endpoints for the login server.
	/// Depends only on IDatabase, RealmManager, and PlayerManager — no networking dependencies.
	class LoginHttpHandlers
	{
	public:
		explicit LoginHttpHandlers(IDatabase& db, RealmManager& realmManager, PlayerManager& playerManager);

	public:
		void HandleCreateAccount(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		void HandleCreateRealm(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		void HandleBanAccount(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		void HandleUnbanAccount(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		void HandleGetGMLevel(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		void HandleSetGMLevel(const net::http::IncomingRequest& request, web::WebResponse& response) const;

	private:
		IDatabase& m_database;
		RealmManager& m_realmManager;
		PlayerManager& m_playerManager;
	};
}
