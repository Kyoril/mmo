// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "flow_paths_file.h"

#include "assets/asset_registry.h"

#include "nlohmann/json.hpp"

#include <iterator>
#include <memory>

namespace mmo
{
	bool ReadFlowPathsFile(const String& fileName, FlowPathsFile& out, String& error)
	{
		out = FlowPathsFile();
		if (!AssetRegistry::HasFile(fileName))
		{
			return true;
		}

		const std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(fileName);
		if (!file)
		{
			error = "river flow file '" + fileName + "' could not be opened";
			return false;
		}

		const std::string text((std::istreambuf_iterator<char>(*file)), std::istreambuf_iterator<char>());
		const nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
		if (json.is_discarded() || !json.is_object())
		{
			error = "river flow file '" + fileName + "' could not be parsed";
			return false;
		}

		const auto readSetting = [&json](const char* key, float& value)
		{
			const auto it = json.find(key);
			if (it != json.end() && it->is_number())
			{
				value = it->get<float>();
			}
		};
		readSetting("uvScale", out.settings.uvScale);
		readSetting("falloff", out.settings.falloff);
		readSetting("minSpeed", out.settings.minSpeed);

		const auto rivers = json.find("rivers");
		if (rivers == json.end() || !rivers->is_array())
		{
			return true;
		}

		for (const auto& entry : *rivers)
		{
			if (!entry.is_object())
			{
				continue;
			}

			String name = "River " + std::to_string(out.rivers.size() + 1);
			if (const auto it = entry.find("name"); it != entry.end() && it->is_string())
			{
				name = it->get<std::string>();
			}

			terrain_io::water_flow::FlowPath flowPath;
			if (const auto path = entry.find("path"); path != entry.end() && path->is_array())
			{
				for (const auto& point : *path)
				{
					if (!point.is_array() || point.size() < 2 || !point[0].is_number() || !point[1].is_number() ||
						(point.size() > 2 && !point[2].is_number()))
					{
						continue;
					}

					terrain_io::water_flow::FlowPoint flowPoint;
					flowPoint.x = point[0].get<float>();
					flowPoint.z = point[1].get<float>();
					flowPoint.halfWidth = point.size() > 2 ? point[2].get<float>() : DefaultFlowPointHalfWidth;
					flowPath.push_back(flowPoint);
				}
			}
			out.rivers.emplace_back(std::move(name), terrain_io::water_flow::ToSpline(flowPath));
		}
		return true;
	}

	bool WriteFlowPathsFile(const String& fileName, const FlowPathsFile& file, String& error)
	{
		if (file.rivers.empty())
		{
			if (AssetRegistry::HasFile(fileName))
			{
				AssetRegistry::RemoveFile(fileName);
			}
			return true;
		}

		nlohmann::json json;
		json["uvScale"] = file.settings.uvScale;
		json["falloff"] = file.settings.falloff;
		json["minSpeed"] = file.settings.minSpeed;
		nlohmann::json rivers = nlohmann::json::array();
		for (const auto& river : file.rivers)
		{
			nlohmann::json path = nlohmann::json::array();
			for (const auto& point : river.second.points)
			{
				path.push_back({ point.x, point.z, point.width });
			}
			rivers.push_back({ { "name", river.first }, { "path", std::move(path) } });
		}
		json["rivers"] = std::move(rivers);

		const std::unique_ptr<std::ostream> stream = AssetRegistry::CreateNewFile(fileName);
		if (!stream)
		{
			error = "river flow file '" + fileName + "' could not be written";
			return false;
		}

		*stream << json.dump(1, '\t');
		return true;
	}
}
