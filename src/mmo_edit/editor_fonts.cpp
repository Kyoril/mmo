// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "editor_fonts.h"

namespace mmo
{
	namespace
	{
		ImFont* s_bodyFont = nullptr;
		ImFont* s_headerFont = nullptr;
		ImFont* s_titleFont = nullptr;
	}

	void SetEditorFonts(ImFont* body, ImFont* header, ImFont* title)
	{
		s_bodyFont = body;
		s_headerFont = header;
		s_titleFont = title;
	}

	ImFont* GetEditorBodyFont()
	{
		return s_bodyFont;
	}

	ImFont* GetEditorHeaderFont()
	{
		return s_headerFont;
	}

	ImFont* GetEditorTitleFont()
	{
		return s_titleFont;
	}
}
