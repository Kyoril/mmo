// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "path.h"

#include <memory>
#include <ostream>

namespace mmo
{
	namespace virtual_dir
	{
		struct IWriter
		{
			virtual ~IWriter();
			virtual std::unique_ptr<std::ostream> writeFile(
			    const Path &fileName,
			    bool openAsText,
			    bool createDirectories
			) = 0;
		};
	}
}
