// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "shader_build_info.h"

namespace mmo::material_compiler_version
{
	uint32 GetBackendVersion(const std::string_view platform)
	{
		if (platform == "D3D11" || platform == "METAL")
		{
			return 1;
		}

		return 0;
	}
}
