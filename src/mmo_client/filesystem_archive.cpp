// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "filesystem_archive.h"


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
		// Nothing to do here
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
}
