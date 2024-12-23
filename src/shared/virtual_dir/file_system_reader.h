// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "reader.h"

#include "base/filesystem.h"


namespace mmo
{
	namespace virtual_dir
	{
		struct FileSystemReader : IReader
		{
			explicit FileSystemReader(std::filesystem::path directory);
			virtual file_type::Enum getType(
			    const Path &fileName
			) override;
			virtual std::unique_ptr<std::istream> readFile(
			    const Path &fileName,
			    bool openAsText
			) override;
			virtual std::set<Path> queryEntries(
			    const Path &fileName
			) override;

		private:

			std::filesystem::path m_directory;
		};
	}
}
