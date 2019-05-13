// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "http_incoming_request.h"

namespace mmo
{
	namespace net
	{
		namespace http
		{
			struct ServerSide
			{
				typedef http::IncomingRequest IncomingPacket;
			};
		}
	}
}
