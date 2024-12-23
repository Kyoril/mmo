// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "extract.h"

#include "hpak/pre_header.h"
#include "hpak/pre_header_load.h"
#include "hpak_v1_0/header.h"
#include "hpak_v1_0/header_load.h"
#include "hpak_v1_0/read_content_file.h"
#include "hpak_v1_0/allocation_map.h"
#include "binary_io/stream_source.h"
#include "binary_io/reader.h"

#include <iostream>
#include <fstream>

#include "zstr/zstr.hpp"


namespace mmo
{
	using namespace hpak;


	namespace details
	{
		/// Extracts all files from the archive.
		bool extractFiles(
		    std::istream &archive,
		    const hpak::v1_0::Header &header,
		    const std::filesystem::path &destination,
		    const ExtractionProgressCallback &callback,
		    hpak::AllocationMap &allocator,
		    std::uintmax_t headerSize
		)
		{
			using namespace v1_0;

			bool success = true;

			/// Iterate through each file. We use a for loop instead of foreach so that we can
			/// report back progress.
			for (size_t i = 0, c = header.files.size(); i < c ; ++i)
			{
				// Verify the file entry first
				const auto &fileEntry = header.files[i];
				if (fileEntry.contentOffset < headerSize)
				{
					std::cerr
					        << fileEntry.name
					        << " (offset " << fileEntry.contentOffset << ")"
					        << " seems to overlap with the header (" << headerSize << ")\n";
				}
				else if (!allocator.Reserve(
				             fileEntry.contentOffset,
				             fileEntry.size))
				{
					std::cerr
					        << fileEntry.name
					        << " seems to overlap with some other file\n";
				}

				// Report progress
				const auto fileDest = destination / fileEntry.name;
				callback(static_cast<double>(i) / static_cast<double>(c), fileDest);

				// Try to create directory structure for the file
				try
				{
					std::filesystem::create_directories(fileDest.parent_path());
				}
				catch (const std::filesystem::filesystem_error &e)
				{
					std::cerr << e.what() << "\n";
					success = false;
					continue;
				}

				// Open the actual file for writing
				std::ofstream file(fileDest.string(), std::ios::binary);
				if (!file)
				{
					std::cerr << "Cannot open " << fileDest << "\n";
					success = false;
					continue;
				}

				// Prepare a stream which contains the file contents
				ContentFileReader fileReader(
				    header,
				    fileEntry,
				    archive);

				// Read from the prepared stream
				std::istream& inStream = fileReader.GetContent();

				// Read file contents step by step in small chunks. Seems to work best for uncompression
				// with this buffer size.
				constexpr std::streamsize buff_size = 1 << 16;
				char buff[buff_size];

				while (true)
				{
					inStream.read(buff, buff_size);
					std::streamsize cnt = inStream.gcount();
					if (cnt == 0) break;
					file.write(buff, cnt);
				}
			}

			return success;
		}
	}


	bool Extract(
	    std::istream &archive,
	    const std::filesystem::path &destination,
	    const ExtractionProgressCallback &callback
	)
	{
		io::StreamSource archiveSource(archive);
		io::Reader archiveReader(archiveSource);

		// Remember the location before the header. Note that the archive stream
		// might be an archive stream inside of another hpak archive, so it does
		// not have to match the beginning of the stream.
		const auto beforeHeader = archive.tellg();

		// Read the common hpak header and validate it
		PreHeader preHeader;
		if (!loadPreHeader(preHeader, archiveReader))
		{
			std::cerr << "Failed to load the common hpak archive header, the file might not be an hpak archive or it might be damaged!\n";
			return false;
		}

		// Read the rest of the archive, depending on the archive version.
		switch (preHeader.version)
		{
		case Version_1_0:
			{
				// Read the v1.0 specific header data
				v1_0::Header header(preHeader.version);
				if (!loadHeader(header, archiveReader))
				{
					std::cerr << "Failed to load the v1.0 hpak archive header!\n";
					return false;
				}

				// Calculate the actual header size in bytes
				const auto afterHeader = archive.tellg();
				const std::uintmax_t headerSize = (afterHeader - beforeHeader);

				// Create an allocation map instance. This is used to validate the archive (for example
				// check for overlaps).
				hpak::AllocationMap allocator;
				allocator.Reserve(0, headerSize);

				// Extract all files
				return details::extractFiles(
				           archive,
				           header,
				           destination,
				           callback,
				           allocator,
				           headerSize
				       );
			}

		default:
			std::cerr << "Unknown archive version " << preHeader.version << "\n";
			return false;
		}
	}
}
