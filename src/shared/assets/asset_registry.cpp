// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "asset_registry.h"
#include "filesystem_archive.h"
#ifndef _DEBUG
#	include "hpak_archive.h"
#endif

#include "log/default_log_levels.h"
#include "base/utilities.h"
#include "base/macros.h"

#include <list>
#include <map>


namespace mmo
{
	/// The base path for assets.
	static std::filesystem::path s_basePath;
	/// The list of loaded archives.
	static std::list<std::shared_ptr<IArchive>> s_archives;
	/// Hashed file name list
	static std::map<std::string, std::shared_ptr<IArchive>, StrCaseIComp> s_files;
	/// The filesystem archive pointing to the base path.
	static std::shared_ptr<FileSystemArchive> s_filesystemArchive;


	void AssetRegistry::Initialize(const std::filesystem::path& basePath, const std::vector<std::string>& archives)
	{
		ASSERT(s_archives.empty());
		ASSERT(s_files.empty());

		// Log asset registry initialization
		ILOG("Initializing asset registry with base path " << basePath);

		// Remember base path
		s_basePath = std::filesystem::absolute(basePath);

		// In debug builds, we skip loading hpak archives as debug builds are not 
		// for distribution, which is the only purpose of hpak files. Also, even in
		// release builds, we allow loading files from the file system.
		// Iterate through files
		for (const std::string& file : archives)
		{
			// Check if the file exists
			auto archivePath = s_basePath / file;
			if (std::filesystem::exists(archivePath))
			{
				// Add archive to the list of archives
				if (archivePath.extension() == ".hpak")
				{
#ifndef _DEBUG
					s_archives.emplace_front(std::make_shared<HPAKArchive>(archivePath.string()));
#endif
				}
				else
				{
					s_archives.emplace_front(std::make_shared<FileSystemArchive>(archivePath.string()));
				}
			}
		}

		// Finally, add the file system archive
		s_filesystemArchive = std::make_shared<FileSystemArchive>(s_basePath.string());
		s_archives.emplace_front(s_filesystemArchive);

		// Iterate through all archives
		for (auto& archive : s_archives)
		{
			// Load the archive
			archive->Load();

			// And enumerate all files the archive contains
			std::vector<std::string> archiveFiles;
			archive->EnumerateFiles(archiveFiles);

			// Iterate through the archive files
			for (const auto& file : archiveFiles)
			{
				// If the file hasn't been tracked yet, we will add it to the list
				// with the current archive that will be used to load the file on 
				// request
				if (s_files.find(file) == s_files.end())
				{
					s_files[file] = archive;
				}
			}
		}

		// Log file count
		DLOG("Asset registry: " << s_files.size() << " files");
	}

	void AssetRegistry::Destroy()
	{
		// Clear file list
		s_files.clear();

		// Remove all archives
		s_archives.clear();

		// Reset filesystem archive
		s_filesystemArchive.reset();
	}

	std::unique_ptr<std::istream> AssetRegistry::OpenFile(const std::string & filename)
	{
		// Try to find the requested file
		const auto it = s_files.find(filename);
		if (it == s_files.end())
		{
			return nullptr;
		}

		// Open file from archive
		return it->second->Open(filename);
	}

	bool AssetRegistry::HasFile(const std::string& filename)
	{
		// Try to find the requested file
		const auto it = s_files.find(filename);
		return it != s_files.end();
	}

	std::unique_ptr<std::ostream> AssetRegistry::CreateNewFile(const std::string& filename)
	{
		ASSERT(s_filesystemArchive != nullptr);
		
		// Creating a file always creates a real physical file as HPAK is not yet supported
		auto filePtr = s_filesystemArchive->Create(filename);
		if (filePtr != nullptr)
		{
			// Register file
			auto archive = std::static_pointer_cast<IArchive>(s_filesystemArchive);
			s_files.emplace(filename, std::move(archive));

			DLOG("Successfully created new file " << filename << " in asset registry");
		}

		return filePtr;
	}
}
