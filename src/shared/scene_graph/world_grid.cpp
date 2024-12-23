// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_grid.h"

#include "material_manager.h"
#include "frame_ui/color.h"
#include "scene_graph/camera.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"

namespace mmo
{
	WorldGrid::WorldGrid(Scene& scene, const String& name)
		: m_scene(scene)
		, m_sceneNode(&m_scene.CreateSceneNode(name))
		, m_renderObject(m_scene.CreateManualRenderObject(name))
	{
		m_renderObject->SetRenderQueueGroup(Background);

		m_scene.GetRootSceneNode().AddChild(*m_sceneNode);
		m_sceneNode->AttachObject(*m_renderObject);

		m_gridRendering = m_renderObject->objectRendering.connect(this, &WorldGrid::BeforeGridRendering);

		SetupGrid();
	}
	
	Vector3 WorldGrid::SnapToGrid(const Vector3& position)
	{
		Vector3 newPosition(position.x, position.y, position.z);

		const float grid = m_gridSize * m_largeGrid;

		newPosition.x = floorf(newPosition.x / grid + 0.5f) * grid;
		newPosition.y = floorf(newPosition.y / grid + 0.5f) * grid;
		newPosition.z = floorf(newPosition.z / grid + 0.5f) * grid;

		return newPosition;
	}
	
	void WorldGrid::UpdatePosition(const Vector3& cameraPosition)
	{
		Vector3 newPosition = cameraPosition;

		const Vector3 mask{1.0f, 0.0f, 1.0f};
		newPosition *= mask;
		newPosition = SnapToGrid(newPosition);

		m_sceneNode->SetPosition(newPosition);
	}

	void WorldGrid::SetQueryFlags(const uint32 mask) const
	{
		ASSERT(m_renderObject);
		m_renderObject->SetQueryFlags(mask);
	}

	void WorldGrid::SetupGrid() const
	{
		m_renderObject->Clear();
		
		const float width = m_numCols * m_gridSize;
		const float height = m_numRows * m_gridSize;
		
		const Vector3 gridOrigin { -width / 2.0f, 0.0f, -height / 2.0f };
		
		const auto operation = m_renderObject->AddLineListOperation(MaterialManager::Get().Load("Models/Engine/WorldGrid.hmat"));
		
		const Color darkColor(0.4f, 0.4f, 0.4f);
		const Color lightColor = Color::White;
		
		Vector3 start, end;
		for (auto i = 0; i < m_numRows; ++i)
		{
			start.z = m_gridSize * i;

			end.x = width;
			end.z = start.z;

			auto& line = operation->AddLine(gridOrigin + start, gridOrigin + end);
			line.SetColor(i % m_largeGrid != 0 ? darkColor : lightColor);
		}

		start.z = 0.0f;
		for (auto i = 0; i < m_numCols; ++i)
		{
			start.x = m_gridSize * i;
			end.x = start.x;
			end.z = height;
			
			auto& line = operation->AddLine(gridOrigin + start, gridOrigin + end);
			line.SetColor(i % m_largeGrid != 0 ? darkColor : lightColor);
		}
	}

	bool WorldGrid::BeforeGridRendering(const MovableObject& movableObject, const Camera& camera)
	{
		UpdatePosition(camera.GetDerivedPosition());

		if (m_invalidated)
		{
			SetupGrid();
			m_invalidated = false;
		}

		return true;
	}
}
