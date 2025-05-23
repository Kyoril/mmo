// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"


namespace mmo::updating
{
	enum UpdateURLScheme
	{
	    UP_FileSystem,
	    UP_HTTP,
		UP_HTTPS
	};


	struct UpdateURL
	{
		UpdateURLScheme scheme;
		std::string host;
		uint16 port;
		std::string path;


		explicit UpdateURL(const std::string &url);
		UpdateURL(
		    UpdateURLScheme scheme,
		    std::string host,
			uint16 port,
		    std::string path);
	};


	bool operator == (const UpdateURL &left, const UpdateURL &right);
}
