#pragma once

#include <memory>

namespace mmo
{
	namespace updating
	{
		struct IUpdateSource;
		struct UpdateURL;


		std::unique_ptr<IUpdateSource> openSourceFromUrl(const UpdateURL &url);
	}
}
