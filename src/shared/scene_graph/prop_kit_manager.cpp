// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "prop_kit_manager.h"

#include "prop_kit_serializer.h"
#include "assets/asset_registry.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "log/default_log_levels.h"

namespace mmo
{
	PropKitManager& PropKitManager::Get()
	{
		static PropKitManager instance;
		return instance;
	}

	PropKitPtr PropKitManager::Load(const String& path)
	{
		const auto it = m_kits.find(path);
		if (it != m_kits.end())
		{
			return it->second;
		}

		const auto filePtr = AssetRegistry::OpenFile(path);
		if (!filePtr || !*filePtr)
		{
			ELOG("Unable to load prop kit file " << path);
			return nullptr;
		}

		io::StreamSource source{ *filePtr };
		io::Reader reader{ source };

		auto kit = std::make_shared<PropKit>();

		PropKitDeserializer deserializer{ *kit };
		if (!deserializer.Read(reader))
		{
			ELOG("Failed to load prop kit '" << path << "'");
			return nullptr;
		}

		m_kits[path] = kit;
		return kit;
	}

	PropKitPtr PropKitManager::Find(const String& path) const
	{
		const auto it = m_kits.find(path);
		if (it != m_kits.end())
		{
			return it->second;
		}

		return nullptr;
	}

	void PropKitManager::Insert(const String& path, PropKitPtr kit)
	{
		m_kits[path] = std::move(kit);
	}

	void PropKitManager::Remove(const String& path)
	{
		m_kits.erase(path);
	}

	void PropKitManager::Clear()
	{
		m_kits.clear();
	}
}
