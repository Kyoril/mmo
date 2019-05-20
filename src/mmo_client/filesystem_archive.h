
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
	};

}
