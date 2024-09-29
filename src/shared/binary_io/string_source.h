// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "container_source.h"

#include <string>

namespace io
{
	typedef ContainerSource<std::string> StringSource;
	typedef ContainerSource<std::wstring> WStringSource;
}
