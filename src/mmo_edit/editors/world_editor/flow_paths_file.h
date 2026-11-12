// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"
#include "math/path_spline.h"
#include "terrain_io/water_flow.h"

#include <utility>
#include <vector>

namespace mmo
{
	/// @brief Water material that reads the flow data. Pages that receive river flow get it assigned.
	constexpr const char* RiverWaterMaterial = "Worlds/Water_River.hmat";

	/// @brief Half width given to a .hflow path point that has no third value.
	constexpr float DefaultFlowPointHalfWidth = 12.0f;

	/// @brief The hand-drawn rivers of a world (its .hflow file): name and path of each river, plus the flow
	///        settings shared by every river path of the world.
	struct FlowPathsFile
	{
		/// @brief Name and path of each river, in file order. A path is an open linear spline in downstream order
		///        whose point width is the channel's half width (terrain_io::water_flow::ToSpline).
		std::vector<std::pair<String, PathSpline>> rivers;

		/// @brief Flow settings of the world.
		terrain_io::water_flow::FlowSettings settings;
	};

	/// @brief Reads a world's .hflow file (JSON: uvScale, falloff, minSpeed, rivers [{ name, path [[x, z, halfWidth]] }]).
	///        Missing settings keep their defaults, a river without a name is named "River <n>", a point with two
	///        values gets DefaultFlowPointHalfWidth and malformed points are skipped.
	/// @param fileName Asset path of the file.
	/// @param out Receives the rivers and settings; a missing file gives no rivers and default settings.
	/// @param error Receives the reason when the call returns false.
	/// @return False if the file exists but could not be opened or parsed (out then holds no rivers and default
	///         settings).
	bool ReadFlowPathsFile(const String& fileName, FlowPathsFile& out, String& error);

	/// @brief Writes a world's .hflow file in the format ReadFlowPathsFile reads (tab-indented JSON, deterministic).
	///        Without rivers the file is removed instead.
	/// @param fileName Asset path of the file.
	/// @param file The rivers and settings to write.
	/// @param error Receives the reason when the call returns false.
	/// @return False if the file could not be written.
	bool WriteFlowPathsFile(const String& fileName, const FlowPathsFile& file, String& error);
}
