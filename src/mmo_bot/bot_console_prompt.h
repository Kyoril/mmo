// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace mmo
{
	enum class BotConsolePromptResultKind
	{
		Selected,
		Aborted,
		NonInteractive,
	};

	struct BotConsolePromptResult
	{
		BotConsolePromptResultKind kind { BotConsolePromptResultKind::Aborted };
		std::optional<size_t> selectedIndex;
	};

	bool IsConsoleInputInteractive();

	BotConsolePromptResult PromptForSelection(
		std::string_view selectionName,
		const std::vector<std::string>& options,
		std::istream& input,
		std::ostream& output,
		bool isInteractive);
}
