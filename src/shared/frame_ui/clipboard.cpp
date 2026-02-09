// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#include "clipboard.h"


namespace mmo
{
	namespace
	{
		std::unique_ptr<IClipboard> s_clipboard;
	}

	IClipboard& Clipboard::GetImplementation()
	{
		if (!s_clipboard)
		{
			s_clipboard = CreateClipboardImplementation();
		}

		return *s_clipboard;
	}

	bool Clipboard::SetText(std::string_view text)
	{
		return GetImplementation().SetText(text);
	}

	std::optional<std::string> Clipboard::GetText()
	{
		return GetImplementation().GetText();
	}
}
