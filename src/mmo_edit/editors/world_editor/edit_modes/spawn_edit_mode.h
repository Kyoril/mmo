// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_edit_mode.h"
#include "editor_windows/editor_entry_window_base.h"
#include "proto_data/project.h"

namespace mmo
{
	class SpawnEditMode final : public WorldEditMode
	{
	public:
		explicit SpawnEditMode(IWorldEditor& worldEditor, proto::MapManager& maps, proto::UnitManager& units);
		~SpawnEditMode() override = default;

	public:
		const char* GetName() const override;

		void DrawDetails() override;

		void OnDeactivate() override;

	private:
		proto::MapManager& m_maps;

		proto::UnitManager& m_units;

		proto::MapEntry* m_mapEntry = nullptr;

		const proto::UnitEntry* m_selectedUnit = nullptr;
	};
}
