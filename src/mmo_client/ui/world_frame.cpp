// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "world_frame.h"

#include "base/macros.h"

#include <memory>

namespace mmo
{
	static std::weak_ptr<WorldFrame> s_currentWorldFrame;
	
	WorldFrame::WorldFrame(const std::string& name)
		: Frame("World", name)
	{
	}

	void WorldFrame::RenderWorld()
	{

	}

	void WorldFrame::SetAsCurrentWorldFrame()
	{
		ASSERT(!s_currentWorldFrame.lock() && "There can't be more than one world frame!");
		s_currentWorldFrame = std::static_pointer_cast<WorldFrame>(shared_from_this());
	}
}
