#include "hpak2_entry_handler.h"
#include "copy_with_progress.h"
#include "parse_directory_entries.h"
#include "update_source.h"
#include "prepare_parameters.h"
#include "prepare_progress_handler.h"
#include "hpak_v1_0/header_load.h"
#include "hpak_v1_0/header_save.h"
#include "hpak_v1_0/header.h"
#include "hpak_v1_0/allocation_map.h"
#include "hpak/pre_header_load.h"
#include "hpak/pre_header.h"
#include "binary_io/stream_source.h"
#include "binary_io/vector_sink.h"
#include "binary_io/reader.h"
#include "binary_io/writer.h"


namespace mmo
{
	using namespace updating;
	using hpak2::HPAK2EntryHandler;
	using hpak2::FileInArchive;
	using hpak2::RequiredFile;

	FileInArchive::FileInArchive()
		: compressedSize(std::numeric_limits<std::uintmax_t>::max())
		, originalSize(std::numeric_limits<std::uintmax_t>::max())
		, offset(std::numeric_limits<std::uintmax_t>::max())
		, compression(hpak::v1_0::NotCompressed)
	{
	}

	RequiredFile::RequiredFile()
		: originalSize(std::numeric_limits<std::uintmax_t>::max())
		, compressedSize(std::numeric_limits<std::uintmax_t>::max())
		, present(nullptr)
		, compression(hpak::v1_0::NotCompressed)
	{
	}

	namespace
	{
		struct FileUpdatePlan
		{
			FileInArchive entry;
			std::function<void (const UpdateParameters &, std::iostream &)> writeContent;
			size_t requiredFileIndex;

			FileUpdatePlan()
				: requiredFileIndex(std::numeric_limits<size_t>::max())
			{
			}
		};

		typedef std::vector<std::unique_ptr<FileUpdatePlan>> Layout;

		Layout findLayoutForRequiredFiles(
		    const std::vector<RequiredFile> &requiredFiles,
		    std::uintmax_t headerSize
		)
		{
			Layout layout(requiredFiles.size());
			std::generate(begin(layout), end(layout), []()
			{
				return std::unique_ptr<FileUpdatePlan>(new FileUpdatePlan);
			});
			hpak::AllocationMap allocator;

			//reserve space for the header
			const std::uintmax_t headerOffset = 0;
			allocator.reserve(headerOffset, headerSize);

			std::optional<size_t> firstCopiedFileIndex;

			for (size_t i = 0; i < requiredFiles.size(); ++i)
			{
				const auto &requiredFile = requiredFiles[i];

				layout[i]->requiredFileIndex = i;

				FileInArchive &newEntry = layout[i]->entry;
				newEntry.path = requiredFile.archivePath;
				newEntry.compressedSize = requiredFile.compressedSize;
				newEntry.originalSize = requiredFile.originalSize;
				newEntry.compression = requiredFile.compression;
				newEntry.offset = 0; //not assigned
				newEntry.sha1 = requiredFile.sha1;

				if (requiredFile.present)
				{
					const auto &present = *requiredFile.present;
					const auto previousOffset = present.offset;

					if (allocator.reserve(
					            previousOffset,
					            requiredFile.compressedSize))
					{
						newEntry.offset = previousOffset;
					}
					else
					{
						const auto endOfEntry = (previousOffset + requiredFile.compressedSize);
						const auto endOfHeader = (headerOffset + headerSize);

						if (endOfEntry > endOfHeader)
						{
							if (firstCopiedFileIndex)
							{
								//TODO: do something useful
								throw std::runtime_error("Files seem to be overlapping");
							}

							firstCopiedFileIndex = i;
						}
					}

					layout[i]->writeContent =
					    [&newEntry, &present]
					    (const UpdateParameters & /*parameters*/, std::iostream & archive)
					{
						if (newEntry.offset != present.offset)
						{
							//TODO: find better solution for moving content files
							archive.seekg(static_cast<std::streamoff>(present.offset), std::ios::beg);
							std::vector<char> content(static_cast<size_t>(newEntry.compressedSize));
							archive.read(content.data(), static_cast<std::streamsize>(content.size()));
							const std::streamsize result = archive.gcount();
							if ((result < 0) ||
							    (static_cast<size_t>(result) != content.size()))
							{
								throw std::runtime_error(
								    "Could not read content of file " +
								    newEntry.path + " for moving (tellg: " +
								    std::to_string(result) +
								    ", file size: " +
								    std::to_string(content.size()) + ")");
							}
							archive.seekp(static_cast<std::streamoff>(newEntry.offset), std::ios::beg);
							archive.write(content.data(), static_cast<std::streamsize>(content.size()));
						}
					};
				}
				else
				{
					layout[i]->writeContent =
					    [&newEntry, &requiredFile]
					    (const UpdateParameters & parameters, std::iostream & archive)
					{
						auto &source = *parameters.source;
						const auto sourceFile =
						    source.readFile(requiredFile.sourcePath);
						checkExpectedFileSize(
						    requiredFile.sourcePath,
						    requiredFile.compressedSize,
						    sourceFile);

						archive.seekp(static_cast<std::streamoff>(newEntry.offset), std::ios::beg);

						const bool doZLibUncompress =
						    (newEntry.compression == hpak::v1_0::NotCompressed);

						copyWithProgress(
						    parameters,
						    *sourceFile.content,
						    archive,
						    newEntry.path,
						    newEntry.compressedSize,
							newEntry.originalSize,
						    doZLibUncompress
						);
					};
				}
			}

			for (size_t i = 0; i < layout.size(); ++i)
			{
				auto &newEntry = layout[i]->entry;
				if (!newEntry.offset)
				{
					newEntry.offset = allocator.allocate(newEntry.compressedSize);
				}

				ASSERT(layout[i]->writeContent);
			}

			if (firstCopiedFileIndex)
			{
				ASSERT(*firstCopiedFileIndex < layout.size());
				ASSERT(!layout.empty());

				std::swap(layout.front(), layout[*firstCopiedFileIndex]);
			}

			return layout;
		}

