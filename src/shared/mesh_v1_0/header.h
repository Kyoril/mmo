// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "mesh/magic.h"
#include "mesh/chunk_writer.h"


namespace mmo
{
	namespace mesh
	{
		namespace v1_0
		{
			static const ChunkMagic MeshChunkMagic = { {'M', 'E', 'S', 'H'} };
			static const ChunkMagic SubMeshChunkMagic = { {'S', 'U', 'B', 'M'} };
			static const ChunkMagic VertexChunkMagic = { {'V', 'E', 'R', 'T'} };
			static const ChunkMagic IndexChunkMagic = { {'I', 'N', 'D', 'X'} };

			
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
