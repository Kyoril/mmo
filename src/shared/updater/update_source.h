// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "update_source_file.h"

#include <cstdint>
#include <string>
#include <vector>


namespace mmo::updating
{
	/// A file an update step is going to read from the source.
	struct RemoteFile
	{
		std::string path;
		/// The size as stored on the source, i.e. compressed if it is compressed.
		std::uintmax_t size = 0;
	};

	struct IUpdateSource
	{
		virtual ~IUpdateSource() = default;
		virtual UpdateSourceFile readFile(
		    const std::string &path
		) = 0;

		/// Announces files that will be read soon, in the order they will most likely be
		/// read. A source may start fetching them in the background so that readFile
		/// finds them ready. Purely an optimization: readFile must work the same for
		/// files that were never announced. The default ignores the hint.
		virtual void prefetch(const std::vector<RemoteFile> &/*files*/)
		{
		}
	};
}
