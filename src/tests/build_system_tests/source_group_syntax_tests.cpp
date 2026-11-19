// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "catch.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// source_group() takes its file list after a keyword (FILES, and TREE/PREFIX for the tree form).
// A list handed over without one is an error in current CMake versions, but older ones accepted
// it silently, so such a call survives on the developer machines and only takes the build down
// when a CI image brings a newer CMake along. Nothing builds after that, which is why this lives
// in a test rather than in a review checklist.

namespace
{
	const std::filesystem::path g_sourceDir = MMO_CMAKE_SOURCE_DIR;

	/// The CMake files of the project itself. deps/ holds third party code which is not ours to
	/// fix, and the data submodules carry no build files at all.
	std::vector<std::filesystem::path> CollectCMakeFiles()
	{
		std::vector<std::filesystem::path> result;

		const std::filesystem::path root = g_sourceDir / "CMakeLists.txt";
		if (std::filesystem::exists(root))
		{
			result.push_back(root);
		}

		for (const std::filesystem::path& directory : { g_sourceDir / "src", g_sourceDir / "cmake" })
		{
			if (!std::filesystem::exists(directory))
			{
				continue;
			}

			for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(directory))
			{
				if (!entry.is_regular_file())
				{
					continue;
				}

				const std::filesystem::path& path = entry.path();
				if (path.filename() == "CMakeLists.txt" || path.extension() == ".cmake")
				{
					result.push_back(path);
				}
			}
		}

