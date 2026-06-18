// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "web_services/web_client.h"
#include "login_http_handlers.h"

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

		/// Creates a web client handler for the login server API.
		explicit WebClient(
		    WebService &webService,
		    std::shared_ptr<Client> connection);

	public:

		/// Handles an incoming HTTP request.
		virtual void handleRequest(const net::http::IncomingRequest &request, web::WebResponse &response) override;

	private:

		/// Handles a shutdown request.
		void handleShutdown(const net::http::IncomingRequest& request, web::WebResponse& response) const;

	private:
		WebService& m_service;
		LoginHttpHandlers m_handlers;
	};
}
