// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

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
