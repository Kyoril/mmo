// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/sha1.h"
#include "mesh/magic.h"

#include <array>


namespace mmo
{
	namespace mesh
	{
		namespace v1_0
		{
			struct Header
			{
				VersionId version;

				explicit Header(VersionId version);
			};
		}
	}
}
