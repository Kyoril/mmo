#pragma once

#include "base/filesystem.h"

#include <functional>

namespace mmo
{
	namespace updating
	{
		struct PreparedUpdate;
		struct UpdateParameters;


		struct ApplicationUpdate
		{
			std::function<void (const UpdateParameters &, const char *const *, size_t)> perform;
		};


		ApplicationUpdate updateApplication(
		    const std::filesystem::path &applicationPath,
		    const PreparedUpdate &preparedUpdate
		);
	}
}
