// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "file_entry_handler.h"
#include "prepared_update.h"
#include "hpak_v1_0/magic.h"

#include <fstream>
#include <unordered_map>


namespace mmo::updating
{
	namespace hpak2
	{
		struct FileInArchive
		{
			std::string path;
			std::uintmax_t compressedSize, originalSize, offset;
			hpak::v1_0::CompressionType compression;
			SHA1Hash sha1;

			FileInArchive();
		};

		struct RequiredFile
		{
			std::string sourcePath;
			std::string archivePath;
			std::uintmax_t originalSize;
			std::uintmax_t compressedSize;
			SHA1Hash sha1;
			FileInArchive *present;
			hpak::v1_0::CompressionType compression;

			RequiredFile();
		};

		struct HPAK2EntryHandler : IFileEntryHandler
		{
			explicit HPAK2EntryHandler(std::string archivePath);

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

		private:

			typedef std::unordered_map<std::string, FileInArchive> ArchiveFilesByName;


			struct UpdateState
			{
				ArchiveFilesByName archiveFiles;
				std::vector<RequiredFile> requiredFiles;
			};

			const std::string m_archivePath;
			std::ifstream m_archiveFile;
			bool m_isChangeNecessary;
			PreparedUpdate::Estimates m_estimates;
			std::shared_ptr<UpdateState> m_updateState;
		};
	}
}