		hpak::v1_0::CompressionType decodeCompression(
		    const std::string &compressionName)
		{
			if (compressionName == "zlib")
			{
				return hpak::v1_0::ZLibCompressed;
			}
			else if (compressionName.empty())
			{
				return hpak::v1_0::NotCompressed;
			}
			else
			{
				throw std::runtime_error(
				    "HPAK does not support compression type " + compressionName);
			}
		}
	}


	HPAK2EntryHandler::HPAK2EntryHandler(std::string archivePath)
		: m_archivePath(std::move(archivePath))
		, m_archiveFile(m_archivePath, std::ios::binary)
		, m_isChangeNecessary(false)
		, m_updateState(std::make_shared<UpdateState>())
	{
		if (!m_archiveFile)
		{
			m_isChangeNecessary = true;
			return;
		}

		io::StreamSource archiveSource(m_archiveFile);
		io::Reader archiveReader(archiveSource);

		hpak::PreHeader preHeader;
		if (!hpak::loadPreHeader(preHeader, archiveReader))
		{
			//TODO: ask user
			m_isChangeNecessary = true;
			return;
		}

		if (preHeader.version != hpak::Version_1_0)
		{
			//TODO: ask user
			m_isChangeNecessary = true;
			return;
		}

		hpak::v1_0::Header header(preHeader.version);
		if (!hpak::v1_0::loadHeader(header, archiveReader))
		{
			//TODO: ask user
			m_isChangeNecessary = true;
			return;
		}

		for (const hpak::v1_0::FileEntry & fileEntry : header.files)
		{
			FileInArchive file;
			file.path = fileEntry.name;
			file.compressedSize = fileEntry.size;
			file.originalSize = fileEntry.originalSize;
			file.offset = fileEntry.contentOffset;
			file.compression = fileEntry.compression;
			file.sha1 = fileEntry.digest;

			m_updateState->archiveFiles.insert(std::make_pair(
			                                       fileEntry.name,
			                                       std::move(file)
			                                   ));
		}
	}

	PreparedUpdate HPAK2EntryHandler::handleDirectory(
	    const PrepareParameters &parameters,
	    const UpdateListProperties &listProperties,
	    const sff::read::tree::Array<std::string::const_iterator> &entries,
	    const std::string &type,
	    const std::string &source,
	    const std::string &destination)
	{
		if (type != "fs")
		{
			throw std::runtime_error(source +
			                         ": Only 'fs'-type entries are permitted inside an archive");
		}

		return parseDirectoryEntries(
		           parameters,
		           listProperties,
		           source,
		           destination,
		           entries,
		           *this
		       );
	}

