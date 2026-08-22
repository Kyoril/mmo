// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_grid.h"

#include "base/clock.h"
#include "material_manager.h"
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
		m_renderObject->SetCastShadows(false);

		m_scene.GetRootSceneNode().AddChild(*m_sceneNode);
		m_sceneNode->AttachObject(*m_renderObject);

		m_gridRendering = m_renderObject->objectRendering.connect(this, &WorldGrid::BeforeGridRendering);

		SetupGrid();
	}

	WorldGrid::~WorldGrid()
	{
		// Disconnect signal before destroying the render object
		m_gridRendering.disconnect();

		if (m_renderObject)
		{
			if (m_sceneNode) { m_sceneNode->DetachObject(*m_renderObject); }
			m_scene.DestroyManualRenderObject(*m_renderObject);
			m_renderObject = nullptr;
		}
		if (m_sceneNode)
		{
			m_scene.DestroySceneNode(*m_sceneNode);
			m_sceneNode = nullptr;
		}
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

		// A flat grid is the same geometry wherever it sits, so moving the node is the whole job.
		// Draped geometry has the surface baked into it and is only correct for the position it
		// was sampled at, so crossing into a new grid cell has to rebuild it. The snap is coarse
		// (one large-grid interval, a full terrain page by default), so this is rare.
		if (m_followTerrain && (!m_built || newPosition != m_builtAt))
		{
			m_invalidated = true;
		}
	}

	void WorldGrid::SetHeightProvider(HeightProvider provider)
	{
		m_heightProvider = std::move(provider);
		m_invalidated = true;
	}

	void WorldGrid::SetFollowTerrain(const bool follow)
	{
		if (m_followTerrain == follow)
		{
			return;
		}

		m_followTerrain = follow;
		m_invalidated = true;
	}

	void WorldGrid::SetTerrainSubdivisions(const uint8 subdivisions)
	{
		const uint8 clamped = subdivisions < 1 ? 1 : subdivisions;
		if (m_terrainSubdivisions == clamped)
		{
			return;
		}

		m_terrainSubdivisions = clamped;
		m_invalidated = true;
	}

	void WorldGrid::SetTerrainOffset(const float offset)
	{
		if (m_terrainOffset == offset)
		{
			return;
		}

		m_terrainOffset = offset;
		m_invalidated = true;
	}

	void WorldGrid::SetQueryFlags(const uint32 mask) const
	{
		ASSERT(m_renderObject);
		m_renderObject->SetQueryFlags(mask);
	}

	Vector3 WorldGrid::ResolvePoint(const Vector3& local)
	{
		if (!m_followTerrain || !m_heightProvider)
		{
			return local;
		}

		// The provider speaks world space; the geometry is built in the node's local space, so the
		// node's position has to be added on the way out and taken back off on the way in.
		const Vector3& origin = m_sceneNode->GetPosition();

		float surface = 0.0f;
		if (!m_heightProvider(origin.x + local.x, origin.z + local.z, surface))
		{
			// No surface here. Leaving the point at the grid's own height keeps the line
			// continuous into whatever comes next instead of dropping it to zero.
			m_hadMissingSamples = true;
			return local;
		}

		return Vector3(local.x, surface + m_terrainOffset - origin.y, local.z);
	}

	void WorldGrid::AddGridLine(ManualLineListOperation& operation, const Vector3& start, const Vector3& end,
		const uint32 segments, const uint32 color)
	{
		if (segments <= 1)
		{
			auto& line = operation.AddLine(ResolvePoint(start), ResolvePoint(end));
			line.SetColor(color);
			return;
		}

		const float step = 1.0f / static_cast<float>(segments);

		Vector3 previous = ResolvePoint(start);
		for (uint32 i = 1; i <= segments; ++i)
		{
			const float t = static_cast<float>(i) * step;
			const Vector3 current = ResolvePoint(start + (end - start) * t);

			auto& line = operation.AddLine(previous, current);
			line.SetColor(color);

			previous = current;
		}
	}

	void WorldGrid::SetupGrid()
	{
		m_renderObject->Clear();

		const float width = m_numCols * m_gridSize;
		const float height = m_numRows * m_gridSize;

		const Vector3 gridOrigin { -width / 2.0f, 0.0f, -height / 2.0f };

		const auto operation = m_renderObject->AddLineListOperation(MaterialManager::Get().Load("Models/Engine/WorldGrid.hmat"));

		// The ref exposes only operator-> and has to outlive the loops below: its destructor is
		// what finishes the operation, so it cannot be collapsed into this reference.
		ManualLineListOperation& lines = *operation.operator->();

		// Packed ARGB rather than frame_ui's Color. scene_graph is below the UI library, and the
		// only thing pulling the two together here was two constants -- which cost every consumer
		// of this file a link against frame_ui and everything it drags behind it (lua, freetype,
		// the asset system). These are the exact values Color(0.4f, 0.4f, 0.4f) and Color::White
		// packed to.
		constexpr uint32 darkColor = 0xFF666666;
		constexpr uint32 lightColor = 0xFFFFFFFF;

		// A flat grid needs one segment per line; draping needs enough of them to follow the
		// ground. Without a provider there is nothing to follow, so it stays at one either way.
		const bool draping = m_followTerrain && static_cast<bool>(m_heightProvider);
		const uint32 rowSegments = draping ? m_numCols * m_terrainSubdivisions : 1;
		const uint32 colSegments = draping ? m_numRows * m_terrainSubdivisions : 1;

		Vector3 start, end;
		for (auto i = 0; i < m_numRows; ++i)
		{
			start.z = m_gridSize * i;

			end.x = width;
			end.z = start.z;

			AddGridLine(lines, gridOrigin + start, gridOrigin + end, rowSegments,
				i % m_largeGrid != 0 ? darkColor : lightColor);
		}

		start.z = 0.0f;
		for (auto i = 0; i < m_numCols; ++i)
		{
			start.x = m_gridSize * i;
			end.x = start.x;
			end.z = height;

			AddGridLine(lines, gridOrigin + start, gridOrigin + end, colSegments,
				i % m_largeGrid != 0 ? darkColor : lightColor);
		}

		m_builtAt = m_sceneNode->GetPosition();
		m_builtTime = GetAsyncTimeMs();
		m_built = true;
	}

	void WorldGrid::Update(const Vector3& cameraPosition)
	{
		UpdatePosition(cameraPosition);

		// A build that could not resolve every position was reading terrain that had not finished
		// streaming, and nothing will announce its arrival. Retrying on a timer costs one rebuild
		// a second until the ground is all there, and then stops on its own.
		if (m_followTerrain && m_hadMissingSamples && !m_invalidated &&
			GetAsyncTimeMs() - m_builtTime >= MissingSampleRetryMs)
		{
			m_invalidated = true;
		}

		if (m_invalidated)
		{
			m_hadMissingSamples = false;
			SetupGrid();
			m_invalidated = false;
		}
	}

	bool WorldGrid::BeforeGridRendering(const MovableObject& movableObject, const Camera& camera)
	{
		Update(camera.GetDerivedPosition());

		return true;
	}
}
