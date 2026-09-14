// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/typedefs.h"

#include <optional>

namespace mmo
{
	/// @brief What the Environment Profile Editor asks every open world editor to show.
	/// @remark Main thread only. World editors poll it each frame. Backed by a process-wide
	///         singleton (see GetEnvironmentPreview below), not owned by any one host, so there is
	///         exactly one preview state per editor process.
	struct EnvironmentPreview
	{
		/// @brief When set, world editors snap to this profile instead of the camera zone's.
		std::optional<uint32> profileId;

		/// @brief Time of day shown while profileId is set, 0 = midnight, 0.5 = noon.
		float normalizedTime = 0.5f;

		/// @brief Bumped on every profile, zone or map environment edit so viewers reconvert.
		uint64 revision = 0;

		/// @brief Records that authored environment data changed.
		void NotifyChanged() { ++revision; }
	};

	/// @brief The editor's single preview state.
	inline EnvironmentPreview& GetEnvironmentPreview()
	{
		static EnvironmentPreview s_preview;
		return s_preview;
	}
}
