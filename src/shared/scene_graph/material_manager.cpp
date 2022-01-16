// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "material_manager.h"

#include <memory>

namespace mmo
{
	static std::unique_ptr<MaterialManager> s_instance;

	MaterialManager& MaterialManager::Get()
	{
		if (!s_instance)
		{
			s_instance = std::make_unique<MaterialManager>();
		}

		return *s_instance;
	}
}
