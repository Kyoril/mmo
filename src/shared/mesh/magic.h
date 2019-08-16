// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once


#include "base/typedefs.h"

#include <array>


namespace mmo
{
	namespace mesh
	{
		static const std::array<char, 4> FileBeginMagic = {{'M', 'E', 'S', 'H'}};

		enum VersionId
		{
		    Version_1_0 = 0x100
		};
	}
}
