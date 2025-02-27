#pragma once

#include "graphics/vertex_index_data.h"
#include "scene_graph/movable_object.h"
#include "scene_graph/renderable.h"

#include <memory>

#include "coverage_map.h"
#include "graphics/material_instance.h"
#include "scene_graph/mesh.h"

namespace mmo
{
	class Entity;
}

namespace mmo
{
	namespace terrain
	{
		class Terrain;
		class Page;

		class Tile final
			: public MovableObject
			, public Renderable
		{
		public:

			explicit Tile(const String& name, Page& page, size_t startX, size_t startZ);
			~Tile() override;

		public:
			int32 GetX() const { return static_cast<int32>(m_tileX); }

			int32 GetY() const { return static_cast<int32>(m_tileY); }

			void PrepareRenderOperation(RenderOperation& operation) override;

			[[nodiscard]] const Matrix4& GetWorldTransform() const override;

			[[nodiscard]] float GetSquaredViewDepth(const Camera& camera) const override;

			[[nodiscard]] MaterialPtr GetMaterial() const override;

			[[nodiscard]] MaterialPtr GetBaseMaterial() const;

			void SetMaterial(MaterialPtr material);

			[[nodiscard]] const String& GetMovableType() const override;

			[[nodiscard]] const AABB& GetBoundingBox() const override;

			[[nodiscard]] float GetBoundingRadius() const override;

			void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables) override;

			void PopulateRenderQueue(RenderQueue& queue) override;

			Page& GetPage() const { return m_page; }

			Terrain& GetTerrain() const;

			void UpdateTerrain(size_t startx, size_t startz, size_t endx, size_t endz);

			void UpdateCoverageMap();

		private:
			void CreateVertexData(size_t startX, size_t startZ);

			void CreateIndexData(uint32 lod, uint32 neighborState);

			uint16 GetIndex(size_t x, size_t y) const;

			enum class Direction : uint8
			{
				North = 0,
				East = 1,
				South = 2,
				West = 3
			};

			uint32 StitchEdge(Direction direction, uint32 hiLOD, uint32 loLOD, bool omitFirstTri, bool omitLastTri, uint16** ppIdx);

		private:
			Page& m_page;
			AABB m_bounds;
			size_t m_tileX, m_tileY;
			size_t m_startX, m_startZ;
			float m_boundingRadius;
			Vector3 m_center;
			std::unique_ptr<VertexData> m_vertexData;
			std::unique_ptr<IndexData> m_indexData;
			VertexBufferPtr m_mainBuffer;
			std::shared_ptr<MaterialInstance> m_materialInstance;
			TexturePtr m_coverageTexture;
		};
	}
}
