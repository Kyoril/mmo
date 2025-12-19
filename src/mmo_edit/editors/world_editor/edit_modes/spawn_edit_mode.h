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
		explicit SpawnEditMode(IWorldEditor& worldEditor, proto::MapManager& maps, proto::UnitManager& units, proto::ObjectManager& objects);
		~SpawnEditMode() override = default;

	public:
		const char* GetName() const override;

		void DrawDetails() override;

		void OnActivate() override;

		void OnDeactivate() override;

		bool SupportsViewportDrop() const override { return m_mapEntry != nullptr; }

		void OnViewportDrop(float x, float y) override;

	public:
		proto::MapEntry* GetMapEntry() const { return m_mapEntry; }

	private:
		void DetectMapEntry();
		void CreateMapEntry(const String& worldName);
		String ExtractWorldNameFromPath() const;

	private:
		proto::MapManager& m_maps;

		proto::UnitManager& m_units;

		proto::ObjectManager& m_objects;

		proto::MapEntry* m_mapEntry = nullptr;

		const proto::UnitEntry* m_selectedUnit = nullptr;
	};
}
