// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "header.h"


namespace mmo
{
	namespace mesh
	{
		namespace v1_0
		{
			Header::Header()
				: version(Version_1_0)
				, vertexChunkOffset(0)
				, indexChunkOffset(0)
			{
			}
		}
	}
}
