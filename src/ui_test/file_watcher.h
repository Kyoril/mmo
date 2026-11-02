// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mmo
{
	/// @brief Polls folders and single files for changes of files with given extensions.
	///
	/// Polling instead of OS notifications keeps the watcher portable and immune to editors that
	/// save by renaming temp files. A scan of the whole Interface folder takes a few milliseconds.
	class FileWatcher final : public NonCopyable
	{
	public:
		/// @brief Watches a folder recursively.
		void AddFolder(const std::filesystem::path& folder);

		/// @brief Watches a single file, which may not exist yet.
		void AddFile(const std::filesystem::path& file);

		/// @brief Sets the extensions (lower case, with dot) that count in watched folders.
		void SetExtensions(std::set<std::string> extensions) { m_extensions = std::move(extensions); }

		/// @brief Removes every watched folder and file.
		void Clear();

		/// @brief Takes the current state as the baseline without reporting changes.
		void Snapshot();

		/// @brief Scans again and returns the files that were changed, added or removed since the last scan.
		std::vector<std::filesystem::path> Poll();

		/// @brief Whether the last Poll() saw files being added or removed (not just modified).
		bool FileSetChanged() const { return m_fileSetChanged; }

	private:
		struct FileState
		{
			std::filesystem::file_time_type time;
			uintmax_t size = 0;

			bool operator==(const FileState& other) const { return time == other.time && size == other.size; }
		};

		std::map<std::filesystem::path, FileState> Scan() const;

	private:
		std::vector<std::filesystem::path> m_folders;
		std::vector<std::filesystem::path> m_files;
		std::set<std::string> m_extensions;
		std::map<std::filesystem::path, FileState> m_state;
		bool m_fileSetChanged = false;
	};
}
