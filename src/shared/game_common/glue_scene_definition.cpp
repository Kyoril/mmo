// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glue_scene_definition.h"

#include <nlohmann/json.hpp>

namespace mmo
{
	namespace
	{
		using json = nlohmann::json;

		bool readVector(const json& parent, const char* name, Vector3& out)
		{
			if (!parent.is_object() || !parent.contains(name))
			{
				return false;
			}

			const json& v = parent[name];
			if (!v.is_array() || v.size() != 3)
			{
				return false;
			}

			for (size_t i = 0; i < 3; ++i)
			{
				if (!v[i].is_number())
				{
					return false;
				}
			}

			out = Vector3(v[0].get<float>(), v[1].get<float>(), v[2].get<float>());
			return true;
		}

		bool readKey(const json& node, GlueCameraKey& out)
		{
			return readVector(node, "position", out.position) && readVector(node, "lookAt", out.lookAt);
		}

		bool readPages(const json& root, const char* name, std::vector<std::pair<uint8, uint8>>& out)
		{
			if (!root.contains(name) || !root[name].is_array())
			{
				return false;
			}

			for (const json& page : root[name])
			{
				if (!page.is_array() || page.size() != 2 || !page[0].is_number_unsigned() || !page[1].is_number_unsigned())
				{
					return false;
				}

				const uint64 x = page[0].get<uint64>();
				const uint64 y = page[1].get<uint64>();
				if (x > 255 || y > 255)
				{
					return false;
				}

				out.emplace_back(static_cast<uint8>(x), static_cast<uint8>(y));
			}

			return true;
		}

		bool readSpot(const json& node, GlueSpot& out)
		{
			if (!node.is_object())
			{
				return false;
			}

			if (!readVector(node, "position", out.position))
			{
				return false;
			}

			if (!node.contains("facing") || !node["facing"].is_number())
			{
				return false;
			}
			out.facingDegrees = node["facing"].get<float>();

			if (!node.contains("pose") || !node["pose"].is_string())
			{
				return false;
			}
			out.pose = node["pose"].get<std::string>();

			if (node.contains("camera") && !readKey(node["camera"], out.camera))
			{
				return false;
			}

			return true;
		}
	}

	std::optional<GlueSceneDefinition> ParseGlueSceneDefinition(const String& text)
	{
		const json root = json::parse(text, nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			return std::nullopt;
		}

		GlueSceneDefinition result;

		if (!root.contains("world") || !root["world"].is_string())
		{
			return std::nullopt;
		}
		result.world = root["world"].get<std::string>();

		if (!readPages(root, "gatePages", result.gatePages) || !readPages(root, "plazaPages", result.plazaPages))
		{
			return std::nullopt;
		}

		if (!root.contains("environmentProfile") || !root["environmentProfile"].is_number_unsigned())
		{
			return std::nullopt;
		}
		result.environmentProfile = root["environmentProfile"].get<uint32>();

		if (!root.contains("flightPath") || !root["flightPath"].is_array() || root["flightPath"].size() < 2)
		{
			return std::nullopt;
		}
		for (const json& keyNode : root["flightPath"])
		{
			GlueCameraKey key;
			if (!readKey(keyNode, key))
			{
				return std::nullopt;
			}
			result.flightPath.push_back(key);
		}

		if (!root.contains("flightSeconds") || !root["flightSeconds"].is_number())
		{
			return std::nullopt;
		}
		result.flightSeconds = root["flightSeconds"].get<float>();

		if (root.contains("flyOutSeconds"))
		{
			if (!root["flyOutSeconds"].is_number())
			{
				return std::nullopt;
			}
			result.flyOutSeconds = root["flyOutSeconds"].get<float>();
		}

		if (!root.contains("front") || !readSpot(root["front"], result.front))
		{
			return std::nullopt;
		}

		if (!root.contains("spots") || !root["spots"].is_array() || root["spots"].size() != static_cast<size_t>(PlazaSpotCount))
		{
			return std::nullopt;
		}
		for (size_t i = 0; i < result.spots.size(); ++i)
		{
			if (!readSpot(root["spots"][i], result.spots[i]))
			{
				return std::nullopt;
			}
		}

		if (!readVector(root, "entrance", result.entrance))
		{
			return std::nullopt;
		}

		return result;
	}
}
