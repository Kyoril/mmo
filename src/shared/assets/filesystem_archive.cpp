// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "filesystem_archive.h"

#include "log/default_log_levels.h"


namespace mmo
{
	FileSystemArchive::FileSystemArchive(const std::string & name)
		: m_name(name)
		, m_reader(name)
		, m_writer(name)
	{
	}

	void FileSystemArchive::Load()
	{
		// Try to create the directory in case it is empty
		if (!std::filesystem::exists(m_name))
		{
			std::filesystem::create_directories(m_name);
		}

	}

	void FileSystemArchive::Unload()
	{
		// Nothing to do here
	}

	const std::string & FileSystemArchive::GetName() const
	{
		return m_name;
	}

	ArchiveMode FileSystemArchive::GetMode() const
	{
		return ArchiveMode::ReadWrite;
	}

	std::unique_ptr<std::istream> FileSystemArchive::Open(const std::string & filename)
	{
		return m_reader.readFile(filename, false);
	}

	void FileSystemArchive::EnumerateFiles(std::vector<std::string>& files)
	{
		EnumerateFilesImpl(m_name, "", files);
	}

	void FileSystemArchive::EnumerateFilesImpl(const virtual_dir::Path & root, const virtual_dir::Path& relPath, std::vector<std::string>& files)
	{
		// Iterate through all files
		for (const auto& entry : m_reader.queryEntries(root))
		{
			std::filesystem::path path = root;
			//path /= relPath;

			if (std::filesystem::is_directory(path / entry))
			{
				EnumerateFilesImpl(
					(path / entry).generic_string(),
					(std::filesystem::path(relPath) / entry).generic_string(),
					files);
			}
			else
			{
				files.push_back(relPath + "/" + entry);
			}
		}
	}
}
