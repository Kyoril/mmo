// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

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

		explicit WebClient(
		    WebService &webService,
		    std::shared_ptr<Client> connection);

	public:

		virtual void handleRequest(const net::http::IncomingRequest &request, web::WebResponse &response) override;

	private:

		void handleShutdown(const net::http::IncomingRequest& request, web::WebResponse& response) const;
		void handleCreateWorld(const net::http::IncomingRequest& request, web::WebResponse& response) const;

	private:
		WebService& m_service;
		
	};
}
