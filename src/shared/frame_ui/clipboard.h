// Copyright (C) 2019 - 2026, Kyoril. All rights reserved.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>


namespace mmo
{
	class IClipboard
	{
	public:
		virtual ~IClipboard() = default;

		virtual bool SetText(std::string_view text) = 0;
		virtual std::optional<std::string> GetText() = 0;
	};

	/// Platform-agnostic clipboard facade used by frame UI controls.
	class Clipboard final
	{
	public:
		static bool SetText(std::string_view text);
		static std::optional<std::string> GetText();

	private:
		static IClipboard& GetImplementation();
	};

	/// Creates a platform-specific clipboard implementation.
	std::unique_ptr<IClipboard> CreateClipboardImplementation();
}

