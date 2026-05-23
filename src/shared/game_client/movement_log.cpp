// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "movement_log.h"

#include "base/clock.h"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

namespace mmo
{
	MovementLog& MovementLog::Get()
	{
		static MovementLog s_instance;
		return s_instance;
	}

	bool MovementLog::Open(const std::string& path)
	{
		std::scoped_lock lock{ m_mutex };

		if (m_file.is_open())
		{
			m_file.close();
		}

		// Ensure the parent directory exists
		const std::filesystem::path fsPath(path);
		if (fsPath.has_parent_path())
		{
			std::error_code ec;
			std::filesystem::create_directories(fsPath.parent_path(), ec);
			// Ignore ec — if creation fails, the open below will fail too and we'll return false.
		}

		m_file.open(path, std::ios::out | std::ios::trunc);
		if (!m_file.is_open())
		{
			return false;
		}

		// Write a header so the reader knows what client session produced this.
		m_file << "# MMO Client Movement Log\n";
		m_file << "# Columns: timestamp_ms | category | message\n";
		m_file << "# -----------------------------------------------\n";
		m_file.flush();
		return true;
	}

	void MovementLog::Close()
	{
		std::scoped_lock lock{ m_mutex };

		if (m_file.is_open())
		{
			m_file << "# Log closed\n";
			m_file.flush();
			m_file.close();
		}
	}

	bool MovementLog::IsOpen() const
	{
		std::scoped_lock lock{ m_mutex };
		return m_file.is_open();
	}

	void MovementLog::Write(const char* category, const std::string& message)
	{
		std::scoped_lock lock{ m_mutex };

		if (!m_file.is_open())
		{
			return;
		}

		const GameTime ts = GetAsyncTimeMs();
		m_file << ts << " | " << category << " | " << message << '\n';
		m_file.flush();
	}

} // namespace mmo
