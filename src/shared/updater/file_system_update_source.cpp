#include "file_system_update_source.h"

#include <fstream>

namespace mmo
{
	namespace updating
	{
		FileSystemUpdateSource::FileSystemUpdateSource(std::filesystem::path root)
			: m_root(std::move(root))
		{
		}

		UpdateSourceFile FileSystemUpdateSource::readFile(
		    const std::string &path
		)
		{
			//TODO: filter paths with ".."
			const auto fullPath = m_root / path;
			std::unique_ptr<std::istream> file(new std::ifstream(
			                                       fullPath.string(), std::ios::binary));

			if (!static_cast<std::ifstream &>(*file))
			{
				throw std::runtime_error("File not found: " + fullPath.string());
			}

			//intentionally left empty
			std::any internalData;
			std::optional<std::uintmax_t> size;

			return UpdateSourceFile(internalData, std::move(file), size);
		}
	}
}
