// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <functional>
#include <string_view>
#include <vector>


namespace mmo
{
	/// @brief Determines whether the asset references of a file are worth scanning for.
	/// @param filename Name of the asset file.
	/// @return true for formats that reference other assets by name (world entities, world models, prop kits,
	///	foliage, terrain pages, meshes, materials, material instances and material functions).
	bool CanReferenceAssets(std::string_view filename);

	/// @brief Finds the names of the assets a binary asset file references.
	/// @details The engine's formats store referenced assets as length-prefixed path strings, so a reference
	///	is found by its file extension and the path characters in front of it. A printable length prefix or
	///	the tail of a neighbouring field can be glued onto the front of a name; exists decides which spelling
	///	is real, and the longest existing one wins. This is a hint for prefetching only: a missed or wrong
	///	reference costs a synchronous load or a wasted read, never correctness.
	/// @param content Content of the scanned file.
	/// @param exists Returns whether an asset of the given name exists.
	/// @param outReferences Receives every existing referenced asset once, with forward slashes, in file order.
	void ScanAssetReferences(std::string_view content, const std::function<bool(const String&)>& exists, std::vector<String>& outReferences);
}
