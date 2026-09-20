// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "proto_data/project.h"

#include <functional>

#include <imgui.h>

namespace mmo
{
	/// @brief Draws a filterable combo that picks an environment profile id.
	/// @param profiles The profile table.
	/// @param label The combo label.
	/// @param currentId The selected id, 0 for none.
	/// @param filter Persistent filter state owned by the calling window.
	/// @param setter Receives the chosen id (0 for the none entry).
	/// @param noneLabel Text of the 0 entry, e.g. "(inherit)".
	/// @return True when the selection changed.
	bool DrawEnvironmentProfileCombo(const proto::EnvironmentProfileManager& profiles, const char* label, uint32 currentId,
		ImGuiTextFilter& filter, const std::function<void(uint32)>& setter, const char* noneLabel);
}
