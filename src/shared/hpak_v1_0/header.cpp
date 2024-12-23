// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "header.h"


namespace mmo
{
	namespace hpak
	{
		namespace v1_0
		{
			FileEntry::FileEntry()
				: compression(NotCompressed)
				, contentOffset(0)
				, size(0)
				, originalSize(0)
			{
			}


			Header::Header(VersionId version)
				: version(version)
			{
			}
		}
	}
}
