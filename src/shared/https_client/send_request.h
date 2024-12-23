// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "request.h"
#include "response.h"

#include "base/typedefs.h"

namespace mmo
{
	namespace net
	{
		namespace https_client
		{
			https_client::Response sendRequest(
			    const std::string &host,
				uint16 port,
			    const Request &request
			);
		}
	}
}
