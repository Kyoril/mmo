// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <string>

namespace mmo
{
	/// Renders every page without starting an updater or touching the installation.
	/// Also exercises page hit tests, scrolling, and keyboard activation.
	bool RenderLauncherPreviews(const std::string& directory);

	/// Verifies that every embedded asset can be found and decoded. Asserts on failure;
	/// called on startup in debug builds.
	void VerifyEmbeddedAssets();
} // namespace mmo
