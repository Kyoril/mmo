// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/math_utils.h"
#include "math/vector3.h"
#include "terrain/constants.h"

#include <vector>

namespace mmo
{
	/// @brief Builds the asset path of a terrain page file (e.g. "Worlds/Test/Test/Terrain/32_32.tile").
	///	@param world The world name.
	///	@param pageX The page column (0..63).
	///	@param pageZ The page row (0..63).
	///	@return The page file asset path.
	String BuildPageFilename(const String &world, int32 pageX, int32 pageZ);

	/// @brief Builds the asset path of a world file (e.g. "Worlds/Test/Test.hwld").
	///	@param world The world name.
	///	@return The world file asset path.
	String BuildWorldFilename(const String &world);

	/// @brief A continuous grid of outer terrain vertices spanning a rectangle of pages.
	/// @details Adjacent pages share their edge vertices, so a rect of n×m pages yields a
	///	         grid of (n·128+1)×(m·128+1) vertices.
	struct ZoneGrid
	{
		/// @brief First page column (inclusive).
		int32 pageX0 = 0;

		/// @brief First page row (inclusive).
		int32 pageZ0 = 0;

		/// @brief Last page column (inclusive).
		int32 pageX1 = 0;

		/// @brief Last page row (inclusive).
		int32 pageZ1 = 0;

		/// @brief Grid width in outer vertices.
		uint32 width = 0;

		/// @brief Grid height in outer vertices.
		uint32 height = 0;

		/// @brief Vertex heights (width × height).
		std::vector<float> heights;

		/// @brief Encoded vertex normals as stored in the page files (width × height).
		std::vector<EncodedNormal8> normals;

		/// @brief Number of pages successfully loaded.
		uint32 loadedPageCount = 0;
	};

	/// @brief Loads all terrain pages of the given rect into one continuous vertex grid.
	/// @details Missing pages are treated as blank (heights 0, up normals) with a warning.
	///	@param world The world name.
	///	@param pageX0 First page column (inclusive).
	///	@param pageZ0 First page row (inclusive).
	///	@param pageX1 Last page column (inclusive).
	///	@param pageZ1 Last page row (inclusive).
	///	@param out Receives the stitched grid.
	///	@return true if at least one page could be loaded.
	bool LoadZoneGrid(const String &world, int32 pageX0, int32 pageZ0, int32 pageX1, int32 pageZ1, ZoneGrid &out);

	/// @brief Computes the float normal of an outer grid vertex exactly like the engine does
	///	       (terrain::Page::CalculateNormalAt), including its scaling constant.
	///	@param heights The vertex height grid.
	///	@param width Grid width in vertices.
	///	@param height Grid height in vertices.
	///	@param x Vertex column.
	///	@param z Vertex row.
	///	@return The normalized vertex normal (Y-up).
	Vector3 ComputeOuterNormal(const std::vector<float> &heights, uint32 width, uint32 height, uint32 x, uint32 z);
}
