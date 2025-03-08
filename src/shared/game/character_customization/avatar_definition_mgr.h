// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <memory>

#include "customizable_avatar_definition.h"
#include "base/non_copyable.h"
#include "base/utilities.h"

namespace mmo
{
	/// @brief Class which manages all materials.
	class AvatarDefinitionManager final : public NonCopyable
	{
		friend std::unique_ptr<AvatarDefinitionManager> std::make_unique<AvatarDefinitionManager>();

	private:
		AvatarDefinitionManager() = default;

	public:
		~AvatarDefinitionManager() override = default;

	public:
		static AvatarDefinitionManager& Get();

	public:
		/// Loads a material from file or retrieves it from the cache.
		std::shared_ptr<CustomizableAvatarDefinition> Load(std::string_view filename);

		void Remove(std::string_view filename);

		void RemoveAllUnreferenced();

	private:
		std::map<std::string, std::shared_ptr<CustomizableAvatarDefinition>, StrCaseIComp> m_definitions;
	};
}
