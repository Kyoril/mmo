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
