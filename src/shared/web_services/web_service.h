// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "http/http_server.h"

namespace mmo
{
	namespace web
	{
		class WebClient;

		class WebService
		{
		public:

			typedef std::shared_ptr<WebClient> WebClientPtr;
			typedef net::http::Client Client;

			explicit WebService(
			    asio::io_service &service,
			    uint16 port
			);
			~WebService();

			void clientDisconnected(WebClient &client);
			asio::io_service &getIOService() const;

			virtual WebClientPtr createClient(std::shared_ptr<Client> connection) = 0;

		private:

			typedef std::vector<WebClientPtr> Clients;


			asio::io_service &m_ioService;
			net::http::Server m_server;
			Clients m_clients;
		};
	}
}
