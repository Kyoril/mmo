// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <cmath>
#include <filesystem>
#include <limits>
#include <utility>
#include <vector>

namespace mmo
{
	/// @brief Metadata sidecar describing how a zone heightmap image maps into the world.
	struct ZoneMeta
	{
		/// @brief Name of the world (asset path is derived as Worlds/{world}/{world}/...).
		String world;

		/// @brief First page column covered by the image (inclusive, 0..63).
		int32 pageX0 = 0;

		/// @brief First page row covered by the image (inclusive, 0..63).
		int32 pageZ0 = 0;

		/// @brief Last page column covered by the image (inclusive, 0..63).
		int32 pageX1 = 0;

		/// @brief Last page row covered by the image (inclusive, 0..63).
		int32 pageZ1 = 0;

		/// @brief World height mapped to pixel value 0.
		float minY = 0.0f;

		/// @brief World height mapped to pixel value 65535.
		float maxY = 0.0f;

		/// @brief Optional per-tile material asset path. Empty = use the world default material.
		String material;

		/// @brief Optional water surface height. When set (not NaN), import flags water
		///	       quads wherever the terrain lies below this level.
		float waterLevel = std::numeric_limits<float>::quiet_NaN();

		/// @brief Water surface material asset path (used when waterLevel is set).
		String waterMaterial = "Worlds/Water_Base.hmat";

		/// @brief Water type written for flagged water quads (see terrain::WaterType, 1 = Water, 2 = Ocean).
		uint8 waterType = 1;

		/// @brief Optional 16-bit PNG holding one zone (area) id per terrain tile, sized
		///	       (pages x · 16) × (pages z · 16). Resolved relative to the metadata file. Empty = no zones.
		std::filesystem::path zoneMap;

		/// @brief When set, pages that already exist are not overwritten.
		bool skipExistingPages = false;

		/// @brief With skipExistingPages: also give the unzoned tiles (id 0) of existing pages the zone
		///	       from the zone map. Everything else in those pages stays untouched. Only v2 pages can be updated.
		bool fillExistingZones = false;

		/// @brief Optional list of pages (x, z) to write. Empty = every page of the rect. Pages
		///	       outside this list are neither written nor touched.
		std::vector<std::pair<int32, int32>> pages;

		/// @brief Whether a water level has been configured.
		[[nodiscard]] bool HasWaterLevel() const { return !std::isnan(waterLevel); }

		/// @brief Number of pages covered horizontally.
		[[nodiscard]] int32 GetPageCountX() const { return pageX1 - pageX0 + 1; }

		/// @brief Number of pages covered vertically.
		[[nodiscard]] int32 GetPageCountZ() const { return pageZ1 - pageZ0 + 1; }

		/// @brief Validates the page rect and height range.
		[[nodiscard]] bool IsValid() const;
	};

	/// @brief Loads zone metadata from a JSON file.
	///	@param path Path of the JSON file to load.
	///	@param out Receives the parsed metadata.
	///	@return true on success, false if the file is missing or malformed.
	bool LoadZoneMeta(const std::filesystem::path &path, ZoneMeta &out);

	/// @brief Saves zone metadata as a JSON file.
	///	@param path Path of the JSON file to write.
	///	@param meta The metadata to serialize.
	///	@return true on success.
	bool SaveZoneMeta(const std::filesystem::path &path, const ZoneMeta &meta);
}
