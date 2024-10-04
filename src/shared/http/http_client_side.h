// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "http_incoming_answer.h"

namespace mmo
{
	namespace net
	{
		namespace http
		{
			struct ClientSide
			{
				typedef http::IncomingAnswer IncomingPacket;
			};
		}
	}
}
