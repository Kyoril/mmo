// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "writer.h"

#include "base/filesystem.h"


namespace mmo
{
	namespace virtual_dir
	{
		struct FileSystemWriter : IWriter
		{
			explicit FileSystemWriter(std::filesystem::path directory);
			virtual std::unique_ptr<std::ostream> writeFile(
			    const Path &fileName,
			    bool openAsText,
			    bool createDirectories
			) override;

		private:

			std::filesystem::path m_directory;
		};
	}
}
