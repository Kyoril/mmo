#include "world_frame.h"

#include "base/macros.h"

#include <memory>

namespace mmo
{
	static std::shared_ptr<WorldFrame> s_currentWorldFrame;
	
	WorldFrame::WorldFrame(const std::string& name)
		: Frame("World", name)
	{
		ASSERT(!s_currentWorldFrame && "There can't be more than one world frame!");
		s_currentWorldFrame = std::static_pointer_cast<WorldFrame>(shared_from_this());
	}
}