	PreparedUpdate HPAK2EntryHandler::handleFile(
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
		parameters.progressHandler.beginCheckLocalCopy(source);

		RequiredFile file;
		file.present = nullptr;

		{
			const auto found = m_updateState->archiveFiles.find(destination);
			if (found != m_updateState->archiveFiles.end())
			{
				FileInArchive &present = found->second;
				if (sha1 == present.sha1)
				{
					file.present = &present;
				}
				else
				{
					m_isChangeNecessary = true;
				}
			}
			else
			{
				m_isChangeNecessary = true;
			}
		}

		file.sourcePath = source;
		file.archivePath = destination;
		file.originalSize = originalSize;
		file.sha1 = sha1;
		file.compression = decodeCompression(compression);
		file.compressedSize = compressedSize;

		//for raw files, size must equal compressedSize
		ASSERT(
		    (file.compression != hpak::v1_0::NotCompressed) ||
		    (file.originalSize == file.compressedSize));

		m_updateState->requiredFiles.push_back(std::move(file));

		PreparedUpdate update;
		if (!file.present && m_isChangeNecessary)	// Only add if the file needs to be downloaded
		{
			update.estimates.downloadSize = compressedSize;
			update.estimates.updateSize = compressedSize;	// Since the files will be saved compressed with zlib
		}

		return update;
	}

	PreparedUpdate HPAK2EntryHandler::finish(const PrepareParameters &parameters)
	{
		(void)parameters;
		m_archiveFile.close();

		if (!m_isChangeNecessary)
		{
			return PreparedUpdate();
		}

		const auto archivePath = m_archivePath;
		const auto updateState = m_updateState;

		PreparedUpdate update;
		update.estimates = m_estimates;
		update.steps.push_back(PreparedUpdateStep(
		                           archivePath,
		                           [archivePath, updateState](const UpdateParameters & parameters) -> bool
		{
			std::vector<char> serializedHeader;
			io::VectorSink headerSink(serializedHeader);
			std::vector<std::unique_ptr<hpak::v1_0::FileEntrySaver>>
			entrySavers;
			{
				io::Writer headerWriter(headerSink);

				hpak::v1_0::HeaderSaver headerSaver(headerSink);

				for (const auto & fileEntry : updateState->requiredFiles)
				{
					entrySavers.push_back(std::unique_ptr<hpak::v1_0::FileEntrySaver>(
					    new hpak::v1_0::FileEntrySaver(
					        headerSink,
					        fileEntry.archivePath,
					        fileEntry.compression
					    )));
				}

				headerSaver.finish(
				    static_cast<uint32>(entrySavers.size()));
			}

			const std::uintmax_t headerSize = serializedHeader.size();
			const auto newLayout = findLayoutForRequiredFiles(
			    updateState->requiredFiles,
			    headerSize
			);
			ASSERT(newLayout.size() == updateState->requiredFiles.size());

			for (size_t i = 0; i < newLayout.size(); ++i)
			{
				const auto &plan = *newLayout[i];
				const auto &newEntry = plan.entry;
				auto &entrySaver = *entrySavers[plan.requiredFileIndex];
				entrySaver.finish(
				    newEntry.offset,
				    newEntry.compressedSize,
				    newEntry.originalSize,
				    newEntry.sha1
				);
			}

			std::fstream archiveFile(
			    archivePath,
			    std::ios::binary | std::ios::in | std::ios::out);
			if (archiveFile)
			{
				//This is an existing archive so we have to invalidate the
				//existing header before writing anything else.
				//This prevents undetectible archive corruption.
				const std::array<char, 4> InvalidMagic =
				{{
						'U', 'P', 'D', 'T'
				}};
				ASSERT(hpak::FileBeginMagic != InvalidMagic);
				archiveFile.write(InvalidMagic.data(), InvalidMagic.size());

				//We have to make sure that the invalid magic is written to disk
				//before anything else.
				archiveFile.flush();
			}
			else
			{
				archiveFile.open(
				    archivePath,
				    std::ios::binary | std::ios::out);
				if (!archiveFile)
				{
					throw std::runtime_error(
					    archivePath + ": Could not open archive for writing");
				}
			}

			for (const auto & plan : newLayout)
			{
				plan->writeContent(
				    parameters,
				    archiveFile
				);
			}

			//The header is written in the end because otherwise
			//it could overwrite existing file contents.

			const auto magicSize = hpak::FileBeginMagic.size();

			archiveFile.seekp(magicSize, std::ios::beg);
			archiveFile.write(
			    serializedHeader.data() + magicSize,
			    static_cast<std::streamsize>(serializedHeader.size() - magicSize)
			);

			//When everything else has been written to disk, the correct magic
			//marks the archive as valid.
			archiveFile.flush();
			archiveFile.seekp(0, std::ios::beg);
			archiveFile.write(
			    hpak::FileBeginMagic.data(),
			    hpak::FileBeginMagic.size()
			);

			//split up into steps
			return false;
		}));

		return update;
	}
}
