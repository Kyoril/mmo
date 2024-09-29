// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include <string>

namespace mmo
{
	namespace virtual_dir
	{
		typedef std::string Path;

		const char PathSeparator = '/';


		void appendPath(Path &left, const Path &right);
		Path joinPaths(Path left, const Path &right);
		std::pair<Path, Path> splitLeaf(Path path);
		std::pair<Path, Path> splitRoot(Path path);
	}
}
