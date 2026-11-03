// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "cook_report.h"

#include <algorithm>

#include "log/default_log_levels.h"

namespace mmo
{
	namespace
	{
		/// GitHub reads workflow commands line by line; a newline in a message would end it early.
		String EscapeAnnotation(const String& text)
		{
			String escaped;
			for (const char c : text)
			{
				switch (c)
				{
				case '%':
					escaped += "%25";
					break;
				case '\r':
					escaped += "%0D";
					break;
				case '\n':
					escaped += "%0A";
					break;
				default:
					escaped += c;
					break;
				}
			}
			return escaped;
		}
	}

	void CookReport::Warn(String asset, String message)
	{
		WLOG(asset << ": " << message);
		m_warnings.push_back({ std::move(asset), std::move(message) });
	}

	void CookReport::Merge(const std::vector<CookWarning>& warnings)
	{
		m_warnings.insert(m_warnings.end(), warnings.begin(), warnings.end());
	}

	std::vector<CookWarning> CookReport::GetSortedWarnings() const
	{
		std::vector<CookWarning> sorted = m_warnings;
		std::sort(sorted.begin(), sorted.end());
		sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
		return sorted;
	}

	void CookReport::WriteGithubAnnotations(std::ostream& out) const
	{
		for (const CookWarning& warning : GetSortedWarnings())
		{
			out << "::warning title=cook::" << EscapeAnnotation(warning.asset) << ": " << EscapeAnnotation(warning.message) << "\n";
		}
	}
}
