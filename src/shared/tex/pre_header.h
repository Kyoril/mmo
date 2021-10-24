// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "magic.h"


namespace mmo
{
	namespace tex
	{
		struct PreHeader
		{
			VersionId version;

			PreHeader();
			explicit PreHeader(VersionId version);
		};
	}
}
