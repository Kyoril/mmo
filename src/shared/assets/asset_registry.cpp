// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "asset_registry.h"
#include "asset_reference_scan.h"
#include "filesystem_archive.h"
#include "hpak_archive.h"
#include "shared_memory_stream.h"

#include "log/default_log_levels.h"
#include "base/utilities.h"
#include "base/macros.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <list>
#include <map>
#include <set>


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

	namespace
	{
		/// Content read ahead by Prefetch, waiting for its first OpenFile.
		struct PrefetchedFile
		{
			std::shared_ptr<const String> content;
			std::chrono::steady_clock::time_point time;
		};

		/// Upper limit of prefetched content held in memory. Prefetching stops while it is reached.
		constexpr size_t PrefetchByteLimit = 512 * 1024 * 1024;

		/// Prefetched content not opened within this time is dropped. Streamed content is normally opened
		/// within frames of its prefetch; this only bounds what a page that left again never asked for.
		constexpr std::chrono::seconds PrefetchLifetime{ 180 };

		/// Guards the prefetch state below. Never held while reading a file.
		std::mutex s_prefetchLock;

		std::map<String, PrefetchedFile, StrCaseIComp> s_prefetched;

		size_t s_prefetchedBytes = 0;

		AssetRegistry::PrefetchStats s_prefetchStats;

		/// Every file opened so far. Resource managers keep what they loaded, so these are not prefetched.
		std::set<String, StrCaseIComp> s_openedFiles;

		String NormalizeFileName(String filename)
		{
			std::replace(filename.begin(), filename.end(), '\\', '/');
			return filename;
		}

		/// Opens a registered file without touching the prefetch state.
		std::unique_ptr<std::istream> OpenRegisteredFile(const String& filename, std::mutex& fileLock)
		{
			std::shared_ptr<IArchive> archive;
			{
				std::unique_lock lock{ fileLock };
				const auto it = s_files.find(filename);
				if (it == s_files.end())
				{
					return nullptr;
				}

				archive = it->second;
			}

			// Read outside the registry lock: unpacking a large file from an hpak takes milliseconds, and the
			// main thread must not wait for a streaming thread's read to look up a file of its own.
			return archive->Open(filename);
		}

		std::shared_ptr<const String> ReadContent(const String& filename, std::mutex& fileLock)
		{
			const std::unique_ptr<std::istream> file = OpenRegisteredFile(filename, fileLock);
			if (!file || !*file)
			{
				return nullptr;
			}

			auto content = std::make_shared<String>(std::istreambuf_iterator<char>(*file), std::istreambuf_iterator<char>());
			return content;
		}

		bool ShouldPrefetch(const String& filename)
		{
			std::unique_lock lock{ s_prefetchLock };
			return s_prefetchedBytes < PrefetchByteLimit && !s_openedFiles.contains(filename) && !s_prefetched.contains(filename);
		}

		void StorePrefetched(const String& filename, std::shared_ptr<const String> content)
		{
			std::unique_lock lock{ s_prefetchLock };

			const auto now = std::chrono::steady_clock::now();
			for (auto it = s_prefetched.begin(); it != s_prefetched.end();)
			{
				if (now - it->second.time > PrefetchLifetime)
				{
					s_prefetchedBytes -= it->second.content->size();
					it = s_prefetched.erase(it);
				}
				else
				{
					++it;
				}
			}

			// Opened by someone else while this read was running: that open already read it itself.
			if (s_openedFiles.contains(filename) || s_prefetched.contains(filename))
			{
				return;
			}

			s_prefetchedBytes += content->size();
			s_prefetched.emplace(filename, PrefetchedFile{ std::move(content), now });
		}

		/// A file whose references are still to be prefetched.
		struct PendingScan
		{
			String name;
			std::shared_ptr<const String> content;
		};

		/// Prefetches every not yet opened asset the content references and queues those that can
		/// reference further assets.
		void PrefetchReferencesOf(const String& content, std::set<String, StrCaseIComp>& visited, std::vector<PendingScan>& pending, std::mutex& fileLock)
		{
			// Locked per lookup, not for the whole scan: a scan of a large mesh or terrain page takes
			// milliseconds, and the main thread must not wait for it to open a file of its own.
			std::vector<String> references;
			ScanAssetReferences(content, [&fileLock](const String& candidate)
			{
				std::unique_lock lock{ fileLock };
				return s_files.contains(candidate);
			}, references);

			for (String& reference : references)
			{
				if (!visited.insert(reference).second || !ShouldPrefetch(reference))
				{
					continue;
				}

				std::shared_ptr<const String> referenced = ReadContent(reference, fileLock);
				if (!referenced)
				{
					continue;
				}

				StorePrefetched(reference, referenced);
				if (CanReferenceAssets(reference))
				{
					pending.push_back({ std::move(reference), std::move(referenced) });
				}
			}
		}

		/// Prefetches what the given file content references, and recursively what those reference.
		void PrefetchReferencedBy(const String& filename, const String& content, std::mutex& fileLock)
		{
			std::set<String, StrCaseIComp> visited{ filename };
			std::vector<PendingScan> pending;

			PrefetchReferencesOf(content, visited, pending, fileLock);
			while (!pending.empty())
			{
				const PendingScan scan = std::move(pending.back());
				pending.pop_back();

				PrefetchReferencesOf(*scan.content, visited, pending, fileLock);
			}
		}
	}


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
		ClearPrefetched();
		{
			std::unique_lock lock{ s_prefetchLock };
			s_openedFiles.clear();
		}

		// Clear file list
		s_files.clear();

		// Remove all archives
		s_archives.clear();

		// Reset filesystem archive
		s_filesystemArchive.reset();
	}

	std::unique_ptr<std::istream> AssetRegistry::OpenFile(std::string filename)
	{
		filename = NormalizeFileName(std::move(filename));

		{
			std::unique_lock lock{ s_prefetchLock };
			s_openedFiles.insert(filename);
			++s_prefetchStats.opens;

			if (const auto it = s_prefetched.find(filename); it != s_prefetched.end())
			{
				++s_prefetchStats.hits;
				std::shared_ptr<const String> content = std::move(it->second.content);
				s_prefetchedBytes -= content->size();
				s_prefetched.erase(it);
				return std::make_unique<SharedMemoryStream>(std::move(content));
			}
		}

		if (auto file = OpenRegisteredFile(filename, s_fileLock))
		{
			return file;
		}

		{
			std::unique_lock lock{ s_fileLock };
			if (s_files.contains(filename))
			{
				// Registered, but the archive could not open it.
				return nullptr;
			}
		}

		// Not part of the registry: try it as a plain path
		auto filePtr = std::make_unique<std::ifstream>(filename, std::ios::binary | std::ios::in);
		if (filePtr && *filePtr)
		{
			return filePtr;
		}

		// Bad file
		return nullptr;
	}

	void AssetRegistry::Prefetch(const std::string& filename)
	{
		const String name = NormalizeFileName(filename);
		if (!ShouldPrefetch(name))
		{
			return;
		}

		std::shared_ptr<const String> content = ReadContent(name, s_fileLock);
		if (!content)
		{
			return;
		}

		StorePrefetched(name, content);
		if (CanReferenceAssets(name))
		{
			PrefetchReferencedBy(name, *content, s_fileLock);
		}
	}

	void AssetRegistry::PrefetchReferences(const std::string& filename)
	{
		const String name = NormalizeFileName(filename);
		if (!CanReferenceAssets(name))
		{
			return;
		}

		// The caller has just read the file, so this read is served by the operating system's cache.
		if (const std::shared_ptr<const String> content = ReadContent(name, s_fileLock))
		{
			PrefetchReferencedBy(name, *content, s_fileLock);
		}
	}

	void AssetRegistry::ClearPrefetched()
	{
		std::unique_lock lock{ s_prefetchLock };
		s_prefetched.clear();
		s_prefetchedBytes = 0;
		s_prefetchStats = {};
	}

	AssetRegistry::PrefetchStats AssetRegistry::GetPrefetchStats()
	{
		std::unique_lock lock{ s_prefetchLock };
		return s_prefetchStats;
	}

	std::optional<std::filesystem::file_time_type> AssetRegistry::GetLastWriteTime(const std::string& filename)
	{
		std::error_code error;
		const auto time = std::filesystem::last_write_time(s_basePath / filename, error);
		if (error)
		{
			return std::nullopt;
		}

		return time;
	}

	bool AssetRegistry::HasFile(const std::string& filename)
	{
		std::unique_lock lock { s_fileLock };

		// Try to find the requested file
		const auto it = s_files.find(filename);
		return it != s_files.end();
	}

	bool AssetRegistry::RemoveFile(const std::string& filename)
	{
		std::unique_lock lock{ s_fileLock };

		// Try to find the requested file
		const auto it = s_files.find(filename);
		if (it == s_files.end())
		{
			return false;
		}

		if (it->second->RemoveFile(filename))
		{
			// Remove from the file registry
			s_files.erase(it);
			return true;
		}

		return false;
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

	std::vector<std::string> AssetRegistry::ListFiles(const std::string& prefix, const std::string& extension)
	{
		std::unique_lock lock{ s_fileLock };
		std::vector<std::string> result;

		for (const auto& file : s_files)
		{
			if (file.first.starts_with(prefix) && (extension.empty() || file.first.ends_with(extension)))
			{
				result.push_back(file.first);
			}
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