		return result;
	}

	std::string ReadFile(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		std::ostringstream buffer;
		buffer << file.rdbuf();
		return buffer.str();
	}

	/// Drops `#` comments, keeping the line structure intact. Quoted arguments may span lines and
	/// may contain a `#`, so the quote state is carried across the whole file.
	std::string StripComments(const std::string& text)
	{
		std::string result;
		result.reserve(text.size());

		bool inQuote = false;
		for (size_t i = 0; i < text.size(); ++i)
		{
			const char character = text[i];
			if (inQuote)
			{
				if (character == '\\' && i + 1 < text.size())
				{
					result.push_back(character);
					result.push_back(text[++i]);
					continue;
				}

				if (character == '"')
				{
					inQuote = false;
				}

				result.push_back(character);
				continue;
			}

			if (character == '"')
			{
				inQuote = true;
				result.push_back(character);
				continue;
			}

			if (character == '#')
			{
				while (i < text.size() && text[i] != '\n')
				{
					++i;
				}

				result.push_back('\n');
				continue;
			}

			result.push_back(character);
		}

		return result;
	}

	size_t LineOf(const std::string& text, size_t offset)
	{
		return static_cast<size_t>(std::count(text.begin(), text.begin() + offset, '\n')) + 1;
	}

	struct SourceGroupCall
	{
		size_t line;
		std::string arguments;
	};

	/// Every source_group() invocation of the given file content, with the raw text between its
	/// parentheses.
	std::vector<SourceGroupCall> FindSourceGroupCalls(const std::string& text)
	{
		std::vector<SourceGroupCall> calls;

		std::string lowered = text;
		std::transform(lowered.begin(), lowered.end(), lowered.begin(),
			[](unsigned char character) { return static_cast<char>(std::tolower(character)); });

		const std::string command = "source_group";
		size_t position = 0;
		while ((position = lowered.find(command, position)) != std::string::npos)
		{
			const size_t afterName = position + command.size();
			const bool startsWord = position == 0 ||
				!(std::isalnum(static_cast<unsigned char>(text[position - 1])) || text[position - 1] == '_');

			size_t open = afterName;
			while (open < text.size() && std::isspace(static_cast<unsigned char>(text[open])))
			{
				++open;
			}

			if (!startsWord || open >= text.size() || text[open] != '(')
			{
				position = afterName;
				continue;
			}

			size_t index = open + 1;
			int depth = 1;
			bool inQuote = false;
			for (; index < text.size() && depth > 0; ++index)
			{
				const char character = text[index];
				if (inQuote)
				{
					if (character == '\\' && index + 1 < text.size())
					{
						++index;
					}
					else if (character == '"')
					{
						inQuote = false;
					}

					continue;
				}

				if (character == '"')
				{
					inQuote = true;
				}
				else if (character == '(')
				{
					++depth;
				}
				else if (character == ')')
				{
					--depth;
				}
			}

			calls.push_back({ LineOf(text, position), text.substr(open + 1, (index - 1) - (open + 1)) });
			position = index;
		}

		return calls;
	}

	std::vector<std::string> Tokenize(const std::string& arguments)
	{
		std::vector<std::string> tokens;
		std::string current;
		bool inQuote = false;
		bool quoted = false;

		for (size_t i = 0; i < arguments.size(); ++i)
		{
			const char character = arguments[i];
			if (inQuote)
			{
				if (character == '\\' && i + 1 < arguments.size())
				{
					current.push_back(arguments[++i]);
				}
				else if (character == '"')
				{
					inQuote = false;
				}
				else
				{
					current.push_back(character);
				}

				continue;
			}

			if (character == '"')
			{
				inQuote = true;
				quoted = true;
				continue;
			}

			if (std::isspace(static_cast<unsigned char>(character)))
			{
				if (quoted || !current.empty())
				{
					tokens.push_back(current);
					current.clear();
					quoted = false;
				}

				continue;
			}

			current.push_back(character);
		}

		if (quoted || !current.empty())
		{
			tokens.push_back(current);
		}

		return tokens;
	}

	bool IsKeyword(const std::string& token)
	{
		return token == "FILES" || token == "REGULAR_EXPRESSION" || token == "PREFIX" || token == "TREE";
	}

	/// Describes what is wrong with the arguments of one call, or an empty string when they are
	/// well formed. Mirrors how CMake itself reads them: everything after the group name belongs
	/// to a keyword, the one exception being the old two argument form `source_group(name regex)`.
	std::string Problem(const std::vector<std::string>& tokens)
	{
		if (tokens.empty())
		{
			return "source_group without arguments";
		}

		size_t index = 1;
		std::string keyword;
		if (tokens[0] == "TREE")
		{
			if (tokens.size() < 2)
			{
				return "TREE without a directory";
			}

			index = 2;
			keyword = "TREE";
		}

		for (; index < tokens.size(); ++index)
		{
			if (IsKeyword(tokens[index]))
			{
				keyword = tokens[index];
				continue;
			}

			if (!keyword.empty())
			{
				continue;
			}

			// The old form names a regular expression, never a variable: a variable here holds a
			// file list whose FILES keyword went missing, however few arguments the call has.
			if (tokens.size() == 2 && index == 1 && tokens[1].find("${") == std::string::npos)
			{
				continue;
			}

			return "argument '" + tokens[index] + "' is not introduced by FILES, REGULAR_EXPRESSION, PREFIX or TREE";
		}

		return "";
	}
}

TEST_CASE("every source_group call hands its file list over after a keyword", "[build_system]")
{
	const std::vector<std::filesystem::path> files = CollectCMakeFiles();
	REQUIRE_FALSE(files.empty());

	size_t seen = 0;
	std::string report;
	for (const std::filesystem::path& file : files)
	{
		const std::string text = StripComments(ReadFile(file));
		for (const SourceGroupCall& call : FindSourceGroupCalls(text))
		{
			++seen;

			const std::string problem = Problem(Tokenize(call.arguments));
			if (!problem.empty())
			{
				report += std::filesystem::relative(file, g_sourceDir).generic_string() + ":" +
					std::to_string(call.line) + ": " + problem + "\n";
			}
		}
	}

	// A scan that finds nothing would pass without ever looking at a call.
	REQUIRE(seen > 0);

	INFO(report);
	CHECK(report.empty());
}
