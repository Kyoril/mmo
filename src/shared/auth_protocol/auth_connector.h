// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "auth_protocol.h"
#include "network/connector.h"

namespace mmo
{
	namespace auth
	{
		typedef mmo::Connector<Protocol> Connector;
		typedef mmo::IConnectorListener<Protocol> IConnectorListener;
	}
}
