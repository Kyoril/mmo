#pragma once

#include "file_entry_handler.h"


namespace mmo::updating
{
	struct FileSystemEntryHandler : IFileEntryHandler
	{
		virtual PreparedUpdate handleDirectory(
		    const PrepareParameters &parameters,
		    const UpdateListProperties &listProperties,
		    const sff::read::tree::Array<std::string::const_iterator> &entries,
		    const std::string &type,
		    const std::string &source,
		    const std::string &destination) override;

		virtual PreparedUpdate handleFile(
		    const PrepareParameters &parameters,
		    const sff::read::tree::Table<std::string::const_iterator> &entryDescription,
		    const std::string &source,
		    const std::string &destination,
		    std::uintmax_t originalSize,
		    const SHA1Hash &sha1,
		    const std::string &compression,
		    std::uintmax_t compressedSize
		) override;

		virtual PreparedUpdate finish(const PrepareParameters &parameters) override;
	};
}
