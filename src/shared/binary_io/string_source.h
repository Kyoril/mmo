// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "container_source.h"

#include <string>

namespace io
{
	typedef ContainerSource<std::string> StringSource;
	typedef ContainerSource<std::wstring> WStringSource;
}
