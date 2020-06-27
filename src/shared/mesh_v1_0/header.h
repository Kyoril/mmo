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
			static const std::array<char, 4> SubMeshChunkMagic = { {'S', 'U', 'B', 'M'} };

			struct Header
			{
				VersionId version;
				uint32 vertexChunkOffset;
				uint32 indexChunkOffset;

				explicit Header();
			};
		}
	}
}
