// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "http/http_client.h"
#include "http/http_outgoing_answer.h"

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

		private:

			WebService &m_webService;
			std::shared_ptr<Client> m_connection;
		};
	}
}
