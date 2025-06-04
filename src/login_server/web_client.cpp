// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "web_client.h"

#include "database.h"
#include "web_service.h"
#include "base/clock.h"
#include "http/http_incoming_request.h"
#include "player_manager.h"
#include "player.h"
#include "log/default_log_levels.h"

#include <regex>

#include "realm_manager.h"

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

					std::ostringstream message;
					message << "{\"uptime\":" << gameTimeToSeconds<unsigned>(GetAsyncTimeMs() - startTime) << "}";
					SendJsonResponse(response, message.str());
				}
				else if (url == "/gm-level")
				{
					handleGetGMLevel(request, response);
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
				else if (url == "/ban-account")
				{
					handleBanAccount(request, response);
				}
				else if (url == "/unban-account")
				{
					handleUnbanAccount(request, response);
				}
				else if (url == "/gm-level")
				{
					handleSetGMLevel(request, response);
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
		const auto result = m_service.GetDatabase().AccountCreate(id, s.asHexStr(), v.asHexStr());
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
		const auto result = m_service.GetDatabase().RealmCreate(id, address, port, s.asHexStr(), v.asHexStr());
		if (result)
		{
			if (*result == RealmCreationResult::RealmNameAlreadyInUse)
			{
				response.setStatus(net::http::OutgoingAnswer::Conflict);
                                SendJsonResponse(response, "{\"status\":\"REALM_NAME_ALREADY_IN_USE\", \"message\":\"Realm name already in use\"}");
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

	namespace
	{
		bool isValidDateTime(const std::string& dateTime)
		{
			// Regular expression for "YYYY-MM-DD HH:MM:SS" format
			std::regex dateTimeRegex(
				R"(^\d{4}-(0[1-9]|1[0-2])-(0[1-9]|[12]\d|3[01])\s(0[0-9]|1[0-9]|2[0-3]):([0-5][0-9]):([0-5][0-9])$)"
			);

			return std::regex_match(dateTime, dateTimeRegex);
		}
	}
	
	void WebClient::handleBanAccount(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPostFormArguments();
		const auto accountNameIt = arguments.find("account_name");
		const auto expirationIt = arguments.find("expiration");
		const auto reasonIt = arguments.find("reason");

		if (accountNameIt == arguments.end() || accountNameIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			SendJsonResponse(response, "{\"status\":\"MISSING_PARAMETER\", \"message\":\"Missing parameter 'account_name'\"}");
			return;
		}

		if (expirationIt != arguments.end() && !isValidDateTime(expirationIt->second))
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			SendJsonResponse(response, "{\"status\":\"INVALID_PARAMETER\", \"message\":\"Parameter 'expiration' must be formatted like this: 'YYYY-MM-DD HH:MM:SS'\"}");
			return;
		}

		if (reasonIt != arguments.end() && reasonIt->second.length() > 256)
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			SendJsonResponse(response, "{\"status\":\"INVALID_PARAMETER\", \"message\":\"Parameter 'reason' must not exceed a length of 256 characters!\"}");
			return;
		}

		const String name = accountNameIt->second;
		const String expiration = expirationIt == arguments.end() ? "" : expirationIt->second;
		const String reason = reasonIt == arguments.end() ? "" : reasonIt->second;

		try
		{
			std::optional<AccountData> account = m_service.GetDatabase().GetAccountDataByName(name);
			if (!account)
			{
				response.setStatus(net::http::OutgoingAnswer::NotFound);
				SendJsonResponse(response, "{\"status\":\"ACCOUNT_DOES_NOT_EXIST\", \"message\":\"An account with the name '"+name+"' does not exist!\"}");
				return;
			}

			if (account->banned != BanState::None)
			{
				response.setStatus(net::http::OutgoingAnswer::Conflict);
				SendJsonResponse(response, "{\"status\":\"ACCOUNT_ALREADY_BANNED\", \"message\":\"The account is already banned right now!\"}");
				return;
			}

			m_service.GetDatabase().BanAccountByName(name, expiration, reason);
			SendJsonResponse(response, "{\"status\":\"SUCCESS\"}");

			// Notify subscribers
			m_service.GetRealmManager().NotifyAccountBanned(account->id);
			m_service.GetPlayerManager().KickPlayerByAccountId(account->id);
		}
		catch(...)
		{
			response.setStatus(net::http::OutgoingAnswer::InternalServerError);
			SendJsonResponse(response, "{\"status\":\"INTERNAL_SERVER_ERROR\"}");
		}
	}

	void WebClient::handleUnbanAccount(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPostFormArguments();
		const auto accountNameIt = arguments.find("account_name");
		const auto reasonIt = arguments.find("reason");

		if (accountNameIt == arguments.end() || accountNameIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			SendJsonResponse(response, "{\"status\":\"MISSING_PARAMETER\", \"message\":\"Missing parameter 'account_name'\"}");
			return;
		}

		if (reasonIt != arguments.end() && reasonIt->second.length() > 256)
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			SendJsonResponse(response, "{\"status\":\"INVALID_PARAMETER\", \"message\":\"Parameter 'reason' must not exceed a length of 256 characters!\"}");
			return;
		}

		const String name = accountNameIt->second;
		const String reason = reasonIt == arguments.end() ? "" : reasonIt->second;

		try
		{
			m_service.GetDatabase().UnbanAccountByName(name, reason);
			SendJsonResponse(response, "{\"status\":\"SUCCESS\"}");
		}
		catch (...)
		{
			response.setStatus(net::http::OutgoingAnswer::InternalServerError);
			SendJsonResponse(response, "{\"status\":\"INTERNAL_SERVER_ERROR\"}");
		}
	}

	void WebClient::handleGetGMLevel(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPathArguments();
		const auto accountNameIt = arguments.find("account_name");

		if (accountNameIt == arguments.end() || accountNameIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			SendJsonResponse(response, "{\"status\":\"MISSING_PARAMETER\", \"message\":\"Missing parameter 'account_name'\"}");
			return;
		}

		const String name = accountNameIt->second;

		try
		{
			// First check if the account exists
			std::optional<AccountData> account = m_service.GetDatabase().GetAccountDataByName(name);
			if (!account)
			{
				response.setStatus(net::http::OutgoingAnswer::NotFound);
				SendJsonResponse(response, "{\"status\":\"ACCOUNT_DOES_NOT_EXIST\", \"message\":\"An account with the name '"+name+"' does not exist!\"}");
				return;
			}

			// Get the account session key which includes GM level
			auto result = m_service.GetDatabase().GetAccountSessionKey(name);
			if (result)
			{
				// The third element in the tuple is the GM level
				uint8 gmLevel = std::get<2>(*result);
				std::ostringstream message;
				message << "{\"status\":\"SUCCESS\", \"account_name\":\"" << name << "\", \"gm_level\":" << static_cast<int>(gmLevel) << "}";
				SendJsonResponse(response, message.str());
			}
			else
			{
				response.setStatus(net::http::OutgoingAnswer::InternalServerError);
				SendJsonResponse(response, "{\"status\":\"INTERNAL_SERVER_ERROR\", \"message\":\"Failed to retrieve GM level information\"}");
			}
		}
		catch (...)
		{
			response.setStatus(net::http::OutgoingAnswer::InternalServerError);
			SendJsonResponse(response, "{\"status\":\"INTERNAL_SERVER_ERROR\"}");
		}
	}

	void WebClient::handleSetGMLevel(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPostFormArguments();
		const auto accountNameIt = arguments.find("account_name");
		const auto gmLevelIt = arguments.find("gm_level");

		if (accountNameIt == arguments.end() || accountNameIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			SendJsonResponse(response, "{\"status\":\"MISSING_PARAMETER\", \"message\":\"Missing parameter 'account_name'\"}");
			return;
		}

		if (gmLevelIt == arguments.end() || gmLevelIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			SendJsonResponse(response, "{\"status\":\"MISSING_PARAMETER\", \"message\":\"Missing parameter 'gm_level'\"}");
			return;
		}

		const String name = accountNameIt->second;
		int gmLevel = 0;
		
		try
		{
			gmLevel = std::stoi(gmLevelIt->second);
		}
		catch (...)
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			SendJsonResponse(response, "{\"status\":\"INVALID_PARAMETER\", \"message\":\"Parameter 'gm_level' must be a valid integer number\"}");
			return;
		}

		if (gmLevel < 0 || gmLevel > 255)
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			SendJsonResponse(response, "{\"status\":\"INVALID_PARAMETER\", \"message\":\"Parameter 'gm_level' must be between 0 and 255\"}");
			return;
		}

		try
		{
			// First check if the account exists
			std::optional<AccountData> account = m_service.GetDatabase().GetAccountDataByName(name);
			if (!account)
			{
				response.setStatus(net::http::OutgoingAnswer::NotFound);
				SendJsonResponse(response, "{\"status\":\"ACCOUNT_DOES_NOT_EXIST\", \"message\":\"An account with the name '"+name+"' does not exist!\"}");
				return;
			}

			// Update the GM level in the database
			if (m_service.GetDatabase().SetAccountGMLevel(name, static_cast<uint8>(gmLevel)))
			{
				// Notify players who might be connected that their GM level changed
				// We need to kick by account ID, not by name
				m_service.GetPlayerManager().KickPlayerByAccountId(account->id);
				
				std::ostringstream message;
				message << "{\"status\":\"SUCCESS\", \"account_name\":\"" << name << "\", \"gm_level\":" << gmLevel << "}";
				SendJsonResponse(response, message.str());
			}
			else
			{
				response.setStatus(net::http::OutgoingAnswer::InternalServerError);
				SendJsonResponse(response, "{\"status\":\"INTERNAL_SERVER_ERROR\", \"message\":\"Failed to update GM level\"}");
			}
		}
		catch (...)
		{
			response.setStatus(net::http::OutgoingAnswer::InternalServerError);
			SendJsonResponse(response, "{\"status\":\"INTERNAL_SERVER_ERROR\"}");
		}
	}
}
