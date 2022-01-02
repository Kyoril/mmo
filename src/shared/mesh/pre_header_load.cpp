// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "pre_header_load.h"
#include "pre_header.h"
#include "magic.h"
#include "binary_io/reader.h"
#include "base/io_array.h"


namespace mmo
{
	namespace mesh
	{
		bool LoadPreHeader(PreHeader &preHeader, io::Reader &reader)
		{
			{
				std::array<char, 4> magic;
				reader >> read_array<char>(magic);

				if (magic != FileBeginMagic)
				{
					return false;
				}
			}

			reader >> io::read<uint32>(preHeader.version);
			return reader;
		}
	}
}
