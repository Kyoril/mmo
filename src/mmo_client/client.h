#pragma once

#include <functional>

namespace mmo
{
	void DispatchOnGameThread(std::function<void()>&& f);
}
