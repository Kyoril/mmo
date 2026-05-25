// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.
#pragma once
#include "base/non_copyable.h"
#include "base/typedefs.h"

namespace mmo
{
	class SpawnEditMode;

	/// @brief Draws the spawn palette panel (Map Information, Available Units, Available Objects).
	class SpawnPalettePanel final : public NonCopyable
	{
	public:
		SpawnPalettePanel() = default;
		~SpawnPalettePanel() override = default;

		/// @brief Draw the panel window. Must be called every frame even when mode is nullptr.
		void Draw(const String& id, SpawnEditMode* mode);
	};
}
