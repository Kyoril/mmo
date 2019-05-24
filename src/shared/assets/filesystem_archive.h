// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#pragma once

#include "archive.h"

#include "virtual_dir/file_system_reader.h"
#include "virtual_dir/file_system_writer.h"


namespace mmo
{
	class FileSystemArchive
		: public IArchive
	{
	private:
		std::string m_name;
		virtual_dir::FileSystemReader m_reader;
		virtual_dir::FileSystemWriter m_writer;

	public:
		FileSystemArchive(const std::string& name);

	public:
		virtual void Load() override;
		virtual void Unload() override;
		virtual const std::string& GetName() const override;
		virtual ArchiveMode GetMode() const override;
		virtual std::unique_ptr<std::istream> Open(const std::string& filename) override;
		virtual void EnumerateFiles(std::vector<std::string>& files) override;

	private:

		void EnumerateFilesImpl(const virtual_dir::Path& root, const virtual_dir::Path& relPath, std::vector<std::string>& files);
	};

}
