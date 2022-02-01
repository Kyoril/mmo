// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "mesh/magic.h"
#include "base/chunk_writer.h"


namespace mmo
{
	namespace mesh
	{
		namespace v1_0
		{	
			static const ChunkMagic MeshChunkMagic = { {'M', 'E', 'S', 'H'} };
			static const ChunkMagic MeshVertexChunk= { {'V', 'E', 'R', 'T'} };
			static const ChunkMagic MeshIndexChunk= { {'I', 'N', 'D', 'X'} };
			static const ChunkMagic MeshSubMeshChunk = { {'S', 'U', 'B', 'M'} };
			
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
