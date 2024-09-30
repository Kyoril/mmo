// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "update_url.h"

#include <algorithm>
#include <stdexcept>
#include <cstdlib>


namespace mmo::updating
{
	UpdateURL::UpdateURL(const std::string &url)
		: port(0)
	{
		if (url.rfind("http://", 0) == 0)
		{
			scheme = UP_HTTP;
			const auto beginOfHost = url.begin() + 7;
			const auto slash = std::find(beginOfHost, url.end(), '/');
			const auto colon = std::find(beginOfHost, slash, ':');
			if (colon != slash)
			{
				port = std::atoi(std::string(colon + 1, slash).c_str());
			}

			host.assign(beginOfHost, colon);
			path.assign(slash, url.end());

			if (host.empty())
			{
				throw std::invalid_argument("Invalid URL: Host expected");
			}

			if (path.empty())
			{
				path = "/";
			}
		}
		else if (url.rfind("https://", 0) == 0)
		{
			scheme = UP_HTTPS;
			const auto beginOfHost = url.begin() + 8;
			const auto slash = std::find(beginOfHost, url.end(), '/');
			const auto colon = std::find(beginOfHost, slash, ':');
			if (colon != slash)
			{
				port = std::atoi(std::string(colon + 1, slash).c_str());
			}

			host.assign(beginOfHost, colon);
			path.assign(slash, url.end());

			if (host.empty())
			{
				throw std::invalid_argument("Invalid URL: Host expected");
			}

			if (path.empty())
			{
				path = "/";
			}
		}
		else
		{
			scheme = UP_FileSystem;
			path = url;
		}
	}

	UpdateURL::UpdateURL(
	    UpdateURLScheme scheme,
	    std::string host,
	    uint16 port,
	    std::string path)
		: scheme(scheme)
		, host(std::move(host))
		, port(port)
		, path(std::move(path))
	{
	}


	bool operator == (const UpdateURL &left, const UpdateURL &right)
	{
		return
		    (left.scheme == right.scheme) &&
		    (left.host == right.host) &&
		    (left.port == right.port) &&
		    (left.path == right.path);
	}
}
