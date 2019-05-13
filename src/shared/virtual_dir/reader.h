#pragma once

#include "path.h"

#include <memory>
#include <istream>
#include <set>


namespace mmo
{
	namespace virtual_dir
	{
		namespace file_type
		{
			enum Enum
			{
			    File,
			    Directory
			};
		}


		struct IReader
		{
			virtual ~IReader();
			virtual file_type::Enum getType(
			    const Path &fileName
			) = 0;
			virtual std::unique_ptr<std::istream> readFile(
			    const Path &fileName,
			    bool openAsText //TODO enum for this
			) = 0;
			virtual std::set<Path> queryEntries(
			    const Path &fileName
			) = 0;
		};
	}
}

