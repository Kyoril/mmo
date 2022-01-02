// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "base/sha1.h"
#include "magic.h"
#include "hpak/magic.h"

#include <vector>


namespace mmo
{
	namespace hpak
	{
		namespace v1_0
		{
			struct FileEntry
			{
				std::string name;
				CompressionType compression;
				uint64 contentOffset;
				uint64 size;
				uint64 originalSize;
				SHA1Hash digest;


				FileEntry();
			};


			struct Header
			{
				typedef std::vector<FileEntry> Files;


				VersionId version;
				Files files;


				explicit Header(VersionId version);
			};
		}
	}
}
