// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "material_manager.h"

#include "material_instance_serializer.h"
#include "material_serializer.h"
#include "assets/asset_registry.h"
#include "graphics/material_instance.h"
#include "log/default_log_levels.h"

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

	MaterialPtr MaterialManager::Load(std::string_view filename)
	{
		const auto it = m_materials.find(String(filename));
		if (it != m_materials.end())
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
		if (p.extension() == ".hmi")
		{
			// TODO: Default material
			auto defaultMaterial = std::make_shared<Material>("__DEFAULT__");

			auto materialInstance = std::make_shared<MaterialInstance>(filename, defaultMaterial);

			io::StreamSource source{ *file };
			io::Reader reader{ source };

			MaterialInstanceDeserializer deserializer{ *materialInstance };
			if (!deserializer.Read(reader))
			{
				ELOG("Failed to load material");
				return nullptr;
			}

			materialInstance->Update();

			m_materials.emplace(filename, materialInstance);
			return materialInstance;
		}
		else if(p.extension() == ".hmat")
		{
			auto material = std::make_shared<Material>(filename);

			io::StreamSource source{ *file };
			io::Reader reader{ source };

			MaterialDeserializer deserializer{ *material };
			if (!deserializer.Read(reader))
			{
				ELOG("Failed to load material");
				return nullptr;
			}

			material->Update();

			m_materials.emplace(filename, material);
			return material;
		}
		else
		{
			ELOG("Failed to load material: Unknown file extension found '" << p.extension().string() << "'");
		}

		return nullptr;
	}

	MaterialPtr MaterialManager::CreateManual(const std::string_view name)
	{
		const auto [it, inserted] = m_materials.emplace(name, std::make_shared<Material>(name));
		return it->second;
	}

	void MaterialManager::Remove(const std::string_view filename)
	{
		m_materials.erase(String(filename));
	}

	void MaterialManager::RemoveAllUnreferenced()
	{
		const size_t erased = std::erase_if(m_materials, [](const std::pair<std::string, MaterialPtr>& pair)
		{
			return pair.second.use_count() <= 1;
		});

		DLOG("Removed " << erased << " unreferenced materials");
	}
}
