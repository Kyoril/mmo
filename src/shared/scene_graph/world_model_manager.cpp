// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_model_manager.h"

#include "world_model_serializer.h"
#include "assets/asset_registry.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "base/macros.h"
#include "log/default_log_levels.h"

namespace mmo
{
	WorldModelManager& WorldModelManager::Get()
	{
		static WorldModelManager instance;
		return instance;
	}

	WorldModelPtr WorldModelManager::Load(const std::string& filename)
	{
		// Try to find the world model first before trying to load it again
		const auto it = m_worldModels.find(filename);
		if (it != m_worldModels.end())
		{
			return it->second;
		}

		// Try to load the file from the registry
		const auto filePtr = AssetRegistry::OpenFile(filename);
		if (!filePtr || !*filePtr)
		{
			ELOG("Unable to load world model file " << filename);
			return nullptr;
		}

		// Create readers
		io::StreamSource source{ *filePtr };
		io::Reader reader{ source };

		// Create the resulting world model
		auto worldModel = std::make_shared<WorldModel>();

		WorldModelDeserializer deserializer{ *worldModel };
		if (!deserializer.Read(reader))
		{
			ELOG("Failed to load world model '" << filename << "'");
			return nullptr;
		}

		// Store the world model in the cache
		m_worldModels[filename] = worldModel;

		// And return the world model pointer
		return worldModel;
	}

	WorldModelPtr WorldModelManager::Find(const std::string& name)
	{
		const auto it = m_worldModels.find(name);
		if (it != m_worldModels.end())
		{
			return it->second;
		}

		return nullptr;
	}

	WorldModelPtr WorldModelManager::CreateManual(const std::string& name)
	{
		const auto it = m_worldModels.find(name);
		ASSERT(it == m_worldModels.end());

		auto worldModel = std::make_shared<WorldModel>();
		m_worldModels[name] = worldModel;

		return worldModel;
	}

	void WorldModelManager::Remove(const std::string& name)
	{
		const auto it = m_worldModels.find(name);
		if (it != m_worldModels.end())
		{
			m_worldModels.erase(it);
		}
	}

	void WorldModelManager::Clear()
	{
		m_worldModels.clear();
	}
}
