// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "file_system_writer.h"

#include <fstream>

namespace mmo
{
	namespace virtual_dir
	{
		FileSystemWriter::FileSystemWriter(std::filesystem::path directory)
			: m_directory(std::move(directory))
		{
		}

		std::unique_ptr<std::ostream> FileSystemWriter::writeFile(
		    const Path &fileName,
		    bool openAsText,
		    bool createDirectories
		)
		{
			const auto fullPath = (m_directory / fileName);
			if (createDirectories)
			{
				std::filesystem::create_directories(
				    fullPath.parent_path()
				);
			}

			std::unique_ptr<std::ostream> file(new std::ofstream(
			                                       fullPath.string(),
			                                       (openAsText ? std::ios::out : std::ios::binary)));

			if (*file)
			{
				return file;
			}

			return std::unique_ptr<std::ostream>();
		}
	}
}
