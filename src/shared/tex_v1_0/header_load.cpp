// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "header_load.h"
#include "header.h"
#include "binary_io/reader.h"
#include "base/io_array.h"


namespace mmo
{
	namespace tex
	{
		namespace v1_0
		{
			bool loadHeader(Header &header, io::Reader &reader)
			{
				return reader
					>> io::read<uint8>(header.format)
					>> io::read<uint8>(header.hasMips)
					>> io::read<uint16>(header.width)
					>> io::read<uint16>(header.height)
					>> io::read_range(header.mipmapOffsets)
					>> io::read_range(header.mipmapLengths);
			}
		}
	}
}
