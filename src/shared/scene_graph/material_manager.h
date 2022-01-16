// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include <memory>

#include "base/non_copyable.h"

namespace mmo
{
	/// @brief Class which manages all materials.
	class MaterialManager final : public NonCopyable
	{
		friend std::unique_ptr<MaterialManager> std::make_unique<MaterialManager>();

	private:
		MaterialManager() = default;

	public:
		~MaterialManager() = default;

	public:
		static MaterialManager& Get();
	};
}
