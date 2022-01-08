// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#include "world_grid.h"

#include "frame_ui/color.h"

namespace mmo
{
	WorldGrid::WorldGrid(GraphicsDevice& device)
		: m_device(device)
	{
		m_renderObject = std::make_unique<ManualRenderObject>(m_device);

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

	void WorldGrid::Update()
	{

	}

	void WorldGrid::UpdatePosition(const Vector3& cameraPosition)
	{
		Vector3 newPosition = cameraPosition;

		const Vector3 mask{1.0f, 0.0f, 1.0f};
		newPosition *= mask;
		newPosition = SnapToGrid(newPosition);

		m_origin = newPosition;
	}

	void WorldGrid::Render() const
	{
		Matrix4 world = Matrix4::Identity;
		world.MakeTrans(m_origin);
		
		m_device.SetTransformMatrix(World, world);

		m_device.SetTopologyType(TopologyType::LineList);
		m_device.SetVertexFormat(VertexFormat::PosColor);
		m_device.SetBlendMode(BlendMode::Opaque);

		m_renderObject->Render();
	}

	void WorldGrid::SetupGrid() const
	{
		m_renderObject->Clear();
		
		const float width = m_numCols * m_gridSize;
		const float height = m_numRows * m_gridSize;
		
		const Vector3 gridOrigin { -width / 2.0f, 0.0f, -height / 2.0f };
		
		const auto operation = m_renderObject->AddLineListOperation();
		
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
}
