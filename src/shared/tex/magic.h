// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once


#include "base/typedefs.h"

#include <array>


namespace mmo
{
	namespace tex
	{
		static const std::array<char, 4> FileBeginMagic = {{'H', 'T', 'E', 'X'}};

		enum VersionId
		{
		    Version_1_0 = 0x100
		};
	}
}
