// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "file_watcher.h"

#include <algorithm>
#include <cctype>

namespace mmo
{
	void FileWatcher::AddFolder(const std::filesystem::path& folder)
	{
		m_folders.push_back(folder);
	}

	void FileWatcher::AddFile(const std::filesystem::path& file)
	{
		m_files.push_back(file);
	}

	void FileWatcher::Clear()
	{
		m_folders.clear();
		m_files.clear();
		m_state.clear();
	}

	void FileWatcher::Snapshot()
	{
		m_state = Scan();
		m_fileSetChanged = false;
	}

	std::vector<std::filesystem::path> FileWatcher::Poll()
	{
		auto current = Scan();

		std::vector<std::filesystem::path> changed;
		m_fileSetChanged = false;

		for (const auto& [path, state] : current)
		{
			const auto it = m_state.find(path);
			if (it == m_state.end())
			{
				changed.push_back(path);
				m_fileSetChanged = true;
			}
			else if (!(it->second == state))
			{
				changed.push_back(path);
			}
		}

		for (const auto& [path, state] : m_state)
		{
			if (!current.contains(path))
			{
				changed.push_back(path);
				m_fileSetChanged = true;
			}
		}

		m_state = std::move(current);
		return changed;
	}

	std::map<std::filesystem::path, FileWatcher::FileState> FileWatcher::Scan() const
	{
		std::map<std::filesystem::path, FileState> result;
		std::error_code ec;

		const auto addFile = [&result, &ec](const std::filesystem::path& path)
		{
			FileState state;
			state.time = std::filesystem::last_write_time(path, ec);
			if (ec)
			{
				return;
			}
			state.size = std::filesystem::file_size(path, ec);
			if (ec)
			{
				return;
			}
			result[path] = state;
		};

		for (const auto& folder : m_folders)
		{
			for (auto it = std::filesystem::recursive_directory_iterator(folder, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
			{
				if (!it->is_regular_file(ec))
				{
					continue;
				}

				std::string extension = it->path().extension().string();
				std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (m_extensions.contains(extension))
				{
					addFile(it->path());
				}
			}
			ec.clear();
		}

		for (const auto& file : m_files)
		{
			if (std::filesystem::exists(file, ec))
			{
				addFile(file);
			}
			ec.clear();
		}

		return result;
	}
}
