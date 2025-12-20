// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "world_edit_mode.h"
#include "proto_data/project.h"

namespace mmo
{
	class AreaTriggerEditMode final : public WorldEditMode
	{
	public:
		explicit AreaTriggerEditMode(IWorldEditor& worldEditor, proto::MapManager& maps, proto::AreaTriggerManager& areaTriggers);
		~AreaTriggerEditMode() override = default;

	public:
		const char* GetName() const override;

		void DrawDetails() override;

		void OnActivate() override;

		void OnDeactivate() override;

	void OnMouseUp(float x, float y) override;
		proto::MapEntry* GetMapEntry() const
		{
			return m_mapEntry;
		}

	private:
		void DetectMapEntry();
		String ExtractWorldNameFromPath() const;
		void LoadAreaTriggersForMap();
		uint32 GenerateUniqueTriggerId();

	private:
		proto::MapManager& m_maps;
		proto::AreaTriggerManager& m_areaTriggers;
		proto::MapEntry* m_mapEntry = nullptr;
		
		enum class TriggerType
		{
			Sphere,
			Box
		};
		
		TriggerType m_selectedTriggerType = TriggerType::Sphere;
	};
}
