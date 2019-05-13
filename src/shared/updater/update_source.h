#pragma once

#include "update_source_file.h"


namespace mmo
{
	namespace updating
	{
		struct IUpdateSource
		{
			virtual ~IUpdateSource();
			virtual UpdateSourceFile readFile(
			    const std::string &path
			) = 0;
		};

	}
}
