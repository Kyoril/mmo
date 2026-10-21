// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/filesystem.h"

#include <memory>
#include <istream>
#include <mutex>
#include <optional>
#include <ostream>
#include <vector>


namespace mmo
{
	/// This class manages assets for the game. It allows loading of files regardless
	/// of their source. For example, a file might be an actual file or a virtual file
	/// inside of an hpak archive.
	class AssetRegistry final
		: public NonCopyable
	{
	private:
		/// Private default constructor.
		AssetRegistry() = default;

	public:
		/// Initializes the asset registry.
		static void Initialize(const std::filesystem::path& basePath, const std::vector<std::string>& archives);

		static void AddArchivePackage(const std::filesystem::path& path);

		/// Destroys the asset registry.
		static void Destroy();

		/// Opens a file for reading. Returns nullptr if the files doesn't exist.
		/// @remark A file prefetched earlier is served from memory, and its prefetched copy is released.
		static std::unique_ptr<std::istream> OpenFile(std::string filename);

		/// @brief Reads a file and every asset it references, recursively, into memory ahead of use.
		/// @details Meant for streaming threads: the first open of an asset on disk costs milliseconds of
		///	file system latency, which the main thread would otherwise pay inside its frame when the asset is
		///	first loaded. Files opened before are skipped (their resource managers already hold them), and so is
		///	everything once the prefetch memory limit is reached. Prefetched content not opened within a few
		///	minutes is dropped.
		/// @param filename Name of the asset to prefetch.
		static void Prefetch(const std::string& filename);

		/// @brief Like Prefetch, but for a file the caller has just read itself: only what it references is
		///	read into memory.
		static void PrefetchReferences(const std::string& filename);

		/// @brief Drops all prefetched content.
		static void ClearPrefetched();

		/// @brief Counts of OpenFile calls since the last ClearPrefetched.
		struct PrefetchStats
		{
			/// Opens served from prefetched content.
			size_t hits = 0;
			/// All opens.
			size_t opens = 0;
		};

		/// @brief Gets how many opens prefetching served since the last ClearPrefetched.
		static PrefetchStats GetPrefetchStats();

		/// Determines whether a given file name is already taken.
		static bool HasFile(const std::string& filename);

		/// @brief Gets the modification time of a loose file below the asset root.
		/// @return The time, or nothing if the file is not a loose file (missing, or packed in an archive).
		static std::optional<std::filesystem::file_time_type> GetLastWriteTime(const std::string& filename);

		static bool RemoveFile(const std::string& filename);

		/// Creates a new file for writing. Returns nullptr if the file couldn't be created.
		static std::unique_ptr<std::ostream> CreateNewFile(const std::string& filename);

		/// Returns a list of file names in the asset registry.
		static std::vector<std::string> ListFiles();

		/// Returns a list of file names in the asset registry.
		static std::vector<std::string> ListFiles(const std::string& prefix, const std::string& extension);

		/// Returns a list of file names in the asset registry.
		static std::vector<std::string> ListFiles(const std::string& extension);

	private:
		static std::mutex s_fileLock;
	};
}