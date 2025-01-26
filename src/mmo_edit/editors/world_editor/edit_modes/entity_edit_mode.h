// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_edit_mode.h"

namespace mmo
{
	class EntityEditMode final : public WorldEditMode
	{
	public:
		explicit EntityEditMode(IWorldEditor& worldEditor);
		~EntityEditMode() override = default;

	public:
		const char* GetName() const override;

		void DrawDetails() override;

		bool SupportsViewportDrop() const override { return true; }

		void OnViewportDrop(float x, float y) override;
		
	};
}
