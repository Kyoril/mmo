// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "http_client_side.h"
#include "network/connector.h"

namespace mmo
{
	namespace net
	{
		namespace http
		{
			typedef net::Connector<http::ClientSide> Connector;
			typedef net::IConnectorListener<http::ClientSide> IConnectorListener;
		}
	}
}
