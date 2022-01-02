// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "request.h"
#include "response.h"

#include "base/typedefs.h"

namespace mmo
{
	namespace net
	{
		namespace http_client
		{
			Response sendRequest(
			    const std::string &host,
				uint16 port,
			    const Request &request
			);
		}
	}
}
