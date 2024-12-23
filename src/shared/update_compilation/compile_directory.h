// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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
		/// Compiles a whole directory with all it's files and folders.
		/// @sourceDir A reader object for the source directory.
		/// @destinationDir A writer object for the destination directory.
		/// @param isZLibCompressed True to apply zlib compression on the files.
		void compileDirectory(
			virtual_dir::IReader &sourceDir,
			virtual_dir::IWriter &destinationDir,
		    bool isZLibCompressed
		);
	}
}
