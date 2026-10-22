// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "terrain_io/water_flow.h"

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

		/// @brief Optional 16-bit grey map with one pixel per water quad of the rect ((pages x * 128) by
		///	       (pages z * 128)) giving each quad its own water level, for lakes above the sea. Pixel 0
		///	       means "no level here" (waterLevel applies, if set); other pixels map linearly from
		///	       waterLevelMapMinY (1) to waterLevelMapMaxY (65535). Full imports only.
		std::filesystem::path waterLevelMap;

		/// @brief Water level of waterLevelMap pixel value 1.
		float waterLevelMapMinY = 0.0f;

		/// @brief Water level of waterLevelMap pixel value 65535.
		float waterLevelMapMaxY = 0.0f;

		/// @brief Water type of the tiles whose water comes from waterLevelMap.
		uint8 waterLevelMapType = 1;

		/// @brief Whether any water source (waterLevel or waterLevelMap) is configured.
		[[nodiscard]] bool HasAnyWater() const { return HasWaterLevel() || !waterLevelMap.empty(); }

		/// @brief Optional 16-bit PNG holding one zone (area) id per terrain tile, sized
		///	       (pages x · 16) × (pages z · 16). Resolved relative to the metadata file. Empty = no zones.
		std::filesystem::path zoneMap;

		/// @brief When set, pages that already exist are not overwritten.
		bool skipExistingPages = false;

		/// @brief With skipExistingPages: also give the unzoned tiles (id 0) of existing pages the zone
		///	       from the zone map. Everything else in those pages stays untouched. Only v2 pages can be updated.
		bool fillExistingZones = false;

		/// @brief When set, the heightmap holds height offsets (minY..maxY) that are added to the
		///	       existing pages instead of replacing them. Everything else in those pages (painted
		///	       layers, holes, zones, colours, materials, water) is kept; normals are recomputed
		///	       only where the terrain changed, and water quads are only ever added (with
		///	       waterLevel). Every page of the rect must already exist.
		bool applyAsDelta = false;

		/// @brief With applyAsDelta: give every written page that holds water the waterMaterial,
		///	       replacing its current surface material (e.g. to unify a river across pages).
		bool setWaterMaterial = false;

		/// @brief With applyAsDelta: optional 16-bit grey mask with one pixel per water quad of the rect
		///	       ((pages x · 128) × (pages z · 128)); quads under white pixels lose their water.
		std::filesystem::path clearWaterMask;

		/// @brief One splat layer painted by a delta import.
		struct PaintLayer
		{
			/// @brief 16-bit grey mask at splat resolution of the rect ((pages x · 1008 + 1) square); white paints fully.
			std::filesystem::path mask;

			/// @brief Splat layer (0..3) the mask paints.
			uint8 layer = 0;

			/// @brief Scales the mask (0..1).
			float strength = 1.0f;

			/// @brief When set, the mask removes this layer instead (its weight moves to layer 0).
			bool remove = false;
		};

		/// @brief With applyAsDelta: splat layers to paint over the existing weights, in order.
		std::vector<PaintLayer> paint;

		/// @brief With applyAsDelta: optional 16-bit grey mask with one pixel per terrain tile of the rect
		///	       ((pages x · 16) × (pages z · 16)). A value k > 0 gives the tile tileMaterials[k - 1].
		std::filesystem::path tileMaterialMask;

		/// @brief Material asset names referenced by tileMaterialMask.
		std::vector<String> tileMaterials;

		/// @brief One point of a river's flow path: position and half width of the channel there.
		typedef terrain_io::water_flow::FlowPoint FlowPoint;

		/// @brief With applyAsDelta: optional river flow path, downstream order. Every written page
		///	       with water gets flow-aligned water UVs and a flow speed from it.
		std::vector<FlowPoint> flowPath;

		/// @brief Further flow paths (branches). Each point takes its flow from the nearest path;
		///	       a branch's distance downstream starts at 0.
		std::vector<std::vector<FlowPoint>> flowBranches;

		/// @brief Water UV units per world unit along and across the flow (the world mapping uses 1/16).
		float flowUVScale = 1.0f / 16.0f;

		/// @brief Distance beyond the channel's half width over which the flow slows to flowMinSpeed.
		float flowFalloff = 60.0f;

		/// @brief Slowest flow (0..1), used for still water off the channel such as bays and ponds.
		float flowMinSpeed = 0.08f;

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
