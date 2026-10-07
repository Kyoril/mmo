// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "launcher_content.h"
#include "version.h"
#include <fstream>
#include <sstream>

namespace mmo
{
	namespace
	{
		constexpr size_t MaxContentBytes = 256 * 1024;

		void loadFile(const std::string& path, std::vector<LauncherArticle>& articles)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file || file.tellg() <= 0 || file.tellg() > static_cast<std::streamoff>(MaxContentBytes))
			{
				return;
			}
			std::string text(static_cast<size_t>(file.tellg()), '\0');
			file.seekg(0);
			if (!file.read(text.data(), static_cast<std::streamsize>(text.size())))
			{
				return;
			}
			auto parsed = LauncherContent::Parse(text);
			if (!parsed.empty())
			{
				articles = std::move(parsed);
			}
		}
	} // namespace

	std::vector<LauncherArticle> LauncherContent::Parse(const std::string& text)
	{
		std::vector<LauncherArticle> result;
		if (text.size() > MaxContentBytes || text.find('\0') != std::string::npos)
		{
			return result;
		}
		std::istringstream stream(text);
		LauncherArticle article;
		size_t lineIndex = 0;
		const auto append = [&]()
		{
			if (!article.title.empty() && !article.summary.empty() && !article.body.empty() && result.size() < 32)
			{
				result.push_back(article);
			}
			article = {};
			lineIndex = 0;
		};
		std::string line;
		while (std::getline(stream, line))
		{
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			if (lineIndex == 0 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
			{
				line.erase(0, 3);
			}
			if (line == "---")
			{
				append();
				continue;
			}
			switch (lineIndex++)
			{
			case 0:
				article.title = line.substr(0, 180);
				break;
			case 1:
				article.date = line.substr(0, 80);
				break;
			case 2:
				article.summary = line.substr(0, 400);
				break;
			default:
				article.body += line + '\n';
				break;
			}
		}
		append();
		return result;
	}

	void LauncherContent::Apply(const std::vector<LauncherArticle>& news, const std::vector<LauncherArticle>& patches)
	{
		if (!news.empty())
		{
			m_news = news;
		}
		if (!patches.empty())
		{
			m_patches = patches;
		}
	}

	void LauncherContent::Load(const std::string& directory)
	{
		m_news = Parse("From the development team\nAlestia Online\nA new home for your adventure.\n"
					   "Welcome to the Alestia Online launcher. Browse the latest news and read update notes while your game downloads.\n\n"
					   "## Stay informed\nNews and release notes appear here as they are published, independently of game updates.\n\n"
					   "## Built for your adventure\nYour download continues when you change pages. Once the update is complete, select "
					   "PLAY to enter Alestia.\n"
					   "---\nExplore Alestia\nDiscover the world\nYour next adventure awaits.\n"
					   "Discover a world of towns, forests and winding paths.\n\n"
					   "## A world to explore\nThe launcher artwork offers an artistic glimpse of Alestia. It is promotional art, rather "
					   "than an in-game screenshot.\n\n"
					   "## Prepare for your journey\nKeep your game up to date before starting your next adventure. You can review "
					   "published patch notes from the Patch Notes tab.\n"
					   "---\nRead while you update\nLauncher guide\nNews and patch notes are always a click away.\n"
					   "## Make the most of update time\nBrowse the News page or select a release in Patch Notes. Your update continues in "
					   "the background.\n\n"
					   "## Need to check your installation?\nAfter the updater finishes, open Settings and select Check and repair files. "
					   "This verifies installed files against the update manifest.\n\n"
					   "## Keyboard controls\nUse Tab and Shift+Tab to move between controls, Enter to activate, and the arrow or Page Up "
					   "/ Page Down keys to scroll an article.\n");
		m_patches = Parse("Launcher " MMO_LAUNCHER_VERSION_STR "\nCurrent build\nA clearer view of news and updates.\n"
						  "## Highlights\n- A roomier launcher with dedicated Home, News and Patch Notes pages.\n"
						  "- Refreshed promotional artwork for Alestia Online.\n\n"
						  "## Launcher improvements\n- Read articles without interrupting downloads.\n"
						  "- Browse release history and scroll through longer notes.\n"
						  "- Use keyboard navigation to move between pages and controls.\n\n"
						  "## Game release notes\nNo game release notes have been published with this build yet. This entry describes the "
						  "launcher only.\n");
		loadFile(directory + "/launcher_content/news.txt", m_news);
		loadFile(directory + "/launcher_content/patches.txt", m_patches);
	}
} // namespace mmo
