// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

namespace mmo
{
	enum class CreateServiceResult
	{
		IsObsoleteProcess,
		IsServiceProcess
	};

	/// Daemonizes the process on unix platforms.
	CreateServiceResult createService();
}
