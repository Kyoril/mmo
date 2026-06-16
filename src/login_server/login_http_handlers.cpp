// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "login_http_handlers.h"

#include "database.h"
#include "realm.h"
#include "realm_manager.h"
#include "player_manager.h"
#include "base/sha1.h"
#include "base/big_number.h"
#include "base/constants.h"
#include "base/clock.h"
#include "http/http_incoming_request.h"
#include "log/default_log_levels.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <sstream>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace mmo
{
	namespace
	{
		void SendJsonResponse(web::WebResponse& response, const json& jsonObject)
		{
			// Use the 'replace' error handler so that strings containing invalid UTF-8 (e.g. corrupt
			// data coming from the database) are serialized with the Unicode replacement character
			// instead of throwing type_error.316, which would otherwise abort the server.
			const std::string jsonStr = jsonObject.dump(-1, ' ', false, json::error_handler_t::replace);
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

		/// Splits a comma-separated list into trimmed, non-empty tokens.
		std::vector<std::string> splitCsv(const std::string& input)
		{
			std::vector<std::string> tokens;
			std::string token;
			std::istringstream stream(input);
			while (std::getline(stream, token, ','))
			{
				// Trim surrounding whitespace
				const auto first = token.find_first_not_of(" \t\r\n");
				if (first == std::string::npos)
				{
					continue;
				}
				const auto last = token.find_last_not_of(" \t\r\n");
				tokens.push_back(token.substr(first, last - first + 1));
			}
			return tokens;
		}

		bool isAllDigits(const std::string& value)
		{
			return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
		}

		/// Resolves a feature reference (a name or a numeric id) to a feature id.
		std::optional<uint32> resolveFeatureId(IDatabase& db, const std::string& reference)
		{
			if (auto byName = db.GetFeatureIdByName(reference))
			{
				return byName;
			}
			if (isAllDigits(reference))
			{
				return static_cast<uint32>(std::strtoul(reference.c_str(), nullptr, 10));
			}
			return {};
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

		if (id.length() < 3)
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "INVALID_PARAMETER";
			jsonResponse["message"] = "Account name must be at least 3 characters long";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		if (id.length() > 16)
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "INVALID_PARAMETER";
			jsonResponse["message"] = "Account name must not exceed 16 characters";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		bool hasLetter = false;
		bool hasDigit = false;
		bool validChars = true;
		for (const char c : id)
		{
			if (std::isalpha(static_cast<unsigned char>(c)))
			{
				hasLetter = true;
			}
			else if (std::isdigit(static_cast<unsigned char>(c)))
			{
				hasDigit = true;
			}
			else
			{
				validChars = false;
				break;
			}
		}

		if (!validChars)
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "INVALID_PARAMETER";
			jsonResponse["message"] = "Account name may only contain letters and digits";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		if (!hasLetter || !hasDigit)
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "INVALID_PARAMETER";
			jsonResponse["message"] = "Account name must contain at least one letter and one digit";
			SendJsonResponse(response, jsonResponse);
			return;
		}

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

	void LoginHttpHandlers::HandleListAccounts(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& args = request.getPathArguments();

		AccountListParams params;

		auto getArg = [&](const char* key, const std::string& fallback = "") -> std::string
		{
			auto it = args.find(key);
			return it != args.end() ? it->second : fallback;
		};

		params.search = getArg("search");
		params.searchField = getArg("search_field", "name");
		params.sortBy = getArg("sort_by", "id");
		params.sortAsc = getArg("sort_order", "asc") != "desc";
		params.bannedOnly = getArg("banned_only") == "1";

		try
		{
			params.page = static_cast<uint32>(std::max(1, std::atoi(getArg("page", "1").c_str())));
			params.limit = static_cast<uint32>(std::clamp(std::atoi(getArg("limit", "100").c_str()), 1, 500));
		}
		catch (...)
		{
			params.page = 1;
			params.limit = 100;
		}

		try
		{
			const auto result = m_database.GetAccountList(params);

			json jsonAccounts = json::array();
			for (const auto& acc : result.accounts)
			{
				json entry;
				entry["id"] = std::to_string(acc.id);
				entry["username"] = acc.username;
				entry["email"] = acc.email.empty() ? nullptr : json(acc.email);
				entry["created_at"] = acc.created_at.empty() ? nullptr : json(acc.created_at);
				entry["last_login"] = acc.last_login.empty() ? nullptr : json(acc.last_login);
				entry["last_ip"] = acc.last_ip.empty() ? nullptr : json(acc.last_ip);
				entry["gm_level"] = static_cast<int>(acc.gm_level);
				entry["ban_state"] = static_cast<int>(acc.ban_state);
				entry["ban_expiration"] = acc.ban_expiration.empty() ? nullptr : json(acc.ban_expiration);
				jsonAccounts.push_back(std::move(entry));
			}

			json jsonResponse;
			jsonResponse["accounts"] = std::move(jsonAccounts);
			jsonResponse["total"] = result.total;
			jsonResponse["page"] = params.page;
			jsonResponse["limit"] = params.limit;
			SendJsonResponse(response, jsonResponse);
		}
		catch (...)
		{
			response.setStatus(net::http::OutgoingAnswer::InternalServerError);
			json jsonResponse;
			jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
			SendJsonResponse(response, jsonResponse);
		}
	}

	void LoginHttpHandlers::HandleListRealms(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		(void)request;

		try
		{
			auto rows = m_database.GetRealmList();

			// Mark realms that are currently connected via the RealmManager.
			m_realmManager.ForEachRealm([&](const Realm& realm)
			{
				for (auto& row : rows)
				{
					if (row.id == realm.GetRealmId())
					{
						row.is_online = realm.IsAuthentificated();
						break;
					}
				}
			});

			json jsonRealms = json::array();
			for (const auto& r : rows)
			{
				json entry;
				entry["id"] = r.id;
				entry["name"] = r.name;
				entry["address"] = r.address;
				entry["port"] = r.port;
				entry["last_login"] = r.last_login.empty() ? nullptr : json(r.last_login);
				entry["last_ip"] = r.last_ip.empty() ? nullptr : json(r.last_ip);
				entry["last_build"] = r.last_build.empty() ? nullptr : json(r.last_build);
				entry["is_online"] = r.is_online;
				jsonRealms.push_back(std::move(entry));
			}

			json jsonResponse;
			jsonResponse["realms"] = std::move(jsonRealms);
			jsonResponse["total"] = rows.size();
			SendJsonResponse(response, jsonResponse);
		}
		catch (...)
		{
			response.setStatus(net::http::OutgoingAnswer::InternalServerError);
			json jsonResponse;
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

	void LoginHttpHandlers::HandleListFeatures(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		(void)request;

		try
		{
			const auto features = m_database.GetFeatures();

			json jsonFeatures = json::array();
			for (const auto& feature : features)
			{
				json entry;
				entry["id"] = feature.id;
				entry["name"] = feature.name;
				entry["description"] = feature.description.empty() ? nullptr : json(feature.description);
				entry["created_at"] = feature.created_at.empty() ? nullptr : json(feature.created_at);
				jsonFeatures.push_back(std::move(entry));
			}

			json jsonResponse;
			jsonResponse["features"] = std::move(jsonFeatures);
			jsonResponse["total"] = features.size();
			SendJsonResponse(response, jsonResponse);
		}
		catch (...)
		{
			response.setStatus(net::http::OutgoingAnswer::InternalServerError);
			json jsonResponse;
			jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
			SendJsonResponse(response, jsonResponse);
		}
	}

	void LoginHttpHandlers::HandleCreateFeature(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPostFormArguments();
		const auto nameIt = arguments.find("name");
		const auto descriptionIt = arguments.find("description");

		json jsonResponse;

		if (nameIt == arguments.end() || nameIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'name'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		const String name = nameIt->second;
		if (name.length() > 64)
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "INVALID_PARAMETER";
			jsonResponse["message"] = "Parameter 'name' must not exceed 64 characters";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		const String description = descriptionIt == arguments.end() ? "" : descriptionIt->second;

		try
		{
			const auto result = m_database.CreateFeature(name, description);
			if (!result)
			{
				response.setStatus(net::http::OutgoingAnswer::Conflict);
				jsonResponse["status"] = "FEATURE_NAME_ALREADY_IN_USE";
				jsonResponse["message"] = "A feature with the name '" + name + "' already exists!";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			jsonResponse["status"] = "SUCCESS";
			jsonResponse["id"] = *result;
			jsonResponse["name"] = name;
			SendJsonResponse(response, jsonResponse);
		}
		catch (...)
		{
			response.setStatus(net::http::OutgoingAnswer::InternalServerError);
			jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
			SendJsonResponse(response, jsonResponse);
		}
	}

	void LoginHttpHandlers::HandleDeleteFeature(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPostFormArguments();
		const auto featureIt = arguments.find("feature");

		json jsonResponse;

		if (featureIt == arguments.end() || featureIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'feature'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		try
		{
			const auto featureId = resolveFeatureId(m_database, featureIt->second);
			if (!featureId)
			{
				response.setStatus(net::http::OutgoingAnswer::NotFound);
				jsonResponse["status"] = "FEATURE_DOES_NOT_EXIST";
				jsonResponse["message"] = "A feature '" + featureIt->second + "' does not exist!";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			if (m_database.DeleteFeature(*featureId))
			{
				jsonResponse["status"] = "SUCCESS";
				SendJsonResponse(response, jsonResponse);
			}
			else
			{
				response.setStatus(net::http::OutgoingAnswer::NotFound);
				jsonResponse["status"] = "FEATURE_DOES_NOT_EXIST";
				jsonResponse["message"] = "A feature '" + featureIt->second + "' does not exist!";
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

	void LoginHttpHandlers::HandleGrantFeature(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPostFormArguments();
		const auto featureIt = arguments.find("feature");
		const auto accountNamesIt = arguments.find("account_names");
		const auto expirationIt = arguments.find("expiration");

		json jsonResponse;

		if (featureIt == arguments.end() || featureIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'feature'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		if (accountNamesIt == arguments.end() || accountNamesIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'account_names' (comma separated)";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		if (expirationIt != arguments.end() && !expirationIt->second.empty() && !isValidDateTime(expirationIt->second))
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "INVALID_PARAMETER";
			jsonResponse["message"] = "Parameter 'expiration' must be formatted like this: 'YYYY-MM-DD HH:MM:SS'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		const String expiration = expirationIt == arguments.end() ? "" : expirationIt->second;

		try
		{
			const auto featureId = resolveFeatureId(m_database, featureIt->second);
			if (!featureId)
			{
				response.setStatus(net::http::OutgoingAnswer::NotFound);
				jsonResponse["status"] = "FEATURE_DOES_NOT_EXIST";
				jsonResponse["message"] = "A feature '" + featureIt->second + "' does not exist!";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			std::vector<uint64> accountIds;
			json unknownAccounts = json::array();
			for (const auto& accountName : splitCsv(accountNamesIt->second))
			{
				if (const auto accountId = m_database.GetAccountIdByName(accountName))
				{
					accountIds.push_back(*accountId);
				}
				else
				{
					unknownAccounts.push_back(accountName);
				}
			}

			if (accountIds.empty())
			{
				response.setStatus(net::http::OutgoingAnswer::NotFound);
				jsonResponse["status"] = "NO_VALID_ACCOUNTS";
				jsonResponse["message"] = "None of the provided account names exist!";
				jsonResponse["unknown_accounts"] = std::move(unknownAccounts);
				SendJsonResponse(response, jsonResponse);
				return;
			}

			if (!m_database.GrantFeature(*featureId, accountIds, expiration))
			{
				response.setStatus(net::http::OutgoingAnswer::InternalServerError);
				jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			jsonResponse["status"] = "SUCCESS";
			jsonResponse["granted"] = accountIds.size();
			jsonResponse["unknown_accounts"] = std::move(unknownAccounts);
			SendJsonResponse(response, jsonResponse);
		}
		catch (...)
		{
			response.setStatus(net::http::OutgoingAnswer::InternalServerError);
			jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
			SendJsonResponse(response, jsonResponse);
		}
	}

	void LoginHttpHandlers::HandleRevokeFeature(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPostFormArguments();
		const auto featureIt = arguments.find("feature");
		const auto accountNamesIt = arguments.find("account_names");

		json jsonResponse;

		if (featureIt == arguments.end() || featureIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'feature'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		if (accountNamesIt == arguments.end() || accountNamesIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'account_names' (comma separated)";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		try
		{
			const auto featureId = resolveFeatureId(m_database, featureIt->second);
			if (!featureId)
			{
				response.setStatus(net::http::OutgoingAnswer::NotFound);
				jsonResponse["status"] = "FEATURE_DOES_NOT_EXIST";
				jsonResponse["message"] = "A feature '" + featureIt->second + "' does not exist!";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			std::vector<uint64> accountIds;
			json unknownAccounts = json::array();
			for (const auto& accountName : splitCsv(accountNamesIt->second))
			{
				if (const auto accountId = m_database.GetAccountIdByName(accountName))
				{
					accountIds.push_back(*accountId);
				}
				else
				{
					unknownAccounts.push_back(accountName);
				}
			}

			if (!accountIds.empty() && !m_database.RevokeFeature(*featureId, accountIds))
			{
				response.setStatus(net::http::OutgoingAnswer::InternalServerError);
				jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			jsonResponse["status"] = "SUCCESS";
			jsonResponse["revoked"] = accountIds.size();
			jsonResponse["unknown_accounts"] = std::move(unknownAccounts);
			SendJsonResponse(response, jsonResponse);
		}
		catch (...)
		{
			response.setStatus(net::http::OutgoingAnswer::InternalServerError);
			jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
			SendJsonResponse(response, jsonResponse);
		}
	}

	void LoginHttpHandlers::HandleGetAccountFeatures(const net::http::IncomingRequest& request, web::WebResponse& response) const
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
			const auto accountId = m_database.GetAccountIdByName(name);
			if (!accountId)
			{
				response.setStatus(net::http::OutgoingAnswer::NotFound);
				jsonResponse["status"] = "ACCOUNT_DOES_NOT_EXIST";
				jsonResponse["message"] = "An account with the name '" + name + "' does not exist!";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			const auto features = m_database.GetActiveAccountFeatures(*accountId);

			json jsonFeatures = json::array();
			for (const auto& feature : features)
			{
				json entry;
				entry["id"] = feature.id;
				entry["name"] = feature.key;
				entry["expiration"] = feature.expiration.empty() ? nullptr : json(feature.expiration);
				jsonFeatures.push_back(std::move(entry));
			}

			jsonResponse["status"] = "SUCCESS";
			jsonResponse["account_name"] = name;
			jsonResponse["features"] = std::move(jsonFeatures);
			SendJsonResponse(response, jsonResponse);
		}
		catch (...)
		{
			response.setStatus(net::http::OutgoingAnswer::InternalServerError);
			jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
			SendJsonResponse(response, jsonResponse);
		}
	}

	void LoginHttpHandlers::HandleSetRealmFeatureRequirement(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPostFormArguments();
		const auto realmIt = arguments.find("realm");
		const auto featureIt = arguments.find("feature");
		const auto visibilityIt = arguments.find("require_visibility");
		const auto loginIt = arguments.find("require_login");

		json jsonResponse;

		if (realmIt == arguments.end() || realmIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'realm'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		if (featureIt == arguments.end() || featureIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'feature'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		const bool requireVisibility = visibilityIt != arguments.end() && (visibilityIt->second == "1" || visibilityIt->second == "true");
		const bool requireLogin = loginIt != arguments.end() && (loginIt->second == "1" || loginIt->second == "true");

		if (!requireVisibility && !requireLogin)
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "INVALID_PARAMETER";
			jsonResponse["message"] = "At least one of 'require_visibility' or 'require_login' must be set. Use the remove endpoint to clear a requirement.";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		try
		{
			std::optional<uint32> realmId = isAllDigits(realmIt->second)
				? std::optional<uint32>(static_cast<uint32>(std::strtoul(realmIt->second.c_str(), nullptr, 10)))
				: m_database.GetRealmIdByName(realmIt->second);
			if (!realmId)
			{
				response.setStatus(net::http::OutgoingAnswer::NotFound);
				jsonResponse["status"] = "REALM_DOES_NOT_EXIST";
				jsonResponse["message"] = "A realm '" + realmIt->second + "' does not exist!";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			const auto featureId = resolveFeatureId(m_database, featureIt->second);
			if (!featureId)
			{
				response.setStatus(net::http::OutgoingAnswer::NotFound);
				jsonResponse["status"] = "FEATURE_DOES_NOT_EXIST";
				jsonResponse["message"] = "A feature '" + featureIt->second + "' does not exist!";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			if (!m_database.SetRealmFeatureRequirement(*realmId, *featureId, requireVisibility, requireLogin))
			{
				response.setStatus(net::http::OutgoingAnswer::InternalServerError);
				jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			// Refresh the cached requirements on the connected realm (if any) so the change takes effect immediately.
			m_realmManager.NotifyRealmRequirementsChanged(*realmId);

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

	void LoginHttpHandlers::HandleRemoveRealmFeatureRequirement(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPostFormArguments();
		const auto realmIt = arguments.find("realm");
		const auto featureIt = arguments.find("feature");

		json jsonResponse;

		if (realmIt == arguments.end() || realmIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'realm'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		if (featureIt == arguments.end() || featureIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'feature'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		try
		{
			std::optional<uint32> realmId = isAllDigits(realmIt->second)
				? std::optional<uint32>(static_cast<uint32>(std::strtoul(realmIt->second.c_str(), nullptr, 10)))
				: m_database.GetRealmIdByName(realmIt->second);
			if (!realmId)
			{
				response.setStatus(net::http::OutgoingAnswer::NotFound);
				jsonResponse["status"] = "REALM_DOES_NOT_EXIST";
				jsonResponse["message"] = "A realm '" + realmIt->second + "' does not exist!";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			const auto featureId = resolveFeatureId(m_database, featureIt->second);
			if (!featureId)
			{
				response.setStatus(net::http::OutgoingAnswer::NotFound);
				jsonResponse["status"] = "FEATURE_DOES_NOT_EXIST";
				jsonResponse["message"] = "A feature '" + featureIt->second + "' does not exist!";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			m_database.RemoveRealmFeatureRequirement(*realmId, *featureId);
			m_realmManager.NotifyRealmRequirementsChanged(*realmId);

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

	void LoginHttpHandlers::HandleGetRealmFeatureRequirements(const net::http::IncomingRequest& request, web::WebResponse& response) const
	{
		const auto& arguments = request.getPathArguments();
		const auto realmIt = arguments.find("realm");

		json jsonResponse;

		if (realmIt == arguments.end() || realmIt->second.empty())
		{
			response.setStatus(net::http::OutgoingAnswer::BadRequest);
			jsonResponse["status"] = "MISSING_PARAMETER";
			jsonResponse["message"] = "Missing parameter 'realm'";
			SendJsonResponse(response, jsonResponse);
			return;
		}

		try
		{
			std::optional<uint32> realmId = isAllDigits(realmIt->second)
				? std::optional<uint32>(static_cast<uint32>(std::strtoul(realmIt->second.c_str(), nullptr, 10)))
				: m_database.GetRealmIdByName(realmIt->second);
			if (!realmId)
			{
				response.setStatus(net::http::OutgoingAnswer::NotFound);
				jsonResponse["status"] = "REALM_DOES_NOT_EXIST";
				jsonResponse["message"] = "A realm '" + realmIt->second + "' does not exist!";
				SendJsonResponse(response, jsonResponse);
				return;
			}

			const auto requirements = m_database.GetRealmFeatureRequirements(*realmId);

			json jsonRequirements = json::array();
			for (const auto& requirement : requirements)
			{
				json entry;
				entry["feature_id"] = requirement.featureId;
				entry["feature_name"] = requirement.featureName;
				entry["require_visibility"] = requirement.requireVisibility;
				entry["require_login"] = requirement.requireLogin;
				jsonRequirements.push_back(std::move(entry));
			}

			jsonResponse["status"] = "SUCCESS";
			jsonResponse["realm_id"] = *realmId;
			jsonResponse["requirements"] = std::move(jsonRequirements);
			SendJsonResponse(response, jsonResponse);
		}
		catch (...)
		{
			response.setStatus(net::http::OutgoingAnswer::InternalServerError);
			jsonResponse["status"] = "INTERNAL_SERVER_ERROR";
			SendJsonResponse(response, jsonResponse);
		}
	}
}
