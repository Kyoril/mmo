// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "http/http_client.h"
#include "http/http_incoming_request.h"
#include "http/http_outgoing_answer.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace mmo
{
	namespace web
	{
		class WebService;

		typedef net::http::OutgoingAnswer WebResponse;

		class WebClient
			: public net::http::IClientListener
		{
		public:

			typedef net::http::Client Client;
			using RouteHandler = std::function<void(const net::http::IncomingRequest&, WebResponse&)>;

			explicit WebClient(
			    WebService &webService,
			    std::shared_ptr<Client> connection);
			virtual ~WebClient();

			virtual void connectionLost();
			virtual void connectionMalformedPacket();
			virtual PacketParseResult connectionPacketReceived(net::http::IncomingRequest &packet);

			WebService &getService() const;
			Client &getConnection() const;

			virtual void handleRequest(const net::http::IncomingRequest &request,
			                           WebResponse &response) = 0;

		protected:

			/// Registers a handler for the given HTTP method and path.
			void RegisterRoute(net::http::IncomingRequest::Type method, std::string path, RouteHandler handler);

			/// Dispatches the request to a registered route handler, or sends a 404 response.
			void DispatchRoute(const net::http::IncomingRequest& request, WebResponse& response);

		private:

			WebService &m_webService;
			std::shared_ptr<Client> m_connection;
			std::unordered_map<std::string, RouteHandler> m_routes;
		};
	}
}
