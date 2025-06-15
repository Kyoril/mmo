// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "describe.h"

#include "hpak/pre_header.h"
#include "hpak/pre_header_load.h"
#include "hpak_v1_0/header.h"
#include "hpak_v1_0/header_load.h"
#include "hpak_v1_0/allocation_map.h"
#include "binary_io/stream_source.h"
#include "binary_io/reader.h"
#include "base/macros.h"

#include "simple_file_format/sff_write_table.h"
#include "simple_file_format/sff_write_array.h"

#include <stdexcept>


namespace mmo
{
	using namespace hpak;


	void Describe(
	    std::istream &archive,
	    std::ostream &description
	)
	{
		const auto beforeHeader = archive.tellg();
		io::StreamSource archiveSource(archive);
		io::Reader archiveReader(archiveSource);

		// Load the basic archive header from the given file stream
		PreHeader preHeader;
		if (!loadPreHeader(preHeader, archiveReader))
		{
			ASSERT(! "Archive version coult not be detected");
		}

		// Depending on the header version, describe the hpak archive.
		switch (preHeader.version)
		{
		case Version_1_0:
			{
				// Now load the v1.0 specific header file informations
				v1_0::Header header(preHeader.version);
				if (!loadHeader(header, archiveReader))
				{
					ASSERT(! "The header does not conform to HPAK 1.0");
				}

				// Setup an sff writer
				sff::write::Writer<char> writer(description);
				sff::write::Table<char> root(writer, sff::write::MultiLine);

				// Add the header size in bytes
				{
					const auto afterHeader = archive.tellg();
					root.addKey("headerSize", static_cast<std::uintmax_t>(afterHeader - beforeHeader));
				}

				// Create file array
				sff::write::Array<char> fileArray(root, "files", sff::write::MultiLine);

				// For each file, add a new entry to the file array above
				for (const auto & fileEntry : header.files)
				{
					// First, format the file hash to a hex string
					std::stringstream hashStrm;
					sha1PrintHex(hashStrm, fileEntry.digest);

					// Add the file entry table
					sff::write::Table<char> entryTable(fileArray, sff::write::Comma);
					entryTable.addKey("name", fileEntry.name);
					entryTable.addKey("position", fileEntry.contentOffset);
					entryTable.addKey("size", fileEntry.size);
					entryTable.addKey("originalSize", fileEntry.originalSize);
					entryTable.addKey("digest", hashStrm.str());
					entryTable.Finish();
				}

				fileArray.Finish();
				root.Finish();
			} break;

		default:
			ASSERT(! "Unsupported HPAK version");
		}
	}
}
