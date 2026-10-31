// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "overlay.h"

#include <algorithm>
#include <fstream>

#include "nlohmann/json.hpp"

namespace mmo
{
	namespace
	{
		nlohmann::json WarningsToJson(const std::vector<CookWarning>& warnings)
		{
			std::vector<CookWarning> sorted = warnings;
			std::sort(sorted.begin(), sorted.end());
			sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

			nlohmann::json array = nlohmann::json::array();
			for (const CookWarning& warning : sorted)
			{
				array.push_back({ { "asset", warning.asset }, { "message", warning.message } });
			}
			return array;
		}

		bool WriteJson(const std::filesystem::path& path, const nlohmann::json& json)
		{
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			if (!file)
			{
				return false;
			}

			// Object keys are sorted by nlohmann::json, so the bytes only depend on the content.
			file << json.dump(1, '\t') << '\n';
			return static_cast<bool>(file);
		}
	}

	bool WriteWarningsFile(const std::filesystem::path& path, const std::vector<CookWarning>& warnings)
	{
		return WriteJson(path, WarningsToJson(warnings));
	}

	bool ReadWarningsFile(const std::filesystem::path& path, std::vector<CookWarning>& out)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			return false;
		}

		const nlohmann::json json = nlohmann::json::parse(file, nullptr, false);
		if (json.is_discarded() || !json.is_array())
		{
			return false;
		}

		std::vector<CookWarning> warnings;
		for (const auto& entry : json)
		{
			if (!entry.is_object() || !entry.contains("asset") || !entry.contains("message")
				|| !entry["asset"].is_string() || !entry["message"].is_string())
			{
				return false;
			}

			warnings.push_back({ entry["asset"].get<String>(), entry["message"].get<String>() });
		}

		out = std::move(warnings);
		return true;
	}

	bool WriteOverlayManifest(const std::filesystem::path& overlayDir, const std::vector<String>& deletePaths, const std::vector<CookWarning>& warnings)
	{
		std::vector<String> sortedDeletes = deletePaths;
		std::sort(sortedDeletes.begin(), sortedDeletes.end());
		sortedDeletes.erase(std::unique(sortedDeletes.begin(), sortedDeletes.end()), sortedDeletes.end());

		nlohmann::json manifest;
		manifest["schema"] = 1;
		manifest["delete"] = sortedDeletes;
		manifest["warnings"] = WarningsToJson(warnings);

		std::error_code error;
		std::filesystem::create_directories(overlayDir, error);
		return WriteJson(overlayDir / "overlay.json", manifest);
	}
}
