
#include "world_grid.h"

namespace mmo
{
	WorldGrid::WorldGrid(GraphicsDevice& device)
		: m_graphicsDevice(device)
	{
		SetupGrid();
	}
	
	Vector3 WorldGrid::SnapToGrid(const Vector3& position)
	{
		Vector3 newPosition(position.x, position.y, position.z);

		newPosition.x = static_cast<float>(floorf(newPosition.x / m_gridSize + 0.5f)) * m_gridSize;
		newPosition.y = static_cast<float>(floorf(newPosition.y / m_gridSize + 0.5f)) * m_gridSize;
		newPosition.z = static_cast<float>(floorf(newPosition.z / m_gridSize + 0.5f)) * m_gridSize;

		return newPosition;
	}

	void WorldGrid::Update()
	{

	}

	void WorldGrid::UpdatePosition(const Vector3& cameraPosition)
	{
		float grid = m_gridSize;
		grid *= m_largeGrid;

		Vector3 newPosition = cameraPosition;

		const Vector3 mask{1.0f, 0.0f, 1.0f};
		newPosition *= mask;
		newPosition = SnapToGrid(newPosition);

		m_origin = newPosition;
	}

	void WorldGrid::Render() const
	{
		Matrix4 world;
		world.SetTrans(m_origin);

		Matrix4 view;
		view.SetTrans(m_origin + Vector3::UnitY);

		m_graphicsDevice.SetTransformMatrix(World, world);
		m_graphicsDevice.SetTransformMatrix(View, view);
		
		m_graphicsDevice.SetTopologyType(TopologyType::LineList);
		m_graphicsDevice.SetVertexFormat(VertexFormat::PosColor);
		m_graphicsDevice.SetBlendMode(BlendMode::Opaque);
		m_graphicsDevice.SetFaceCullMode(FaceCullMode::None);

		m_vertexBuffer->Set();
		m_graphicsDevice.Draw(m_vertexBuffer->GetVertexCount());
	}

	void WorldGrid::SetupGrid()
	{
		const float width = m_gridSize * m_numCols;
		const float height = m_gridSize * m_numRows;
		
		Vector3 start, end;
		const Vector3 gridOrigin { -width / 2.0f, 0.0f, -height / 2.0f };

		start.y = 0.0f;
		end.y = 0.0f;

		const auto vertexCount = m_numRows * 2 + m_numCols * 2;
		std::vector<POS_COL_VERTEX> vertices( vertexCount, {{0.0f, 0.0f, 0.0f}, 0});
		
		Vector3 position;
		for (auto i = 0; i < m_numRows * 2; i += 2)
		{
			start.x = 0.0f;
			start.z = i * m_gridSize;

			end.x = width;
			end.z = start.z;
			
			if (i % m_largeGrid != 0)
			{
				vertices[i].color = 0xFFFFFFFF;
				vertices[i + 1].color = 0xFFFFFFFF;
			}
			else
			{
				vertices[i].color = 0xFFA0A0A0;
				vertices[i + 1].color = 0xFFFFFFFF;
			}

			position = gridOrigin + start;
			vertices[i].pos[0] = position.x;
			vertices[i].pos[1] = position.y;
			vertices[i].pos[2] = position.z;
			
			position = gridOrigin + end;
			vertices[i + 1].pos[0] = position.x;
			vertices[i + 1].pos[1] = position.y;
			vertices[i + 1].pos[2] = position.z;
		}

		start.y = 0.0f;
		start.z = 0.0f;
		for (auto i = 0; i < m_numCols * 2; i += 2)
		{
			const auto j = i + m_numRows * 2;

			start.x = i * m_gridSize;
			end.x = start.x;
			end.z = height;
			
			if (i % m_largeGrid != 0)
			{
				vertices[j].color = 0xFFFFFFFF;
				vertices[j + 1].color = 0xFFFFFFFF;
			}
			else
			{
				vertices[j].color = 0xFFA0A0A0;
				vertices[j + 1].color = 0xFFFFFFFF;
			}

			position = gridOrigin + start;
			vertices[j].pos[0] = position.x;
			vertices[j].pos[1] = position.y;
			vertices[j].pos[2] = position.z;
			
			position = gridOrigin + end;
			vertices[j + 1].pos[0] = position.x;
			vertices[j + 1].pos[1] = position.y;
			vertices[j + 1].pos[2] = position.z;
		}
		
		m_vertexBuffer = m_graphicsDevice.CreateVertexBuffer(vertices.size(),
			sizeof(POS_COL_VERTEX), false, vertices.data());
	}
}
