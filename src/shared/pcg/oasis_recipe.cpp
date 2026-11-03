// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "oasis_recipe.h"

#include "nlohmann/json.hpp"

#include <algorithm>

namespace mmo::pcg
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr double kFloatLimit = 3.0e38;

		void ReadFloat(const Json& obj, const char* key, float& out)
		{
			const auto it = obj.find(key);
			if (it != obj.end() && it->is_number())
			{
				// Clamped before the cast: a double beyond the float range would be undefined behaviour.
				const double value = it->get<double>();
				out = static_cast<float>(std::max(-kFloatLimit, std::min(kFloatLimit, value)));
			}
		}

		void ReadUInt(const Json& obj, const char* key, uint32& out)
		{
			const auto it = obj.find(key);
			if (it != obj.end() && it->is_number() && it->get<double>() >= 0.0)
			{
				out = static_cast<uint32>(std::min(it->get<double>(), 4.0e9));
			}
		}

		void ReadString(const Json& obj, const char* key, String& out)
		{
			const auto it = obj.find(key);
			if (it != obj.end() && it->is_string())
			{
				out = it->get<String>();
			}
		}

		void ReadList(const Json& obj, const char* key, std::vector<String>& out)
		{
			const auto it = obj.find(key);
			if (it == obj.end() || !it->is_array())
			{
				return;
			}
			out.clear();
			for (const Json& item : *it)
			{
				if (item.is_string())
				{
					out.push_back(item.get<String>());
				}
			}
		}
	}

	bool ParseOasisRecipe(const String& jsonText, OasisParams& params, OasisAssets& assets)
	{
		const Json root = Json::parse(jsonText, nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			return false;
		}

		const auto pit = root.find("params");
		const Json& p = (pit != root.end() && pit->is_object()) ? *pit : root;
		const auto ait = root.find("assets");
		const Json& a = (ait != root.end() && ait->is_object()) ? *ait : root;

#define PCG_F(name) ReadFloat(p, #name, params.name)
		PCG_F(basinDepth); PCG_F(rimWidth); PCG_F(outerFalloff);
		ReadUInt(p, "terraceSteps", params.terraceSteps);
		PCG_F(terraceSharpness); PCG_F(floorNoiseAmplitude); PCG_F(rimPercentile); PCG_F(waterInset);
		PCG_F(waterDepth); PCG_F(grassBand); PCG_F(dirtBand); PCG_F(bandNoiseAmplitude);
		PCG_F(rockSpacing); PCG_F(rockOffset); PCG_F(rockOffsetJitter); PCG_F(rockYawJitterDeg);
		PCG_F(rockScaleMin); PCG_F(rockScaleMax); PCG_F(rockSink);
		PCG_F(cliffWeight); PCG_F(shelfWeight); PCG_F(clumpWeight);
		PCG_F(palmCell); PCG_F(palmDensity); PCG_F(palmNearWaterBoost); PCG_F(palmScaleMin); PCG_F(palmScaleMax);
		PCG_F(bushCell); PCG_F(bushDensity); PCG_F(waterPlantBand); PCG_F(waterPlantDensity); PCG_F(maxSlopeDeg);
		PCG_F(shelfSink); PCG_F(clumpSink);
#undef PCG_F

		ReadList(a, "cliffs", assets.cliffs);
		ReadList(a, "shelves", assets.shelves);
		ReadList(a, "clumps", assets.clumps);
		ReadList(a, "palms", assets.palms);
		ReadList(a, "bushes", assets.bushes);
		ReadList(a, "waterPlants", assets.waterPlants);
		ReadString(a, "terrainMaterial", assets.terrainMaterial);
		ReadString(a, "waterMaterial", assets.waterMaterial);
		SanitizeParams(params);
		return true;
	}
}
