// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "login_http_handlers.h"

#include "database.h"
#include "realm_manager.h"
#include "player_manager.h"
#include "base/sha1.h"
#include "base/big_number.h"
#include "base/constants.h"
#include "base/clock.h"
#include "http/http_incoming_request.h"
#include "log/default_log_levels.h"

#include <regex>
#include "nlohmann/json.hpp"

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

		std::pair<BigNumber, BigNumber> calculateSV(String& id, String& password)
		{
			std::transform(id.begin(), id.end(), id.begin(), ::toupper);
			std::transform(password.begin(), password.end(), password.begin(), ::toupper);

			const std::string authString = id + ":" + password;
			const SHA1Hash authHash = sha1(authString.c_str(), authString.size());

			BigNumber s;
			s.setRand(32 * 8);

			HashGeneratorSha1 gen;

			BigNumber x;
			gen.update((const char*)s.asByteArray().data(), s.getNumBytes());
			gen.update((const char*)authHash.data(), authHash.size());
			const SHA1Hash x_hash = gen.finalize();
			x.setBinary(x_hash.data(), x_hash.size());

			BigNumber v = constants::srp::g.modExp(x, constants::srp::N);
			return std::make_pair(s, v);
		}

		bool isValidDateTime(const std::string& dateTime)
		{
			std::regex dateTimeRegex(
				R"(^\d{4}-(0[1-9]|1[0-2])-(0[1-9]|[12]\d|3[01])\s(0[0-9]|1[0-9]|2[0-3]):([0-5][0-9]):([0-5][0-9])$)"
			);
			return std::regex_match(dateTime, dateTimeRegex);
		}
	}

	LoginHttpHandlers::LoginHttpHandlers(IDatabase& db, RealmManager& realmManager, PlayerManager& playerManager)
		: m_database(db)
		, m_realmManager(realmManager)
		, m_playerManager(playerManager)
	{
	}

	void LoginHttpHandlers::HandleCreateAccount(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPostFormArguments();
		const auto accountIdIt = arguments.find("id");
		const auto accountPassIt = arguments.find("password");

		json jsonResponse;

		if (accountIdIt == arguments.end())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'id'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		if (accountPassIt == arguments.end())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'password'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		String id = accountIdIt->second;
		String password = accountPassIt->second;
		const auto [s, v] = calculateSV(id, password);

		const auto result = m_database.AccountCreate(id, s.asHexStr(), v.asHexStr());
		if (result)
		{
			if (*result == AccountCreationResult::AccountNameAlreadyInUse)
			{
				response.setStatus(net::http::OutgoingAnswer::Conflict);
				jsonResponse["status"] = "ACCOUNT_NAME_ALREADY_IN_USE";
				jsonResponse["message"] = "Account name already in use";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			if (*result == AccountCreationResult::Success)
			{
				SendJsonResponse(response, jsonResponse);
				return;
			}
		}

		response.setStatus(net::http::OutgoingAnswer::InternalServerError);
		jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
		SendJsonResponse(response, jsonResponse);
	}

	void LoginHttpHandlers::HandleCreateRealm(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPostFormArguments();
		const auto accountIdIt = arguments.find("id");
		const auto accountPassIt = arguments.find("password");
		const auto addressIt = arguments.find("address");
		const auto portIt = arguments.find("port");

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

		if (addressIt == arguments.end() || addressIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'address'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		if (portIt == arguments.end() || portIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'port'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		const String address = addressIt->second;
		const auto port = static_cast<uint16>(std::atoi(portIt->second.c_str()));

		String id = accountIdIt->second;
		String password = accountPassIt->second;
		const auto [s, v] = calculateSV(id, password);

		const auto result = m_database.RealmCreate(id, address, port, s.asHexStr(), v.asHexStr());
		if (result)
		{
			if (*result == RealmCreationResult::RealmNameAlreadyInUse)
			{
				response.setStatus(net::http::OutgoingAnswer::Conflict);
				jsonResponse["status"] = "REALM_NAME_ALREADY_IN_USE";
				jsonResponse["message"] = "Realm name already in use";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			if (*result == RealmCreationResult::Success)
			{
				SendJsonResponse(response, jsonResponse);
				return;
			}
		}

		response.setStatus(net::http::OutgoingAnswer::InternalServerError);
		jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
		SendJsonResponse(response, jsonResponse);
	}

	void LoginHttpHandlers::HandleBanAccount(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPostFormArguments();
		const auto accountNameIt = arguments.find("account_name");
		const auto expirationIt = arguments.find("expiration");
		const auto reasonIt = arguments.find("reason");

		json jsonResponse;
		if (accountNameIt == arguments.end() || accountNameIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'account_name'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		if (expirationIt != arguments.end() && !isValidDateTime(expirationIt->second))
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "INVALID_PARAMETER";
			jsonResponse["message"] = "Parameter 'expiration' must be formatted like this: 'YYYY-MM-DD HH:MM:SS'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		if (reasonIt != arguments.end() && reasonIt->second.length() > 256)
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "INVALID_PARAMETER";
			jsonResponse["message"] = "Parameter 'reason' must not exceed a length of 256 characters!";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		const String name = accountNameIt->second;
		const String expiration = expirationIt == arguments.end() ? "" : expirationIt->second;
		const String reason = reasonIt == arguments.end() ? "" : reasonIt->second;

		try
		{
			std::optional<AccountData> account = m_database.GetAccountDataByName(name);
			if (!account)
			{
				response.setStatus(net::http::OutgoingAnswer::NotFound);
				jsonResponse["status"] = "ACCOUNT_DOES_NOT_EXIST";
				jsonResponse["message"] = "An account with the name '" + name + "' does not exist!";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			if (account->banned != BanState::None)
			{
				response.setStatus(net::http::OutgoingAnswer::Conflict);
				jsonResponse["status"] = "ACCOUNT_ALREADY_BANNED";
				jsonResponse["message"] = "The account '" + name + "' is already banned!";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			m_database.BanAccountByName(name, expiration, reason);
			jsonResponse["status"] = "SUCCESS";
			SendJsonResponse(response, jsonResponse);

			m_realmManager.NotifyAccountBanned(account->id);
			m_playerManager.KickPlayerByAccountId(account->id);
		}
		catch (...)
		{
			response.setStatus(net::http::OutgoingAnswer::InternalServerError);
			jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
			SendJsonResponse(response, jsonResponse);
		}
	}

	void LoginHttpHandlers::HandleUnbanAccount(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPostFormArguments();
		const auto accountNameIt = arguments.find("account_name");
		const auto reasonIt = arguments.find("reason");

		json jsonResponse;

		if (accountNameIt == arguments.end() || accountNameIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'account_name'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		if (reasonIt != arguments.end() && reasonIt->second.length() > 256)
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "INVALID_PARAMETER";
			jsonResponse["message"] = "Parameter 'reason' must not exceed a length of 256 characters!";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		const String name = accountNameIt->second;
		const String reason = reasonIt == arguments.end() ? "" : reasonIt->second;

		try
		{
			m_database.UnbanAccountByName(name, reason);
			jsonResponse["status"] = "SUCCESS";
			SendJsonResponse(response, jsonResponse);
		}
		catch (...)
		{
			response.setStatus(net::http::OutgoingAnswer::InternalServerError);
			jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
			SendJsonResponse(response, jsonResponse);
		}
	}

	void LoginHttpHandlers::HandleGetGMLevel(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPathArguments();
		const auto accountNameIt = arguments.find("account_name");

		json jsonResponse;

		if (accountNameIt == arguments.end() || accountNameIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'account_name'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		const String name = accountNameIt->second;

		try
		{
			std::optional<AccountData> account = m_database.GetAccountDataByName(name);
			if (!account)
			{
				response.setStatus(net::http::OutgoingAnswer::NotFound);
				jsonResponse["status"] = "ACCOUNT_DOES_NOT_EXIST";
				jsonResponse["message"] = "An account with the name '" + name + "' does not exist!";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			auto result = m_database.GetAccountSessionKey(name);
			if (result)
			{
				uint8 gmLevel = std::get<2>(*result);
				jsonResponse["status"] = "SUCCESS";
				jsonResponse["account_name"] = name;
				jsonResponse["gm_level"] = static_cast<int>(gmLevel);
				SendJsonResponse(response, jsonResponse);
			}
			else
			{
				response.setStatus(net::http::OutgoingAnswer::InternalServerError);
				jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
				jsonResponse["message"] = "Failed to retrieve GM level information";
				SendJsonResponse(response, jsonResponse);
			}
		}
		catch (...)
		{
			response.setStatus(net::http::OutgoingAnswer::InternalServerError);
			jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
			SendJsonResponse(response, jsonResponse);
		}
	}

	void LoginHttpHandlers::HandleSetGMLevel(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPostFormArguments();
		const auto accountNameIt = arguments.find("account_name");
		const auto gmLevelIt = arguments.find("gm_level");

		json jsonResponse;

		if (accountNameIt == arguments.end() || accountNameIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'account_name'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		if (gmLevelIt == arguments.end() || gmLevelIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'gm_level'";
			SendJsonResponse(response, jsonResponse);
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
			jsonResponse["status"] = "INVALID_PARAMETER";
			jsonResponse["message"] = "Parameter 'gm_level' must be a valid integer number";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		if (gmLevel < 0 || gmLevel > 255)
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "INVALID_PARAMETER";
			jsonResponse["message"] = "Parameter 'gm_level' must be between 0 and 255";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		try
		{
			std::optional<AccountData> account = m_database.GetAccountDataByName(name);
			if (!account)
			{
				response.setStatus(net::http::OutgoingAnswer::NotFound);
				jsonResponse["status"] = "ACCOUNT_DOES_NOT_EXIST";
				jsonResponse["message"] = "An account with the name '" + name + "' does not exist!";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			if (m_database.SetAccountGMLevel(name, static_cast<uint8>(gmLevel)))
			{
				m_playerManager.KickPlayerByAccountId(account->id);
				jsonResponse["status"] = "SUCCESS";
				jsonResponse["account_name"] = name;
				jsonResponse["gm_level"] = gmLevel;
				SendJsonResponse(response, jsonResponse);
			}
			else
			{
				response.setStatus(net::http::OutgoingAnswer::InternalServerError);
				jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
				jsonResponse["message"] = "Failed to update GM level for account '" + name + "'";
				SendJsonResponse(response, jsonResponse);
			}
		}
		catch (...)
		{
			response.setStatus(net::http::OutgoingAnswer::InternalServerError);
			jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
			SendJsonResponse(response, jsonResponse);
		}
	}
}
