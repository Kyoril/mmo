// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <deque>
#include <mutex>
#include <string>

namespace mmo
{
	/// Keeps the most recent client log lines in memory, for bug reports.
	///
	/// Client.log is write-buffered while the client runs, so reading the file would miss exactly
	/// the lines closest to the report; this buffer is fed straight from the log signal instead.
	class ClientLogTail final
	{
	public:
		explicit ClientLogTail(const size_t maxLines = 200)
			: m_maxLines(maxLines)
		{
		}

		/// Appends a line, dropping the oldest when full.
		void Add(std::string line)
		{
			std::scoped_lock lock{ m_mutex };
			if (m_maxLines == 0)
			{
				return;
			}

			while (m_lines.size() >= m_maxLines)
			{
				m_lines.pop_front();
			}
			m_lines.push_back(std::move(line));
		}

		/// All buffered lines, oldest first, separated by newlines.
		[[nodiscard]] std::string GetText() const
		{
			std::scoped_lock lock{ m_mutex };
			std::string text;
			for (const auto& line : m_lines)
			{
				text += line;
				text += '\n';
			}
			return text;
		}

	private:
		size_t m_maxLines;
		mutable std::mutex m_mutex;
		std::deque<std::string> m_lines;
	};
}
