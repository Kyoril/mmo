// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "api_url.h"

namespace mmo
{
	bool ParseApiUrl(const std::string& url, ApiUrl& out)
	{
		out = ApiUrl{};

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
}

