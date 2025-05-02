// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "navigation_edit_mode.h"

#include <DetourDebugDraw.h>
#include <imgui.h>

#include "detour_debug_drawer.h"
#include "nav_build/common.h"
#include "nav_mesh/map.h"
#include "scene_graph/material_manager.h"

#include "base/filesystem.h"

namespace mmo
{
	NavigationEditMode::NavigationEditMode(IWorldEditor& worldEditor)
		: WorldEditMode(worldEditor)
	{
		// Initialize the detour debug draw
		m_detourDebugDraw = std::make_unique<DetourDebugDraw>(*m_worldEditor.GetCamera().GetScene(), 
			MaterialManager::Get().Load("Models/Engine/DetourDebug.hmat"));
			
		// Create a nav map
		const auto path = worldEditor.GetWorldPath();
		m_navMap = std::make_unique<nav::Map>(path.filename().replace_extension().string());
	}

	const char* NavigationEditMode::GetName() const
	{
		static const char* s_name = "Navigation";
		return s_name;
	}

	void NavigationEditMode::DrawDetails()
	{
		if (ImGui::CollapsingHeader("Navigation Mesh Visualization", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bool optionsChanged = false;
			
			optionsChanged |= ImGui::Checkbox("Show NavMesh", &m_showNavMesh);
			ImGui::SameLine();
			optionsChanged |= ImGui::Checkbox("Show Portals", &m_showNavMeshPortals);
			
			optionsChanged |= ImGui::Checkbox("Show BVTree", &m_showNavMeshBVTree);
			ImGui::SameLine();
			optionsChanged |= ImGui::Checkbox("Show Nodes", &m_showNavMeshNodes);
			
			if (optionsChanged)
			{
				UpdateNavigationVisibility();
			}
			
			// Add buttons to load specific navmesh pages
			ImGui::Separator();
			ImGui::Text("Page Controls:");
			
			if (ImGui::Button("Clear All Pages"))
			{
				m_navMap->UnloadAllPages();
				UpdateNavigationVisibility();
			}
			
			// Simple page loading UI - could be expanded with a grid view
			ImGui::Text("Load page at coordinates:");
			static int pageX = 32;
			static int pageY = 32;
			if (ImGui::InputInt("Page X", &pageX))
			{
				pageX = std::max(0, std::min(63, pageX));
			}
			if (ImGui::InputInt("Page Y", &pageY)) 
			{
				pageY = std::max(0, std::min(63, pageY));
			}
			
			if (ImGui::Button("Load Page"))
			{
				m_navMap->LoadPage(pageX, pageY);
				UpdateNavigationVisibility();
			}
		}
	}

	void NavigationEditMode::OnActivate()
	{
		WorldEditMode::OnActivate();
		
		// Make sure navigation is visible when mode is activated
		UpdateNavigationVisibility();
	}

	void NavigationEditMode::OnDeactivate()
	{
		WorldEditMode::OnDeactivate();
		
		// Clear any visualization when mode is deactivated
		if (m_detourDebugDraw)
		{
			m_detourDebugDraw->Clear();
		}
	}

	void NavigationEditMode::UpdateNavigationVisibility()
	{
		if (!m_detourDebugDraw || !m_navMap)
		{
			return;
		}
		
		// Clear previous visualization
		m_detourDebugDraw->Clear();
		
		// Calculate debug draw flags based on settings
		unsigned int navMeshDrawFlags = 0;
				
		// Only draw if we have flags set
		if (m_showNavMesh)
		{
			// Ground tiles
			duDebugDrawNavMeshPolysWithFlags(m_detourDebugDraw.get(), m_navMap->GetNavMesh(), poly_flags::Ground, 0xff00ff00);
			duDebugDrawNavMeshPolysWithFlags(m_detourDebugDraw.get(), m_navMap->GetNavMesh(), poly_flags::Entity, 0xffff4444);
			duDebugDrawNavMeshPolysWithFlags(m_detourDebugDraw.get(), m_navMap->GetNavMesh(), poly_flags::Steep, 0xff0000ff);
		}

		if (m_showNavMeshPortals)
		{
			duDebugDrawNavMeshPortals(m_detourDebugDraw.get(), m_navMap->GetNavMesh());
		}

		if (m_showNavMeshBVTree)
		{
			duDebugDrawNavMeshBVTree(m_detourDebugDraw.get(), m_navMap->GetNavMesh());
		}

		if (m_showNavMeshNodes)
		{
			duDebugDrawNavMeshNodes(m_detourDebugDraw.get(), m_navMap->GetNavMeshQuery());
		}
	}
}