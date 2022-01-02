// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "header.h"


namespace mmo
{
	namespace tex
	{
		namespace v1_0
		{
			Header::Header(VersionId version)
				: version(version)
				, format(RGB)
				, hasMips(false)
			{
				mipmapOffsets.fill(0);
				mipmapLengths.fill(0);
			}
		}
	}
}
