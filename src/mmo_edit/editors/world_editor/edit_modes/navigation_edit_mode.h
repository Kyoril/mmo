// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include <memory>

#include "world_edit_mode.h"

namespace mmo
{
	namespace nav
	{
		class Map;
	}

	class DetourDebugDraw;

	class NavigationEditMode final : public WorldEditMode
	{
	public:
		explicit NavigationEditMode(IWorldEditor& worldEditor);
		~NavigationEditMode() override = default;

	public:
		const char* GetName() const override;

		void DrawDetails() override;

		void OnActivate() override;
		void OnDeactivate() override;

	private:
		// Navigation visualization options
		bool m_showNavMesh = true;
		bool m_showNavMeshPortals = true;
		bool m_showNavMeshBVTree = false;
		bool m_showNavMeshNodes = false;
		
		std::unique_ptr<DetourDebugDraw> m_detourDebugDraw;
		std::unique_ptr<nav::Map> m_navMap;
		
		void UpdateNavigationVisibility();
	};
}