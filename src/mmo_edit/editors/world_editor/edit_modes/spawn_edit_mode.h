// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_edit_mode.h"

namespace mmo
{
	class SpawnEditMode final : public WorldEditMode
	{
	public:
		explicit SpawnEditMode(IWorldEditor& worldEditor);
		~SpawnEditMode() override = default;

	public:
		const char* GetName() const override;

		void DrawDetails() override;
	};
}
