// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "header_load.h"
#include "header.h"
#include "binary_io/reader.h"
#include "base/io_array.h"


namespace mmo::mesh::v1_0
{
	bool LoadHeader(Header &header, io::Reader &reader)
	{
		reader
			>> io::read<uint32>(header.vertexChunkOffset)
			>> io::read<uint32>(header.indexChunkOffset);

		return reader;
	}
}
