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
		void HandleListAccounts(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		void HandleListRealms(const net::http::IncomingRequest& request, web::WebResponse& response) const;

		// Account feature endpoints.
		void HandleListFeatures(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		void HandleCreateFeature(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		void HandleDeleteFeature(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		void HandleGrantFeature(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		void HandleRevokeFeature(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		void HandleGetAccountFeatures(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		void HandleSetRealmFeatureRequirement(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		void HandleRemoveRealmFeatureRequirement(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		void HandleGetRealmFeatureRequirements(const net::http::IncomingRequest& request, web::WebResponse& response) const;

		// Dashboard statistics endpoints.
		void HandleGetStatsSummary(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		void HandleGetStatsTimeSeries(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		void HandleGetStatsRecentActivity(const net::http::IncomingRequest& request, web::WebResponse& response) const;

	private:
		IDatabase& m_database;
		RealmManager& m_realmManager;
		PlayerManager& m_playerManager;
	};
}
