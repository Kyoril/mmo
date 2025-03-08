// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "avatar_definition_mgr.h"

#include <filesystem>

#include "assets/asset_registry.h"
#include "binary_io/stream_source.h"
#include "log/default_log_levels.h"

namespace mmo
{

	static std::unique_ptr<AvatarDefinitionManager> s_instance;

	AvatarDefinitionManager& AvatarDefinitionManager::Get()
	{
		if (!s_instance)
		{
			s_instance = std::make_unique<AvatarDefinitionManager>();
		}

		return *s_instance;
	}

	std::shared_ptr<CustomizableAvatarDefinition> AvatarDefinitionManager::Load(std::string_view filename)
	{
		const auto it = m_definitions.find(String(filename));
		if (it != m_definitions.end())
		{
			return it->second;
		}

		const auto file = AssetRegistry::OpenFile(String(filename));
		if (!file)
		{
			ELOG("Failed to load material file " << filename << ": File not found!");
			return nullptr;
		}

		std::filesystem::path p = filename;

		auto definition = std::make_shared<CustomizableAvatarDefinition>();

		io::StreamSource source{ *file };
		io::Reader reader{ source };
		if (!definition->Read(reader))
		{
			ELOG("Failed to load avatar definition!");
			return nullptr;
		}

		m_definitions.emplace(filename, definition);
		return definition;
	}

	void AvatarDefinitionManager::Remove(const std::string_view filename)
	{
		m_definitions.erase(String(filename));
	}

	void AvatarDefinitionManager::RemoveAllUnreferenced()
	{
		std::erase_if(m_definitions, [](const std::pair<std::string, std::shared_ptr<CustomizableAvatarDefinition>>& pair)
			{
				return pair.second.use_count() <= 1;
			});
	}
}
