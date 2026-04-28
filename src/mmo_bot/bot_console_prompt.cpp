// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "bot_console_prompt.h"

#include <cctype>
#include <limits>
#include <sstream>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace mmo
{
	namespace
	{
		std::string TrimCopy(std::string_view value)
		{
			size_t start = 0;
			while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0)
			{
				++start;
			}

			size_t end = value.size();
			while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
			{
				--end;
			}

			return std::string(value.substr(start, end - start));
		}
	}

	bool IsConsoleInputInteractive()
	{
#ifdef _WIN32
		return _isatty(_fileno(stdin)) != 0;
#else
		return isatty(fileno(stdin)) != 0;
#endif
	}

	BotConsolePromptResult PromptForSelection(
		std::string_view selectionName,
		const std::vector<std::string>& options,
		std::istream& input,
		std::ostream& output,
		bool isInteractive)
	{
		if (!isInteractive)
		{
			output << "Selection required for " << selectionName << " but stdin is not interactive." << '\n';
			return { BotConsolePromptResultKind::NonInteractive, std::nullopt };
		}

		if (options.empty())
		{
			output << "No " << selectionName << " options are available." << '\n';
			return { BotConsolePromptResultKind::Aborted, std::nullopt };
		}

		output << "Select " << selectionName << ":" << '\n';
		for (size_t i = 0; i < options.size(); ++i)
		{
			output << "  " << (i + 1) << ") " << options[i] << '\n';
		}

		std::string line;
		while (true)
		{
			output << "Enter choice [1-" << options.size() << "]: ";
			output.flush();

			if (!std::getline(input, line))
			{
				output << "Selection aborted." << '\n';
				return { BotConsolePromptResultKind::Aborted, std::nullopt };
			}

			const std::string trimmed = TrimCopy(line);
			if (trimmed.empty())
			{
				output << "Invalid selection. Enter a number between 1 and " << options.size() << "." << '\n';
				continue;
			}

			std::istringstream parser(trimmed);
			size_t selected = 0;
			char trailing = '\0';
			if (!(parser >> selected) || (parser >> trailing) || selected == 0 || selected > options.size())
			{
				output << "Invalid selection. Enter a number between 1 and " << options.size() << "." << '\n';
				continue;
			}

			return { BotConsolePromptResultKind::Selected, selected - 1 };
		}
	}
}
