// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "web_services/web_client.h"

namespace mmo
{
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

	};
}
