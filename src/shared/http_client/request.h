// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include <string>

namespace mmo
{
	namespace net
	{
		namespace http_client
		{
			struct Request
			{
				std::string host;
				std::string document;
			};
		}
	}
}
