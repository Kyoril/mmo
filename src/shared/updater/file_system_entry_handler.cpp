// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "file_system_entry_handler.h"
#include "prepare_parameters.h"
#include "prepare_progress_handler.h"
#include "update_source.h"
#include "copy_with_progress.h"
#include "parse_directory_entries.h"
#include "hpak2_entry_handler.h"
#include "base/filesystem.h"


namespace mmo::updating
{
	PreparedUpdate FileSystemEntryHandler::handleDirectory(
	    const PrepareParameters &parameters,
	    const UpdateListProperties &listProperties,
	    const sff::read::tree::Array<std::string::const_iterator> &entries,
	    const std::string &type,
	    const std::string &source,
	    const std::string &destination)
	{
		if (type == "fs" ||
		    (type == "hpak2" && parameters.doUnpackArchives))
		{
			std::filesystem::create_directories(destination);
			return parseDirectoryEntries(
			           parameters,
			           listProperties,
			           source,
			           destination,
			           entries,
			           *this
			       );
		}

		if (type == "hpak2")
		{
			std::vector<PreparedUpdate> updates;

			hpak2::HPAK2EntryHandler hpakHandler(destination);
			updates.push_back(parseDirectoryEntries(
			                      parameters,
			                      listProperties,
			                      source,
			                      "",
			                      entries,
			                      hpakHandler
			                  ));
			updates.push_back(hpakHandler.finish(parameters));
			return accumulate(std::move(updates));
		}

		throw std::runtime_error(
		    "Unknown file system entry type: " + type);
	}

	PreparedUpdate FileSystemEntryHandler::handleFile(
	    const PrepareParameters &parameters,
	    const sff::read::tree::Table<std::string::const_iterator> &/*entryDescription*/,
	    const std::string &source,
	    const std::string &destination,
	    std::uintmax_t originalSize,
	    const SHA1Hash &sha1,
	    const std::string &compression,
	    std::uintmax_t compressedSize
	)
	{
		for (;;)
		{
			{
				parameters.progressHandler.beginCheckLocalCopy(source);

				//TODO: prevent race condition
				std::ifstream previousFile(
				    destination,
				    std::ios::binary | std::ios::ate);

				if (previousFile)
				{
					const std::uintmax_t previousSize = previousFile.tellg();
					if (previousSize == originalSize)
					{
						previousFile.seekg(0, std::ios::beg);
						const auto previousDigest = mmo::sha1(previousFile);
						if (previousDigest == sha1)
						{
							break;
						}
					}
				}
			}

			PreparedUpdate update;
			update.estimates.downloadSize = compressedSize;
			update.estimates.updateSize = originalSize;
			update.steps.push_back(PreparedUpdateStep(
			                           destination,
			                           [source, destination, compression, compressedSize, originalSize]
			                           (const UpdateParameters & parameters) -> bool
			{
				const auto sourceFile = parameters.source->readFile(source);
				checkExpectedFileSize(source, compressedSize, sourceFile);

				bool doZLibUncompress = false;

				if (compression == "zlib")
				{
					doZLibUncompress = true;
				}
				else if (!compression.empty())
				{
					throw std::runtime_error(
					    "Unsupported compression type " + compression);
				}

				std::ofstream sinkFile(destination, std::ios::binary);
				if (!sinkFile)
				{
					throw std::runtime_error("Could not open output file " + destination);
				}

				copyWithProgress(
				    parameters,
				    *sourceFile.content,
				    sinkFile,
				    source,
				    originalSize,
					originalSize,
				    doZLibUncompress
				);

				//TODO: Copy step-wise
				return false;
			}));

			return update;
		}

		return PreparedUpdate();
	}

	PreparedUpdate FileSystemEntryHandler::finish(const PrepareParameters &/*parameters*/)
	{
		return PreparedUpdate();
	}
}
