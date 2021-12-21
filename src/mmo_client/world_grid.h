#pragma once

#include "base/typedefs.h"
#include "graphics/graphics_device.h"
#include "math/vector3.h"

namespace mmo
{
	/// A class for rendering a grid in the world.
	class WorldGrid final
	{
	public:
		explicit WorldGrid(GraphicsDevice& device);
		Vector3 SnapToGrid(const Vector3& position);

	public:
		void Update();

		void UpdatePosition(const Vector3& cameraPosition);

		void Render() const;

	private:
		void SetupGrid();
		
	private:
		GraphicsDevice& m_graphicsDevice;
		uint8 m_numRows { 48 };
		uint8 m_numCols { 48 };
		uint8 m_largeGrid { 16 };
		float m_gridSize { 1.0f };
		Vector3 m_origin;

		VertexBufferPtr m_vertexBuffer;
		IndexBufferPtr m_indexBuffer;
	};
}
