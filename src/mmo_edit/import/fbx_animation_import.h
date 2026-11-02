// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <filesystem>
#include <functional>

namespace mmo
{
	class Skeleton;

	/// @brief Returns the engine animation name for an animation take of a model file, or an empty string to skip the take.
	using FbxAnimationNameResolver = std::function<String(const String& takeName)>;

	/// @brief Imports the animation takes of a model file into a skeleton.
	/// @details Keyframes are stored relative to the skeleton's binding pose, so the skeleton must have been
	///          imported from the same rig. Existing animations of the same name are replaced.
	/// @param skeleton The skeleton that receives the animations.
	/// @param file The model file (fbx, gltf, ...) holding the animation takes.
	/// @param resolveName Maps a take name to the animation name to create, or to an empty string to skip the take.
	/// @param scale Uniform scale applied to the animated bone translations. Use the scale the skeleton was imported with.
	/// @return The number of imported animations, or -1 if the file could not be read.
	int32 ImportFbxAnimations(Skeleton& skeleton, const std::filesystem::path& file, const FbxAnimationNameResolver& resolveName, float scale = 1.0f);

	/// @brief Imports animation takes into a skeleton asset and saves it, e.g. for `mmo_edit --import-fbx-animations`.
	/// @param skeletonAsset The .skel asset, relative to the game content root.
	/// @param file The model file holding the animation takes.
	/// @param resolveName Maps a take name to the animation name to create, or to an empty string to skip the take.
	/// @param scale Uniform scale applied to the animated bone translations.
	/// @return true if at least one animation was imported and the skeleton was saved.
	bool ImportFbxAnimationsIntoAsset(const String& skeletonAsset, const std::filesystem::path& file, const FbxAnimationNameResolver& resolveName, float scale = 1.0f);
}
