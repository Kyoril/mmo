// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "magic.h"


namespace mmo
{
	namespace hpak
	{
		struct PreHeader
		{
			VersionId version;

			PreHeader();
			explicit PreHeader(VersionId version);
		};
	}
}
