// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

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
