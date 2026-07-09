// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "terrain_batch.h"

#include "page.h"
#include "terrain.h"

#include "graphics/graphics_device.h"
#include "scene_graph/render_operation.h"
#include "scene_graph/render_queue.h"
#include "scene_graph/camera.h"
#include "scene_graph/scene_node.h"

namespace mmo
{
	namespace terrain
	{
		namespace
		{
			const String TerrainBatchMovableType = "TerrainBatch";
		}

		TerrainBatch::TerrainBatch(const String& name, Page& page, const uint32 quadX, const uint32 quadY, MaterialPtr baseMaterial, TexturePtr coverageTexture, std::vector<Tile*> tiles)
			: MovableObject(name)
			, m_page(page)
			, m_quadX(quadX)
			, m_quadY(quadY)
			, m_tiles(std::move(tiles))
		{
			ASSERT(!m_tiles.empty());
			ASSERT(baseMaterial);

			// Render in the same queue group as individual terrain tiles.
			SetRenderQueueGroup(TerrainGeometry);

			m_materialInstance = std::make_shared<MaterialInstance>(name, std::move(baseMaterial));
			if (coverageTexture)
			{
				m_materialInstance->SetTextureParameter("Splatting", coverageTexture);
			}

			m_lastStitchKeys.assign(m_tiles.size(), 0xffffffff);

			// Terrain does not cast shadows (mirrors Tile). This also keeps the member tiles' LOD
			// state from being driven by shadow-pass cameras.
			SetCastShadows(false);

			BuildVertexBuffer();

			// Build an initial index buffer (all members at LOD 0, no stitching — the same state
			// freshly created tiles start in). The first PreRender re-evaluates the real LODs.
			RebuildIndexBuffer();
			m_indexDirty = false;
		}

		TerrainBatch::~TerrainBatch() = default;

		const String& TerrainBatch::GetMovableType() const
		{
			return TerrainBatchMovableType;
		}

		void TerrainBatch::VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables)
		{
			visitor.Visit(*this, 0, false);
		}

		void TerrainBatch::PopulateRenderQueue(RenderQueue& queue)
		{
			if (!m_hasGeometry)
			{
				return;
			}

			queue.AddRenderable(*this, m_renderQueueId);
		}

		void TerrainBatch::PrepareRenderOperation(RenderOperation& operation)
		{
			operation.vertexData = m_vertexData.get();
			operation.indexData = m_indexData.get();
			operation.topology = TopologyType::TriangleList;
			operation.material = m_materialInstance;
		}

		const Matrix4& TerrainBatch::GetWorldTransform() const
		{
			return GetParentNodeFullTransform();
		}

		float TerrainBatch::GetSquaredViewDepth(const Camera& camera) const
		{
			const Vector3 center = (GetParentNodeFullTransform() * GetBoundingBox()).GetCenter();
			return (camera.GetDerivedPosition() - center).GetSquaredLength();
		}

		bool TerrainBatch::PreRender(Scene& scene, GraphicsDevice& graphicsDevice, Camera& camera)
		{
			const bool lodEnabled = m_page.GetTerrain().IsLodEnabled();

			// Drive the members' LOD state (excluded tiles no longer render themselves, so their
			// PreRender never runs). UpdateLOD has cheap early-outs for a stationary camera.
			for (size_t i = 0; i < m_tiles.size(); ++i)
			{
				uint32 desiredKey = 0;
				if (lodEnabled)
				{
					m_tiles[i]->UpdateLOD(camera);
					desiredKey = m_tiles[i]->GetCurrentStitchKey();
				}

				if (desiredKey != m_lastStitchKeys[i])
				{
					m_indexDirty = true;
				}
			}

			if (m_indexDirty)
			{
				// Note: RenderSingleObject captures the operation's index-data pointer BEFORE
				// PreRender runs, so the previous index data must stay alive through this draw —
				// RebuildIndexBuffer retires it instead of destroying it (see m_retiredIndexData).
				RebuildIndexBuffer();
				m_indexDirty = false;
			}

			return Renderable::PreRender(scene, graphicsDevice, camera);
		}

