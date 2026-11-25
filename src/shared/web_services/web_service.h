// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "http/http_server.h"

#include <mutex>

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

			/// Stops accepting new HTTP connections and closes the ones already open.
			///
			/// The acceptor holds a pending async_accept for the whole life of the service, which
			/// is outstanding io_service work -- so without this a shutdown that waits for the
			/// service to drain never finishes, no matter what else has been closed.
			void Stop();
			asio::io_service &getIOService() const;

			virtual WebClientPtr createClient(std::shared_ptr<Client> connection) = 0;

		private:

			typedef std::vector<WebClientPtr> Clients;


			asio::io_service &m_ioService;
			net::http::Server m_server;
			Clients m_clients;

			/// Protects m_clients: the accept callback, clientDisconnected() and Stop() each
			/// run on whatever io thread asio happens to dispatch them on, with no strand
			/// tying them together.
			std::mutex m_clientsMutex;
		};
	}
}
