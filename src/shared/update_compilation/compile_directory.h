#pragma once

#include "base/filesystem.h"

namespace mmo
{
	namespace virtual_dir
	{
		struct IReader;
		struct IWriter;
	}

	namespace updating
	{
		void compileDirectory(
			virtual_dir::IReader &sourceDir,
			virtual_dir::IWriter &destinationDir,
		    bool isZLibCompressed
		);
	}
}