		void TerrainBatch::BuildVertexBuffer()
		{
			m_vertexData = std::make_unique<VertexData>();
			m_vertexData->vertexStart = 0;
			m_vertexData->vertexCount = static_cast<uint32>(m_tiles.size()) * constants::VerticesPerTile;

			VertexDeclaration* decl = m_vertexData->vertexDeclaration;
			VertexBufferBinding* bind = m_vertexData->vertexBufferBinding;

			uint32 offset = 0;
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Position).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::ColorArgb, VertexElementSemantic::Diffuse).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Normal).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Binormal).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Tangent).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float2, VertexElementSemantic::TextureCoordinate).GetSize();

			std::vector<TileVertex> vertices(m_vertexData->vertexCount);

			// UV remapping into the quadrant coverage texture. The per-tile mapping puts the tile's
			// UV range onto its own (PixelsPerTile) window; the quadrant texture lays those windows
			// out with shared border pixels ((PixelsPerTile - 1) texels per tile). A tile-local UV t
			// of tile k therefore maps to texel (k + t) * (PixelsPerTile - 1), sampled at
			// (texel + 0.5) / PixelsPerBatchSide.
			constexpr float texelsPerTile = static_cast<float>(constants::PixelsPerTile - 1);
			constexpr float invBatchPixels = 1.0f / static_cast<float>(PixelsPerBatchSide);
			constexpr float uvScale = texelsPerTile * invBatchPixels;

			m_bounds = AABB();
			bool first = true;

			for (size_t i = 0; i < m_tiles.size(); ++i)
			{
				Tile& tile = *m_tiles[i];

				const auto tileX = static_cast<size_t>(tile.GetX());
				const auto tileY = static_cast<size_t>(tile.GetY());

				// Tile coordinates local to this quadrant.
				const float quadLocalX = static_cast<float>(tileX - m_quadX * TilesPerBatchSide);
				const float quadLocalY = static_cast<float>(tileY - m_quadY * TilesPerBatchSide);

				// Vertex u maps the tile's Z axis (tileY), v maps the X axis (tileX) — mirroring the
				// per-tile vertex generation and coverage window layout.
				const float uBias = (quadLocalY * texelsPerTile + 0.5f) * invBatchPixels;
				const float vBias = (quadLocalX * texelsPerTile + 0.5f) * invBatchPixels;

				float minHeight = 0.0f, maxHeight = 0.0f;
				Tile::FillTileVertices(m_page, tileX, tileY, uvScale, uBias, vBias, vertices.data() + i * constants::VerticesPerTile, minHeight, maxHeight);

				// Grow the page-local batch bounds by the member tile's bounds.
				if (first)
				{
					m_bounds = tile.GetBoundingBox();
					first = false;
				}
				else
				{
					m_bounds.Combine(tile.GetBoundingBox());
				}
			}

			m_boundingRadius = m_bounds.GetExtents().GetLength();

			m_vertexBuffer = GraphicsDevice::Get().CreateVertexBuffer(m_vertexData->vertexCount, decl->GetVertexSize(0), BufferUsage::StaticWriteOnly, vertices.data());
			bind->SetBinding(0, m_vertexBuffer);
		}

		void TerrainBatch::RebuildIndexBuffer()
		{
			const bool lodEnabled = m_page.GetTerrain().IsLodEnabled();

			m_indexScratch.clear();

			for (size_t i = 0; i < m_tiles.size(); ++i)
			{
				Tile& tile = *m_tiles[i];

				const uint32 key = lodEnabled ? tile.GetCurrentStitchKey() : 0;
				m_lastStitchKeys[i] = key;

				if (!tile.HasRenderableGeometry())
				{
					continue;
				}

				m_tileScratch.clear();
				Tile::GenerateTileIndices(
					key & 0xF,
					(key >> 4) & 0xF,
					(key >> 8) & 0xF,
					(key >> 12) & 0xF,
					(key >> 16) & 0xF,
					m_page.GetTileHoleMap(static_cast<uint32>(tile.GetX()), static_cast<uint32>(tile.GetY())),
					m_tileScratch);

				const auto baseVertex = static_cast<uint16>(i * constants::VerticesPerTile);
				for (const uint16 index : m_tileScratch)
				{
					m_indexScratch.push_back(index + baseVertex);
				}
			}

			// Retire the previous index data instead of destroying it: the in-flight render
			// operation may still reference it (PrepareRenderOperation runs before PreRender).
			// The retired generation is released on the next rebuild, long after the draw.
			m_retiredIndexData = std::move(m_indexData);

			m_hasGeometry = !m_indexScratch.empty();
			if (!m_hasGeometry)
			{
				return;
			}

			// Recreate the index buffer with the new contents. Rebuilds only happen when a member
			// tile changes its LOD/stitch configuration, which is rare relative to frame rate.
			m_indexData = std::make_unique<IndexData>();
			m_indexData->indexBuffer = GraphicsDevice::Get().CreateIndexBuffer(
				static_cast<uint32>(m_indexScratch.size()),
				IndexBufferSize::Index_16,
				BufferUsage::StaticWriteOnly,
				m_indexScratch.data());
			m_indexData->indexCount = static_cast<uint32>(m_indexScratch.size());
			m_indexData->indexStart = 0;
		}
	}
}
