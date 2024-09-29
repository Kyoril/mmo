// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "web_client.h"

#include "database.h"
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
					message << "{\"uptime\":" << gameTimeToSeconds<unsigned>(GetAsyncTimeMs() - startTime) << "}";
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
				// Parse arguments
				if (url == "/shutdown")
				{
					handleShutdown(request, response);
				}
				else if (url == "/create-account")
				{
					handleCreateAccount(request, response);
				}
				else if (url == "/create-realm")
				{
					handleCreateRealm(request, response);
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

	void WebClient::handleCreateAccount(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPostFormArguments();
		const auto accountIdIt = arguments.find("id");
		const auto accountPassIt = arguments.find("password");

		if (accountIdIt == arguments.end())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			SendJsonResponse(response, "{\"status\":\"MISSING_PARAMETER\", \"message\":\"Missing parameter 'id'\"}");
			return;
		}

		if (accountPassIt == arguments.end())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			SendJsonResponse(response, "{\"status\":\"MISSING_PARAMETER\", \"message\":\"Missing parameter 'password'\"}");
			return;
		}

		// Generate salt and verifier data
		String id = accountIdIt->second;
		String password = accountPassIt->second;
		const auto [s, v] = calculateSV(id, password);
		
		// Execute
		const auto result = m_service.getDatabase().accountCreate(id, s.asHexStr(), v.asHexStr());
		if (result)
		{
			if (*result == AccountCreationResult::AccountNameAlreadyInUse)
			{
				response.setStatus(net::http::OutgoingAnswer::Conflict);
				SendJsonResponse(response, "{\"status\":\"ACCOUNT_NAME_ALREADY_IN_USE\", \"message\":\"Account name already in use\"}");
				return;
			}

			if(*result == AccountCreationResult::Success)
			{
				SendJsonResponse(response, "");
				return;
			}
		}

		response.setStatus(net::http::OutgoingAnswer::InternalServerError);
		SendJsonResponse(response, "{\"status\":\"INTERNAL_SERVER_ERROR\"}");
	}

	void WebClient::handleCreateRealm(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPostFormArguments();
		const auto accountIdIt = arguments.find("id");
		const auto accountPassIt = arguments.find("password");
		const auto addressIt = arguments.find("address");
		const auto portIt = arguments.find("port");

		if (accountIdIt == arguments.end() || accountIdIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			SendJsonResponse(response, "{\"status\":\"MISSING_PARAMETER\", \"message\":\"Missing parameter 'id'\"}");
			return;
		}

		if (accountPassIt == arguments.end() || accountPassIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			SendJsonResponse(response, "{\"status\":\"MISSING_PARAMETER\", \"message\":\"Missing parameter 'password'\"}");
			return;
		}

		if (addressIt == arguments.end() || addressIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			SendJsonResponse(response, "{\"status\":\"MISSING_PARAMETER\", \"message\":\"Missing parameter 'address'\"}");
			return;
		}

		if (portIt == arguments.end() || portIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			SendJsonResponse(response, "{\"status\":\"MISSING_PARAMETER\", \"message\":\"Missing parameter 'port'\"}");
			return;
		}

		const String address = addressIt->second;
		const auto port = static_cast<uint16>(std::atoi(portIt->second.c_str()));

		// Generate salt and verifier data
		String id = accountIdIt->second;
		String password = accountPassIt->second;
		const auto [s, v] = calculateSV(id, password);

		// Execute
		const auto result = m_service.getDatabase().realmCreate(id, address, port, s.asHexStr(), v.asHexStr());
		if (result)
		{
			if (*result == RealmCreationResult::RealmNameAlreadyInUse)
			{
				response.setStatus(net::http::OutgoingAnswer::Conflict);
				SendJsonResponse(response, "{\"status\":\"ACCOUNT_NAME_ALREADY_IN_USE\", \"message\":\"Realm name already in use\"}");
				return;
			}

			if (*result == RealmCreationResult::Success)
			{
				SendJsonResponse(response, "");
				return;
			}
		}

		response.setStatus(net::http::OutgoingAnswer::InternalServerError);
		SendJsonResponse(response, "{\"status\":\"INTERNAL_SERVER_ERROR\"}");
	}
}
