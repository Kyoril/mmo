// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <memory>

namespace mmo::updating
{
	struct IUpdateSource;
	struct UpdateURL;


	std::unique_ptr<IUpdateSource> openSourceFromUrl(const UpdateURL &url);
}
