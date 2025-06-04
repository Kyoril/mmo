// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "web_client.h"

#include "database.h"
#include "motd_manager.h"
#include "web_service.h"
#include "base/clock.h"
#include "http/http_incoming_request.h"
#include "player_manager.h"
#include "player.h"
#include "log/default_log_levels.h"

#include "nlohmann/json.hpp"

// Add a namespace alias for convenience
using json = nlohmann::json;

namespace mmo
{
	namespace
	{
		void SendJsonResponse(web::WebResponse& response, const json& jsonObject)
		{
			const std::string jsonStr = jsonObject.dump();
			response.finishWithContent("application/json", jsonStr.data(), jsonStr.size());
		}
	}

	WebClient::WebClient(WebService &webService, std::shared_ptr<Client> connection)
		: web::WebClient(webService, connection)
		, m_service(webService)
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
			respondUnauthorized(response, "MMO Realm");
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
				else if (url == "/motd")
				{
					handleGetMotd(request, response);
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
				else if (url == "/create-world")
				{
					handleCreateWorld(request, response);
				}
				else if (url == "/motd")
				{
					handleSetMotd(request, response);
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

	void WebClient::handleShutdown(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		ILOG("Shutting down..");
		response.finish();

		auto& ioService = getService().getIOService();
		ioService.stop();
	}

	std::pair<BigNumber, BigNumber> calculateSV(String& id, String& password)
	{
		std::transform(id.begin(), id.end(), id.begin(), ::toupper);
		std::transform(password.begin(), password.end(), password.begin(), ::toupper);

		// Calculate auth hash
		const std::string authString = id + ":" + password;
		const SHA1Hash authHash = sha1(authString.c_str(), authString.size());

		BigNumber s;
		s.setRand(32 * 8);

		HashGeneratorSha1 gen;

		// Calculate x
		BigNumber x;
		gen.update((const char*)s.asByteArray().data(), s.getNumBytes());
		gen.update((const char*)authHash.data(), authHash.size());
		const SHA1Hash x_hash = gen.finalize();
		x.setBinary(x_hash.data(), x_hash.size());

		// Calculate v
		BigNumber v = constants::srp::g.modExp(x, constants::srp::N);
		return std::make_pair(s, v);
	}
	
	void WebClient::handleCreateWorld(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPostFormArguments();
		const auto accountIdIt = arguments.find("id");
		const auto accountPassIt = arguments.find("password");

		json jsonResponse;

		if (accountIdIt == arguments.end() || accountIdIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'id'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		if (accountPassIt == arguments.end() || accountPassIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'password'";
			SendJsonResponse(response, jsonResponse);
			return;
		}
		
		// Generate salt and verifier data
		String id = accountIdIt->second;
		String password = accountPassIt->second;
		const auto [s, v] = calculateSV(id, password);

		// Execute
		if (const auto result = m_service.GetDatabase().CreateWorkd(id, s.asHexStr(), v.asHexStr()))
		{
			if (*result == WorldCreationResult::WorldNameAlreadyInUse)
			{
				response.setStatus(net::http::OutgoingAnswer::Conflict);
				jsonResponse["status"] = "WORLD_NAME_ALREADY_IN_USE";
				jsonResponse["message"] = "World name already in use";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			if (*result == WorldCreationResult::Success)
			{
				SendJsonResponse(response, jsonResponse);
				return;
			}
		}

		response.setStatus(net::http::OutgoingAnswer::InternalServerError);
		jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
		SendJsonResponse(response, jsonResponse);
	}
	
	void WebClient::handleGetMotd(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const String& motd = m_service.GetMOTDManager().GetMessageOfTheDay();

		json jsonResponse;
		jsonResponse["message"] = motd;
		SendJsonResponse(response, jsonResponse);
	}
	
	void WebClient::handleSetMotd(const net::http::IncomingRequest& request, web::WebResponse& response) const 
	{
		const auto& arguments = request.getPostFormArguments();
		const auto messageIt = arguments.find("message");

		json jsonResponse;

		if (messageIt == arguments.end())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'message'";
			SendJsonResponse(response, jsonResponse);
			return;
		}
		
		try
		{
			// Update the MOTD through the manager
			// Player notifications are now handled via signal/slot
			if (m_service.GetMOTDManager().SetMessageOfTheDay(messageIt->second))
			{
				jsonResponse["status"] = "SUCCESS";
				jsonResponse["message"] = "MOTD updated successfully";
				SendJsonResponse(response, jsonResponse);
			}
			else
			{
				response.setStatus(net::http::OutgoingAnswer::InternalServerError);
				jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
				jsonResponse["message"] = "Failed to update MOTD";
				SendJsonResponse(response, jsonResponse);
			}
		}
		catch (const std::exception& ex)
		{
			ELOG("Failed to update MOTD: " << ex.what());
			response.setStatus(net::http::OutgoingAnswer::InternalServerError);
			jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
			jsonResponse["message"] = "Failed to update MOTD due to an exception";
			SendJsonResponse(response, jsonResponse);
		}
	}
}
