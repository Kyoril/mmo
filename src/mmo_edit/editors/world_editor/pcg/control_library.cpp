// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "control_library.h"

#include "assets/asset_registry.h"

#include <algorithm>
#include <iterator>

namespace mmo
{
	std::vector<String> ControlLibrary::ListBiomes() const
	{
		const String prefix = BiomesFolder;
		std::vector<String> biomes;
		for (const String& file : AssetRegistry::ListFiles(prefix, ""))
		{
			const size_t slash = file.find('/', prefix.size());
			if (slash == String::npos || slash == prefix.size())
			{
				// A file directly in the biomes folder is not a biome.
				continue;
			}

			biomes.push_back(file.substr(prefix.size(), slash - prefix.size()));
		}

		std::sort(biomes.begin(), biomes.end());
		biomes.erase(std::unique(biomes.begin(), biomes.end()), biomes.end());
		return biomes;
	}

	std::vector<String> ControlLibrary::ListControlTypes(const String& biome) const
	{
		if (biome.empty())
		{
			return {};
		}

		std::vector<String> types = AssetRegistry::ListFiles(String(BiomesFolder) + biome + "/Controls/", ".hctl");
		std::sort(types.begin(), types.end());
		return types;
	}

	const pcg::graph::ControlType* ControlLibrary::Find(const String& typePath, String* error)
	{
		auto it = m_types.find(typePath);
		if (it == m_types.end())
		{
			Entry entry;
			const std::unique_ptr<std::istream> file = typePath.empty() ? nullptr : AssetRegistry::OpenFile(typePath);
			if (!file)
			{
				entry.error = "control type '" + typePath + "' not found";
			}
			else
			{
				const String text((std::istreambuf_iterator<char>(*file)), std::istreambuf_iterator<char>());
				auto type = std::make_unique<pcg::graph::ControlType>();
				String parseError;
				if (pcg::graph::ParseControlType(text, *type, parseError))
				{
					entry.type = std::move(type);
				}
				else
				{
					entry.error = "control type '" + typePath + "' is invalid: " + parseError;
				}
			}

			it = m_types.emplace(typePath, std::move(entry)).first;
		}

		if (!it->second.type && error)
		{
			*error = it->second.error;
		}

		return it->second.type.get();
	}

	void ControlLibrary::Invalidate(const String& typePath)
	{
		m_types.erase(typePath);
		++m_revision;
	}

	void ControlLibrary::InvalidateAll()
	{
		m_types.clear();
		++m_revision;
	}
}
