// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "asset_registry.h"
#include "filesystem_archive.h"
#include "hpak_archive.h"

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

	std::mutex AssetRegistry::s_fileLock{};


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
					s_archives.emplace_front(std::make_shared<HPAKArchive>(archivePath.string()));
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
				if (!s_files.contains(file))
				{
					s_files[file] = archive;
				}
			}
		}

		// Log file count
		DLOG("Asset registry: " << s_files.size() << " files");
	}

	void AssetRegistry::AddArchivePackage(const std::filesystem::path& path)
	{
		std::shared_ptr<IArchive> archive;

		auto archivePath = s_basePath / path;
		if (std::filesystem::exists(archivePath))
		{
			// Add archive to the list of archives
			if (archivePath.extension() == ".hpak")
			{
				archive = std::make_shared<HPAKArchive>(archivePath.string());
			}
			else
			{
				archive = std::make_shared<FileSystemArchive>(archivePath.string());
			}
		}

		if (!archive)
		{
			return;
		}

		s_archives.push_front(archive);
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

	void AssetRegistry::Destroy()
	{
		// Clear file list
		s_files.clear();

		// Remove all archives
		s_archives.clear();

		// Reset filesystem archive
		s_filesystemArchive.reset();
	}

	std::unique_ptr<std::istream> AssetRegistry::OpenFile(std::string filename)
	{
		std::unique_lock lock { s_fileLock };

		std::transform(filename.begin(), filename.end(), filename.begin(), [](const char c)
		{
			return c == '\\' ? '/' : c;
		});

		// Try to find the requested file
		const auto it = s_files.find(filename);
		if (it == s_files.end())
		{
			return std::make_unique<std::ifstream>(filename, std::ios::binary | std::ios::in);
		}

		// Open file from archive
		return it->second->Open(filename);
	}

	bool AssetRegistry::HasFile(const std::string& filename)
	{
		std::unique_lock lock { s_fileLock };

		// Try to find the requested file
		const auto it = s_files.find(filename);
		return it != s_files.end();
	}

	std::unique_ptr<std::ostream> AssetRegistry::CreateNewFile(const std::string& filename)
	{
		std::unique_lock lock { s_fileLock };

		ASSERT(s_filesystemArchive != nullptr);

		std::string converted = filename;

		size_t index;
		while((index = converted.find('\\')) != std::string::npos)
		{
			converted[index] = '/';
		}

		// Creating a file always creates a real physical file as HPAK is not yet supported
		auto filePtr = s_filesystemArchive->Create(converted);
		if (filePtr != nullptr)
		{
			// Register file
			auto archive = std::static_pointer_cast<IArchive>(s_filesystemArchive);
			s_files.emplace(converted, std::move(archive));

			DLOG("Successfully created new file " << converted << " in asset registry");
		}

		return filePtr;
	}

	std::vector<std::string> AssetRegistry::ListFiles()
	{
		std::unique_lock lock{ s_fileLock };
		std::vector<std::string> result;

		for (const auto& file : s_files)
		{
			result.push_back(file.first);
		}

		return result;
	}

	std::vector<std::string> AssetRegistry::ListFiles(const std::string& extension)
	{
		std::unique_lock lock{ s_fileLock };
		std::vector<std::string> result;

		for (const auto& file : s_files)
		{
			if (extension.empty() || file.first.ends_with(extension))
			{
				result.push_back(file.first);
			}
		}

		return result;
	}
}
