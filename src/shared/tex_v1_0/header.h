// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/sha1.h"
#include "magic.h"
#include "tex/magic.h"

#include <array>


namespace mmo
{
	namespace tex
	{
		namespace v1_0
		{
			struct Header
			{
				VersionId version;
				PixelFormat format;
				bool hasMips;
				uint16 width;
				uint16 height;
				std::array<uint32, 16> mipmapOffsets;
				std::array<uint32, 16> mipmapLengths;

				explicit Header(VersionId version);
			};
		}
	}
}
