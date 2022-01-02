// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "pre_header.h"


namespace mmo
{
	namespace hpak
	{
		PreHeader::PreHeader()
			: version(VersionId::Version_1_0)
		{
		}

		PreHeader::PreHeader(VersionId version)
			: version(version)
		{
		}
	}
}
