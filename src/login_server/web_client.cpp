// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "web_client.h"

#include "web_service.h"
#include "base/clock.h"
#include "http/http_incoming_request.h"
#include "log/default_log_levels.h"

#include "nlohmann/json.hpp"

// Add a namespace alias for convenience
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

		const auto &url = request.getPath();
		switch(request.getType())
		{
			case net::http::IncomingRequest::Get:
			{
				if (url == "/uptime")
				{
					const GameTime startTime = static_cast<WebService &>(getService()).GetStartTime();

					json jsonResponse;
					jsonResponse["uptime"] = gameTimeToSeconds<unsigned>(GetAsyncTimeMs() - startTime);
					SendJsonResponse(response, jsonResponse);
				}
				else if (url == "/gm-level")
				{
					m_handlers.HandleGetGMLevel(request, response);
				}
				else if (url == "/accounts")
				{
					m_handlers.HandleListAccounts(request, response);
				}
				else if (url == "/realms")
				{
					m_handlers.HandleListRealms(request, response);
				}
				else
				{
					response.setStatus(net::http::OutgoingAnswer::NotFound);

					const String message = "The command '" + url + "' does not exist";
					response.finishWithContent("text/html", message.data(), message.size());
				}
				break;
			}
			case net::http::IncomingRequest::Post:
			{
				// Parse arguments
				if (url == "/shutdown")
				{
					handleShutdown(request, response);
				}
				else if (url == "/create-account")
				{
					m_handlers.HandleCreateAccount(request, response);
				}
				else if (url == "/create-realm")
				{
					m_handlers.HandleCreateRealm(request, response);
				}
				else if (url == "/ban-account")
				{
					m_handlers.HandleBanAccount(request, response);
				}
				else if (url == "/unban-account")
				{
					m_handlers.HandleUnbanAccount(request, response);
				}
				else if (url == "/gm-level")
				{
					m_handlers.HandleSetGMLevel(request, response);
				}
				else
				{
					const String message = "The command '" + url + "' does not exist";
					response.finishWithContent("text/html", message.data(), message.size());
				}
				break;
			}
			default:
			{
				break;
			}
		}
	}

	void WebClient::handleShutdown(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		ILOG("Shutting down..");
		response.finish();

		auto& ioService = getService().getIOService();
		ioService.stop();
	}
}
