// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>
#include <vector>

namespace mmo
{
	/// A plain-text article. Body supports paragraphs, ## headings and - bullets.
	struct LauncherArticle
	{
		std::string title;
		std::string date;
		std::string summary;
		std::string body;
		std::string image;
	};

	/// Loads bounded UTF-8 content files; invalid files retain the embedded fallback.
	class LauncherContent
	{
	  public:
		/// Loads optional launcher_content/news.txt and patches.txt in the game directory.
		void Load(const std::string& directory);
		/// Parses articles separated by a line containing ---. First three lines are metadata.
		static std::vector<LauncherArticle> Parse(const std::string& text);
		/// Applies remotely published lists, retaining fallback lists when omitted.
		void Apply(const std::vector<LauncherArticle>& news, const std::vector<LauncherArticle>& patches);
		/// Current news, newest first.
		const std::vector<LauncherArticle>& GetNews() const
		{
			return m_news;
		}
		/// Current patch notes, newest first.
		const std::vector<LauncherArticle>& GetPatches() const
		{
			return m_patches;
		}

	  private:
		std::vector<LauncherArticle> m_news;
		std::vector<LauncherArticle> m_patches;
	};
} // namespace mmo
