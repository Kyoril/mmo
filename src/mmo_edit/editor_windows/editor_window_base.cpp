// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "editor_window_base.h"

namespace mmo
{
	EditorWindowBase::EditorWindowBase(const String& name)
		: m_name(name)
	{	
	}

	void EditorWindowBase::SetVisible(const bool value)
	{
		if (m_visible != value)
		{
			m_visible = value;
			visibilityChanged(*this, m_visible);
		}
	}

	void EditorWindowBase::Close()
	{
		SetVisible(false);
	}

	void EditorWindowBase::Open()
	{
		SetVisible(true);
	}
}
