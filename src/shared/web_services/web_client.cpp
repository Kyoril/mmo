// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "web_client.h"
#include "web_service.h"
#include "base/clock.h"
#include "log/default_log_levels.h"
#include "base/typedefs.h"

namespace mmo
{
	namespace web
	{
		namespace
		{
			std::string MakeRouteKey(net::http::IncomingRequest::Type method, const std::string& path)
			{
				switch (method)
				{
					case net::http::IncomingRequest::Get:  return "GET " + path;
					case net::http::IncomingRequest::Post: return "POST " + path;
					default:                               return "UNKNOWN " + path;
				}
			}
		}
		WebClient::WebClient(WebService &webService, std::shared_ptr<Client> connection)
			: m_webService(webService)
			, m_connection(connection)
		{
			m_connection->setListener(*this);
		}

		WebClient::~WebClient()
		{
		}

		void WebClient::connectionLost()
		{
			m_webService.clientDisconnected(*this);
		}

		void WebClient::connectionMalformedPacket()
		{
			//WLOG("Bad HTTP request");
			m_webService.clientDisconnected(*this);
		}

		PacketParseResult WebClient::connectionPacketReceived(net::http::IncomingRequest &packet)
		{
			io::StringSink sink(m_connection->getSendBuffer());
			WebResponse response(sink);
			if (packet.getType() == net::http::IncomingRequest::Options)
			{
				response.addHeader("Access-Control-Allow-Origin", "*");
				response.addHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
				response.addHeader("Access-Control-Allow-Max-Age", "1000");
				response.addHeader("Access-Control-Allow-Headers", "origin, x-csrftoken, content-type, accept, authentication");
			}
			response.addHeader("Connection", "close");
			handleRequest(packet, response);
			m_connection->flush();

			return PacketParseResult::Pass;
		}

		WebService &WebClient::getService() const
		{
			return m_webService;
		}

		WebClient::Client &WebClient::getConnection() const
		{
			assert(m_connection);
			return *m_connection;
		}

		void WebClient::RegisterRoute(net::http::IncomingRequest::Type method, std::string path, RouteHandler handler)
		{
			m_routes.emplace(MakeRouteKey(method, std::move(path)), std::move(handler));
		}

		void WebClient::DispatchRoute(const net::http::IncomingRequest& request, WebResponse& response)
		{
			const auto key = MakeRouteKey(request.getType(), request.getPath());
			const auto it = m_routes.find(key);
			if (it != m_routes.end())
			{
				it->second(request, response);
			}
			else
			{
				response.setStatus(net::http::OutgoingAnswer::NotFound);
				const String message = "The command '" + request.getPath() + "' does not exist";
				response.finishWithContent("text/html", message.data(), message.size());
			}
		}
	}
}
