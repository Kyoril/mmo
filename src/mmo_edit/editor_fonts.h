// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

struct ImFont;

namespace mmo
{
	/// @brief Registers the editor's fonts so any panel can pull them without depending on MainWindow.
	///	Called once during ImGui initialization.
	void SetEditorFonts(ImFont* body, ImFont* header, ImFont* title);

	/// @brief Gets the default body font (Roboto 16 + FontAwesome). May be null before init.
	[[nodiscard]] ImFont* GetEditorBodyFont();

	/// @brief Gets the header font (Roboto 18 + FontAwesome), used for section titles. May be null.
	[[nodiscard]] ImFont* GetEditorHeaderFont();

	/// @brief Gets the large title font (Roboto 26 + FontAwesome), used for hero/welcome text. May be null.
	[[nodiscard]] ImFont* GetEditorTitleFont();
}
