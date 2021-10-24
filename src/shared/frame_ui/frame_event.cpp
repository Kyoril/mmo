// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "frame_event.h"
#include "frame_mgr.h"

#include <utility>


namespace mmo
{
	void FrameEvent::operator()() const
	{
		if (!m_luaCode.empty())
		{
			FrameManager::Get().ExecuteLua(m_luaCode);
		}
	}

	void FrameEvent::Set(std::string code)
	{
		m_luaCode = std::move(code);
	}
}
