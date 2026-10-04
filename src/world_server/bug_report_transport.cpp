// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "bug_report_transport.h"

#include "http_client/send_request.h"
#include "https_client/send_request.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace mmo
{
	bool ParseBugApiUrl(const std::string& url, BugApiUrl& out)
	{
		out = BugApiUrl{};

		const auto schemeEnd = url.find("://");
		if (schemeEnd == std::string::npos)
		{
			return false;
		}

		out.scheme = url.substr(0, schemeEnd);
		const std::string rest = url.substr(schemeEnd + 3);

		if (out.scheme == "file")
		{
			out.path = rest;
			return !out.path.empty();
		}

		if (out.scheme != "http" && out.scheme != "https")
		{
			return false;
		}

		const auto pathStart = rest.find('/');
		const std::string authority = rest.substr(0, pathStart);
		out.path = pathStart == std::string::npos ? "" : rest.substr(pathStart);
		while (!out.path.empty() && out.path.back() == '/')
		{
			out.path.pop_back();
		}

		const auto colon = authority.find(':');
		out.host = authority.substr(0, colon);
		out.port = out.scheme == "https" ? 443 : 80;
		if (colon != std::string::npos)
		{
			const std::string portText = authority.substr(colon + 1);
			if (portText.empty() || portText.find_first_not_of("0123456789") != std::string::npos)
			{
				return false;
			}

			const unsigned long port = std::stoul(portText);
			if (port == 0 || port > 65535)
			{
				return false;
			}
			out.port = static_cast<uint16>(port);
		}

		return !out.host.empty();
	}

	namespace
	{
		template<class Request>
		Request MakeRequest(const BugApiUrl& url, const std::string& apiKey, const std::string& body)
		{
			Request request;
			request.host = url.host;
			request.document = url.path + "/api/bugs";
			request.method = "POST";
			request.headers.emplace_back("Content-Type", "application/json");
			request.headers.emplace_back("Accept", "application/json");
			request.headers.emplace_back("X-Api-Key", apiKey);
			request.body = body;
			return request;
		}
	}

	BugReportUploader::Transport MakeBugReportTransport(const std::string& url, const std::string& apiKey)
	{
		BugApiUrl parsed;
		if (!ParseBugApiUrl(url, parsed))
		{
			return {};
		}

		if (parsed.scheme == "file")
		{
			auto counter = std::make_shared<std::atomic<uint32>>(0);
			return [directory = parsed.path, counter](const std::string& body, std::string& error) -> uint32
			{
				std::error_code ec;
				std::filesystem::create_directories(directory, ec);

				const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count();
				const auto file = std::filesystem::path(directory) /
					("bug-" + std::to_string(now) + "-" + std::to_string(++*counter) + ".json");

				std::ofstream out(file, std::ios::binary);
				if (!out)
				{
					error = "cannot write " + file.string();
					return 0;
				}
				out << body;
				return 201;
			};
		}

		if (parsed.scheme == "https")
		{
			return [parsed, apiKey](const std::string& body, std::string& error) -> uint32
			{
				const auto response = net::https_client::sendRequest(parsed.host, parsed.port,
					MakeRequest<net::https_client::Request>(parsed, apiKey, body));
				if (response.status >= 400 && response.body)
				{
					std::string text((std::istreambuf_iterator<char>(*response.body)), std::istreambuf_iterator<char>());
					error = text.substr(0, 512);
				}
				return response.status;
			};
		}

		return [parsed, apiKey](const std::string& body, std::string& error) -> uint32
		{
			const auto response = net::http_client::sendRequest(parsed.host, parsed.port,
				MakeRequest<net::http_client::Request>(parsed, apiKey, body));
			if (response.status >= 400 && response.body)
			{
				std::string text((std::istreambuf_iterator<char>(*response.body)), std::istreambuf_iterator<char>());
				error = text.substr(0, 512);
			}
			return response.status;
		};
	}
}
