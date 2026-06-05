// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "web_client.h"

#include "web_service.h"
#include "base/clock.h"
#include "http/http_incoming_request.h"
#include "http/http_outgoing_answer.h"
#include "log/default_log_levels.h"

#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace mmo
{
	namespace
	{
		void SendJsonResponse(web::WebResponse &response, const json &jsonObject)
		{
			const std::string jsonStr = jsonObject.dump();
			response.finishWithContent("application/json", jsonStr.data(), jsonStr.size());
		}
	}

	WebClient::WebClient(WebService &webService, std::shared_ptr<Client> connection)
		: web::WebClient(webService, connection)
		, m_service(webService)
		, m_handlers(webService.GetDatabase(), webService.GetRealmManager(), webService.GetPlayerManager())
	{
		using Type = net::http::IncomingRequest::Type;

		RegisterRoute(Type::Get, "/uptime", [this](const net::http::IncomingRequest&, web::WebResponse& response)
		{
			const GameTime startTime = static_cast<WebService &>(getService()).GetStartTime();
			json jsonResponse;
			jsonResponse["uptime"] = gameTimeToSeconds<unsigned>(GetAsyncTimeMs() - startTime);
			SendJsonResponse(response, jsonResponse);
		});

		RegisterRoute(Type::Get, "/gm-level", [this](const net::http::IncomingRequest& req, web::WebResponse& response)
		{
			m_handlers.HandleGetGMLevel(req, response);
		});

		RegisterRoute(Type::Get, "/accounts", [this](const net::http::IncomingRequest& req, web::WebResponse& response)
		{
			m_handlers.HandleListAccounts(req, response);
		});

		RegisterRoute(Type::Get, "/realms", [this](const net::http::IncomingRequest& req, web::WebResponse& response)
		{
			m_handlers.HandleListRealms(req, response);
		});

		RegisterRoute(Type::Post, "/shutdown", [this](const net::http::IncomingRequest& req, web::WebResponse& response)
		{
			handleShutdown(req, response);
		});

		RegisterRoute(Type::Post, "/create-account", [this](const net::http::IncomingRequest& req, web::WebResponse& response)
		{
			m_handlers.HandleCreateAccount(req, response);
		});

		RegisterRoute(Type::Post, "/create-realm", [this](const net::http::IncomingRequest& req, web::WebResponse& response)
		{
			m_handlers.HandleCreateRealm(req, response);
		});

		RegisterRoute(Type::Post, "/ban-account", [this](const net::http::IncomingRequest& req, web::WebResponse& response)
		{
			m_handlers.HandleBanAccount(req, response);
		});

		RegisterRoute(Type::Post, "/unban-account", [this](const net::http::IncomingRequest& req, web::WebResponse& response)
		{
			m_handlers.HandleUnbanAccount(req, response);
		});

		RegisterRoute(Type::Post, "/gm-level", [this](const net::http::IncomingRequest& req, web::WebResponse& response)
		{
			m_handlers.HandleSetGMLevel(req, response);
		});
	}

	void WebClient::handleRequest(const net::http::IncomingRequest &request,
	                              web::WebResponse &response)
	{
		if (!net::http::authorize(request,
		                          [this](const std::string &name, const std::string &password) -> bool
			{
				(void)name;
				const auto &expectedPassword = static_cast<WebService &>(this->getService()).GetPassword();
				return (expectedPassword == password);
			}))
		{
			respondUnauthorized(response, "MMO Login");
			return;
		}

		DispatchRoute(request, response);
	}

	void WebClient::handleShutdown(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		ILOG("Shutting down..");
		response.finish();

		auto& ioService = getService().getIOService();
		ioService.stop();
	}
}
