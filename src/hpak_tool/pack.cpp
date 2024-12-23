// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "pack.h"

#include "hpak/pre_header.h"
#include "hpak/pre_header_save.h"
#include "hpak_v1_0/header.h"
#include "hpak_v1_0/header_save.h"
#include "binary_io/stream_sink.h"
#include "binary_io/writer.h"
#include "base/sha1.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

#include "zstr/zstr.hpp"

namespace mmo
{
	using namespace hpak;
	using namespace v1_0;

	namespace
	{
		/// This struct contains data of a file that has been found for packing.
		struct FoundFile
		{
			/// The absolute file path.
			std::filesystem::path source;
			/// The file name for the archive (relative to the root directory).
			std::string name;
		};


		/// Gathers all files of the given directory. This function is called recursively.
		std::vector<FoundFile> GatherAllFiles(
		    const std::filesystem::path &directory,
		    const std::string &directoryName
		)
		{
			// Will contain all file entries that have been found
			std::vector<FoundFile> files;

			try
			{
				// Parse all file system entries (files and folders)
				auto pos = std::filesystem::directory_iterator(directory);
				const auto end = std::filesystem::directory_iterator();

				for (; pos != end; ++pos)
				{
					try
					{
						// If we found a folder, gather files from there as well
						if (is_directory(*pos))
						{
							// First, add the name of the folder to the relative filename string
							std::string subDirName;
							if (!directoryName.empty())
							{
								subDirName = directoryName + "/";
							}
							subDirName += pos->path().filename().string();

							// Recursive function call, insert results into the files array
							const auto directoryFiles = GatherAllFiles(*pos, subDirName);
							files.insert(files.end(),
							             directoryFiles.begin(),
							             directoryFiles.end());
						}
						else if (is_regular_file(*pos))
						{
							// Regular file found, create a new entry and build the relative file name
							// used in the archive file
							FoundFile file;
							file.source = *pos;

							// Make sure the directory name ends with a separator character
							if (!directoryName.empty())
							{
								file.name = directoryName + "/";
							}

							file.name += pos->path().filename().string();
							files.push_back(file);
						}
					}
					catch (const std::filesystem::filesystem_error &e)
					{
						// In case of a filesystem error, print that to cerr
						std::cerr << e.what() << "\n";
					}
				}
			}
			catch (const std::filesystem::filesystem_error &e)
			{
				// In case of an iteration error, print that to cerr
				std::cerr << e.what() << "\n";
			}

			return files;
		}
	}


	bool Pack(
	    std::ostream &archive,
	    const std::filesystem::path &source,
	    bool isCompressionEnabled,
	    const PathFilter &inclusionFilter,
	    const PackProgressCallback &callback
	)
	{
		bool success = true;

		// First, gather all files from the path
		auto files = GatherAllFiles(source, std::string());
		{
			// Find files that should be ignored using the inclusion filter
			const auto ignored = std::partition(
			                         files.begin(),
			                         files.end(),
			                         [&inclusionFilter](const FoundFile & found)
			{
				return inclusionFilter(found.source);
			});

			// Notify the user about files that are ignored
			for (auto i = ignored; i != files.end(); ++i)
			{
				std::cerr << "Ignoring " << i->source << "\n";
			}

			// And remove the ignored files
			files.erase(
			    ignored,
			    files.end());
		}

		// Create an io writer object linked to the output file
		io::StreamSink archiveSink(archive);
		io::Writer archiveWriter(archiveSink);

		// Create the v1.0 header saver to write the hpack header
		v1_0::HeaderSaver saver(archiveSink);
		saver.finish(static_cast<uint32>(files.size()));

		// Allocate entry savers used to save file content
		std::vector<std::unique_ptr<FileEntrySaver>> entrySavers;
		entrySavers.reserve(files.size());

		// Iterate through all files and write their content
		for (const auto & found : files)
		{
			entrySavers.push_back(std::make_unique<FileEntrySaver>(
				archiveSink,
				found.name,
				isCompressionEnabled ? hpak::v1_0::ZLibCompressed : hpak::v1_0::NotCompressed));
		}

		// Remember the current offset, since this is where file content actually starts
		uint64 offset = static_cast<uint64>(archive.tellp());

		// Iterate through all files
		for (size_t i = 0, c = files.size(); i < c; ++i)
		{
			// Report progress using the callback
			const auto &fileEntry = files[i];
			callback(static_cast<double>(i) / static_cast<double>(c), fileEntry.source);

			// Open t he file that should be added for reading, starting at the end of the
			// file so that it's size can be determined right away
			std::ifstream file(fileEntry.source.string(), std::ios::binary | std::ios::ate);
			if (!file)
			{
				std::cerr << "Cannot read file " << fileEntry.source << "\n";
				success = false;
				continue;
			}

			// Obtain the file size
			const uint64 originalFileSize = static_cast<uint64>(file.tellg());
			file.seekg(0, std::ios::beg);

			// Will generate the sha1 hash of the input file for us while we read
			// that file in
			HashGeneratorSha1 hashGen;

			// Remember old archive position; this is used to calculate the new file size
			// after it has been written.
			const uint64 archiveSizeBefore = static_cast<uint64>(archive.tellp());

			// Generate the output stream with compression if requested
			std::unique_ptr<std::ostream> outStream;
			if (!isCompressionEnabled)
			{
				outStream = std::make_unique<std::ostream>(archive.rdbuf());
			}
			else
			{
				outStream = std::make_unique<zstr::ostream>(archive);
			}

			// Read in source file in 4k byte chunks and process it
			char buf[4096];
			std::streamsize read;
			do
			{
				file.read(buf, 4096);
				read = file.gcount();
				if (read > 0)
				{
					hashGen.update(buf, read);
					outStream->write(buf, read);
				}
			} while (file && read > 0);

			// Flush the output stream
			outStream->flush();
			outStream.reset();
			
			// Calculate actual file size (maybe file has been compressed)
			const uint64 archiveSizeAfter = static_cast<uint64>(archive.tellp());
			const uint64 packedFileSize = (archiveSizeAfter - archiveSizeBefore);

			// Finalize sha1 hash calculation
			const SHA1Hash digest = hashGen.finalize();
			entrySavers[i]->finish(
			    offset,
			    packedFileSize,
			    originalFileSize,
			    digest
			);

			// Increase file content offset
			offset += packedFileSize;
		}

		return success;
	}
}
