// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "file_system_reader.h"

#include <fstream>

namespace mmo
{
	namespace virtual_dir
	{
		FileSystemReader::FileSystemReader(std::filesystem::path directory)
			: m_directory(std::move(directory))
		{
		}

		file_type::Enum FileSystemReader::getType(
		    const Path &fileName
		)
		{
			auto fullPath = m_directory / fileName;
			const auto type = 
				std::filesystem::status(m_directory / fileName).type();

			
			switch (type)
			{
			case std::filesystem::file_type::regular:
				return file_type::File;
			case std::filesystem::file_type::directory:
				return file_type::Directory;
			default:
				throw std::runtime_error("Unsupported physical file type: " + fullPath.string());
			}
		}

		std::unique_ptr<std::istream> FileSystemReader::readFile(
		    const Path &fileName,
		    bool openAsText)
		{
			const auto fullPath = m_directory / fileName;

			std::unique_ptr<std::istream> file(new std::ifstream(
			                                       fullPath.string().c_str(),
			                                       openAsText ? std::ios::in : std::ios::binary));

			if (static_cast<std::ifstream &>(*file))
			{
				return std::move(file);
			}

			return std::unique_ptr<std::istream>();
		}

		std::set<Path> FileSystemReader::queryEntries(
		    const Path &fileName)
		{
			std::set<Path> entries;
			const auto fullPath = m_directory / fileName;

			for (std::filesystem::directory_iterator i(fullPath); i != std::filesystem::directory_iterator(); ++i)
			{
				entries.insert(
				    i->path().filename().string()
				);
			}

			return entries;
		}
	}
}
