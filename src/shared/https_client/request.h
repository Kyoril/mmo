// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>
#include <optional>
#include <cstdint>

namespace mmo
{
	namespace net
	{
		namespace https_client
		{
			struct Request
			{
				std::string host;
				std::string document;
				
				// Optional byte range for partial content requests (HTTP Range header)
				// If set, requests bytes from start to end (inclusive)
				std::optional<std::pair<std::uintmax_t, std::uintmax_t>> byteRange;
			};
		}
	}
}
