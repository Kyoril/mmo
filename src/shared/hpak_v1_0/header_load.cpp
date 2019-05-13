// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "header_load.h"
#include "header.h"
#include "binary_io/reader.h"
#include "base/io_array.h"


namespace mmo
{
	namespace hpak
	{
		namespace v1_0
		{
			static bool loadFile(
			    FileEntry &file,
			    io::Reader &reader,
			    uint32 /*version*/)
			{
				return reader
				       >> io::read_container<uint16>(file.name)
				       >> io::read<uint16>(file.compression)
				       >> io::read<uint64>(file.contentOffset)
				       >> io::read<uint64>(file.size)
				       >> io::read<uint64>(file.originalSize)
				       >> io::read_range(file.digest)
				       ;
			}

			bool loadHeader(Header &header, io::Reader &reader)
			{
				size_t fileCount = 0;
				reader >> io::read<uint32>(fileCount);

				for (size_t i = 0; i < fileCount; ++i)
				{
					FileEntry file;

					if (!loadFile(file, reader, header.version))
					{
						return false;
					}

					header.files.push_back(std::move(file));
				}

				return true;
			}
		}
	}
}
