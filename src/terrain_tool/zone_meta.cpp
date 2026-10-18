// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "zone_meta.h"

#include "log/default_log_levels.h"
#include "terrain/constants.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace mmo
{
	bool ZoneMeta::IsValid() const
	{
		if (world.empty())
		{
			ELOG("Zone metadata is missing the world name!");
			return false;
		}

		const int32 maxPage = static_cast<int32>(terrain::constants::MaxPages) - 1;
		if (pageX0 < 0 || pageZ0 < 0 || pageX1 > maxPage || pageZ1 > maxPage || pageX0 > pageX1 || pageZ0 > pageZ1)
		{
			ELOG("Zone metadata page rect (" << pageX0 << "," << pageZ0 << ")-(" << pageX1 << "," << pageZ1 << ") is invalid (valid page indices are 0.." << maxPage << ")!");
			return false;
		}

		if (!(maxY > minY))
		{
			ELOG("Zone metadata height range is invalid (maxY must be greater than minY)!");
			return false;
		}

		if (waterType == 0 || waterType >= static_cast<uint8>(terrain::WaterType::Count_))
		{
			ELOG("Zone metadata water type " << static_cast<uint32>(waterType) << " is invalid (valid: 1 Water, 2 Ocean, 3 Lava, 4 Slime)!");
			return false;
		}

		for (const auto &[x, z] : pages)
		{
			if (x < pageX0 || x > pageX1 || z < pageZ0 || z > pageZ1)
			{
				ELOG("Zone metadata lists page " << x << "," << z << " outside of its page rect!");
				return false;
			}
		}

		return true;
	}

	bool LoadZoneMeta(const std::filesystem::path &path, ZoneMeta &out)
	{
		std::ifstream file{ path };
		if (!file.is_open())
		{
			ELOG("Failed to open zone metadata file '" << path.string() << "'!");
			return false;
		}

		const nlohmann::json json = nlohmann::json::parse(file, nullptr, false);
		if (json.is_discarded() || !json.is_object())
		{
			ELOG("Zone metadata file '" << path.string() << "' is not valid JSON!");
			return false;
		}

		out.world = json.value("world", String());
		out.minY = json.value("minY", 0.0f);
		out.maxY = json.value("maxY", 0.0f);
		out.material = json.value("material", String());
		out.waterLevel = json.value("waterLevel", std::numeric_limits<float>::quiet_NaN());
		out.waterMaterial = json.value("waterMaterial", String("Worlds/Water_Base.hmat"));
		out.waterType = static_cast<uint8>(json.value("waterType", 1));
		out.skipExistingPages = json.value("skipExistingPages", false);
		out.fillExistingZones = json.value("fillExistingZones", false);
		out.applyAsDelta = json.value("applyAsDelta", false);
		out.setWaterMaterial = json.value("setWaterMaterial", false);
		if (const auto flow = json.find("waterFlow"); flow != json.end() && flow->is_object())
		{
			out.flowUVScale = flow->value("uvScale", 1.0f / 16.0f);
			out.flowFalloff = flow->value("falloff", 60.0f);
			out.flowMinSpeed = flow->value("minSpeed", 0.08f);
			if (const auto path = flow->find("path"); path != flow->end() && path->is_array())
			{
				for (const auto &point : *path)
				{
					if (!point.is_array() || point.size() < 2)
					{
						ELOG("Zone metadata waterFlow path points must be [x, z] or [x, z, halfWidth]!");
						return false;
					}
					ZoneMeta::FlowPoint flowPoint;
					flowPoint.x = point[0].get<float>();
					flowPoint.z = point[1].get<float>();
					flowPoint.halfWidth = point.size() > 2 ? point[2].get<float>() : 10.0f;
					out.flowPath.push_back(flowPoint);
				}
			}
			if (out.flowPath.size() < 2)
			{
				ELOG("Zone metadata waterFlow needs a path of at least two points!");
				return false;
			}
		}

		if (const auto tiles = json.find("tileMaterials"); tiles != json.end() && tiles->is_object())
		{
			const String mask = tiles->value("mask", String());
			out.tileMaterialMask = std::filesystem::path(mask).is_absolute() ? std::filesystem::path(mask) : path.parent_path() / mask;
			out.tileMaterials = tiles->value("materials", std::vector<String>());
			if (mask.empty() || out.tileMaterials.empty())
			{
				ELOG("Zone metadata tileMaterials needs a mask and at least one material!");
				return false;
			}
		}
		if (const String clearWater = json.value("clearWaterMask", String()); !clearWater.empty())
		{
			out.clearWaterMask = std::filesystem::path(clearWater).is_absolute() ? std::filesystem::path(clearWater) : path.parent_path() / clearWater;
		}

		out.paint.clear();
		if (const auto paint = json.find("paint"); paint != json.end() && paint->is_array())
		{
			for (const auto &entry : *paint)
			{
				ZoneMeta::PaintLayer layer;
				const String mask = entry.value("mask", String());
				layer.mask = std::filesystem::path(mask).is_absolute() ? std::filesystem::path(mask) : path.parent_path() / mask;
				layer.layer = static_cast<uint8>(entry.value("layer", 0));
				layer.strength = entry.value("strength", 1.0f);
				layer.remove = entry.value("mode", String()) == "remove";
				if (mask.empty() || layer.layer > 3)
				{
					ELOG("Zone metadata paint entries need a mask and a layer from 0 to 3!");
					return false;
				}
				out.paint.push_back(layer);
			}
		}

		if (const String zoneMap = json.value("zoneMap", String()); !zoneMap.empty())
		{
			out.zoneMap = std::filesystem::path(zoneMap).is_absolute() ? std::filesystem::path(zoneMap) : path.parent_path() / zoneMap;
		}

		out.pages.clear();
		if (const auto pages = json.find("pages"); pages != json.end() && pages->is_array())
		{
			for (const auto &page : *pages)
			{
				if (!page.is_array() || page.size() != 2)
				{
					ELOG("Zone metadata file '" << path.string() << "' has a malformed pages entry (expected [x, z])!");
					return false;
				}

				out.pages.emplace_back(page[0].get<int32>(), page[1].get<int32>());
			}
		}

		if (const auto rect = json.find("pageRect"); rect != json.end() && rect->is_object())
		{
			out.pageX0 = rect->value("x0", 0);
			out.pageZ0 = rect->value("z0", 0);
			out.pageX1 = rect->value("x1", 0);
			out.pageZ1 = rect->value("z1", 0);
		}
		else
		{
			ELOG("Zone metadata file '" << path.string() << "' is missing the pageRect object!");
			return false;
		}

		return out.IsValid();
	}

	bool SaveZoneMeta(const std::filesystem::path &path, const ZoneMeta &meta)
	{
		nlohmann::json json;
		json["world"] = meta.world;
		json["pageRect"] = { { "x0", meta.pageX0 }, { "z0", meta.pageZ0 }, { "x1", meta.pageX1 }, { "z1", meta.pageZ1 } };
		json["minY"] = meta.minY;
		json["maxY"] = meta.maxY;
		json["material"] = meta.material;
		if (meta.HasWaterLevel())
		{
			json["waterLevel"] = meta.waterLevel;
			json["waterMaterial"] = meta.waterMaterial;
			json["waterType"] = meta.waterType;
		}

		std::ofstream file{ path };
		if (!file.is_open())
		{
			ELOG("Failed to create zone metadata file '" << path.string() << "'!");
			return false;
		}

		file << json.dump(2) << std::endl;
		return true;
	}
}
