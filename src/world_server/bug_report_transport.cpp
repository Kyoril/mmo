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
