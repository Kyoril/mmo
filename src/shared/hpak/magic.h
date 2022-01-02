// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once


#include "base/typedefs.h"

#include <array>


namespace mmo
{
	namespace hpak
	{
		static const std::array<char, 4> FileBeginMagic = {{'H', 'P', 'A', 'K'}};

		enum VersionId
		{
		    Version_1_0 = 0x100
		};
	}
}
