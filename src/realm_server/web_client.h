// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "web_services/web_client.h"

namespace mmo
{
	class AsyncDatabase;
	struct IDatabase;
	class WebService;

	class WebClient
		: public web::WebClient
		, public std::enable_shared_from_this<WebClient>
	{
	public:

		/// Creates a web client handler for the realm server API.
		explicit WebClient(
		    WebService &webService,
		    std::shared_ptr<Client> connection);

	public:

		/// Handles an incoming HTTP request.
		virtual void handleRequest(const net::http::IncomingRequest &request, web::WebResponse &response) override;

	private:

		/// Handles a shutdown request.
		void handleShutdown(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		/// Handles a world creation request.
		void handleCreateWorld(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		/// Handles a Message of the Day query.
		void handleGetMotd(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		/// Handles a Message of the Day update.
		void handleSetMotd(const net::http::IncomingRequest& request, web::WebResponse& response) const;

		/// GET /time-of-day: reports the realm-wide time of day, the system time and the override offset.
		void handleGetTimeOfDay(const net::http::IncomingRequest& request, web::WebResponse& response) const;

		/// POST /time-of-day: sets the time of day ('time=HH:MM[:SS]') or returns to the system time
		/// ('reset=1'), with an optional client transition length in seconds ('transition').
		void handleSetTimeOfDay(const net::http::IncomingRequest& request, web::WebResponse& response) const;

	private:
		WebService& m_service;
	};
}
