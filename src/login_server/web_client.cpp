// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "web_client.h"
#include "web_service.h"
#include "base/clock.h"
#include "http/http_incoming_request.h"
#include "player_manager.h"
#include "player.h"
#include "log/default_log_levels.h"

namespace mmo
{
	namespace
	{
		void SendJsonResponse(web::WebResponse &response, const String &json)
		{
			response.finishWithContent("application/json", json.data(), json.size());
		}
	}

	WebClient::WebClient(WebService &webService, std::shared_ptr<Client> connection)
		: web::WebClient(webService, connection)
	{
	}

	void WebClient::handleRequest(const net::http::IncomingRequest &request,
	                              web::WebResponse &response)
	{
		if (!net::http::authorize(request,
		                          [this](const std::string &name, const std::string &password) -> bool
			{
				(void)name;
				const auto &expectedPassword = static_cast<WebService &>(this->getService()).getPassword();
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
					const GameTime startTime = static_cast<WebService &>(getService()).getStartTime();

					std::ostringstream message;
					message << "{\"uptime\":" << gameTimeToSeconds<unsigned>(GetCurrentTime() - startTime) << "}";
					SendJsonResponse(response, message.str());
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
				std::vector<String> arguments;
				std::istringstream stream(request.getPostData());
				std::string argument;
				while(std::getline(stream, argument, '&'))
				{
					arguments.emplace_back(std::move(argument));
				}

				// Parse arguments
				if (url == "/shutdown")
				{
					ILOG("Shutting down..");
					response.finish();

					auto &ioService = getService().getIOService();
					ioService.stop();
				}
				else if (url == "/ban-account")
				{
					// Handle required data
					uint32 accountId = 0;

					for (auto &arg : arguments)
					{
						auto delimiterPos = arg.find('=');
						String argName = arg.substr(0, delimiterPos);
						String argValue = arg.substr(delimiterPos + 1);

						if (argName == "id")
						{
							accountId = atoi(argValue.c_str());
						}
					}

					if (accountId == 0)
					{
						response.setStatus(net::http::OutgoingAnswer::BadRequest);
						SendJsonResponse(response, "{\"status\":\"MISSING_ACCOUNT_DATA\"}");
						break;
					}

					auto &playerMgr = static_cast<WebService &>(this->getService()).getPlayerManager();
					auto *player = playerMgr.getPlayerByAccountID(accountId);
					if (player)
					{
						// Account is currently logged in, so disconnect him
						playerMgr.playerDisconnected(*player);
						break;
					}

					// Check if account exists
					auto &database = static_cast<WebService &>(this->getService()).getDatabase();
					// TODO

					// Succeeded
					SendJsonResponse(response, "");
				}
				else
				{
					const String message = "The command '" + url + "' does not exist";
					response.finishWithContent("text/html", message.data(), message.size());
				}
			}
			default:
			{
				break;
			}
		}
	}
}
