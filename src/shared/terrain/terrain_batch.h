// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "graphics/vertex_index_data.h"
#include "graphics/material_instance.h"
#include "scene_graph/movable_object.h"
#include "scene_graph/renderable.h"

#include "constants.h"
#include "tile.h"

#include <memory>
#include <vector>

namespace mmo
{
	namespace terrain
	{
		class Page;

		/// @brief Renders a quarter-page group of terrain tiles (8x8 tiles sharing one base
		///        material) with a single draw call instead of one draw call per tile.
		///
		/// The member tiles stay in the scene for collision, picking and LOD bookkeeping, but are
		/// excluded from the render queue (see Tile::SetExcludedFromRendering). This batch owns a
		/// merged vertex buffer (member vertices with UVs remapped into quadrant splat-map space)
		/// and a concatenated index buffer assembled from each member's current LOD/stitching
		/// triangle list. The index buffer is only rebuilt when a member tile's stitch key changes,
		/// so a stationary camera never triggers any per-frame work beyond the LOD early-outs.
		class TerrainBatch final
			: public MovableObject
			, public Renderable
		{
		public:
			/// @brief Number of tiles along one side of a batch (quarter page).
			static constexpr uint32 TilesPerBatchSide = constants::TilesPerPage / 2;

			/// @brief Coverage-map pixels along one side of a batch (mirrors the per-tile window
			///        layout: overlapping windows share their border pixel).
			static constexpr uint32 PixelsPerBatchSide = TilesPerBatchSide * (constants::PixelsPerTile - 1) + 1;

			/// @brief Constructs a batch for the given member tiles.
			/// @param name Unique object name.
			/// @param page The owning page.
			/// @param quadX/quadY Quadrant coordinates within the page (0 or 1).
			/// @param baseMaterial The shared base material of all member tiles.
			/// @param coverageTexture Quadrant-wide splat/coverage texture.
			/// @param tiles The member tiles (row-major within the quadrant).
			explicit TerrainBatch(const String& name, Page& page, uint32 quadX, uint32 quadY, MaterialPtr baseMaterial, TexturePtr coverageTexture, std::vector<Tile*> tiles);

			~TerrainBatch() override;

		public:
			// ~ Begin MovableObject
			[[nodiscard]] const String& GetMovableType() const override;
			[[nodiscard]] const AABB& GetBoundingBox() const override { return m_bounds; }
			[[nodiscard]] float GetBoundingRadius() const override { return m_boundingRadius; }
			void VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables) override;
			void PopulateRenderQueue(RenderQueue& queue) override;
			// ~ End MovableObject

			// ~ Begin Renderable
			void PrepareRenderOperation(RenderOperation& operation) override;
			[[nodiscard]] const Matrix4& GetWorldTransform() const override;
			[[nodiscard]] float GetSquaredViewDepth(const Camera& camera) const override;
			[[nodiscard]] MaterialPtr GetMaterial() const override { return m_materialInstance; }

			/// @copydoc Renderable::GetCastsShadows
			/// @remark Both the MovableObject and the Renderable, so forward to the single flag.
			[[nodiscard]] bool GetCastsShadows() const override { return MovableObject::IsCastingShadows(); }
			bool PreRender(Scene& scene, GraphicsDevice& graphicsDevice, Camera& camera) override;
			// ~ End Renderable

			/// @brief Quadrant coordinate of this batch within its page (0 or 1).
			[[nodiscard]] uint32 GetQuadX() const { return m_quadX; }

			/// @copydoc GetQuadX
			[[nodiscard]] uint32 GetQuadY() const { return m_quadY; }

			/// @brief Rebuilds the merged geometry after the member tiles' heights changed.
			///
			/// The merged vertex buffer is a snapshot of the members taken when the batch was
			/// built, so a deform that goes through Page::UpdateTiles leaves both the geometry and
			/// the bounds behind. Only the editor deforms loaded terrain, and it does so outside
			/// the render pass, but the previous vertex data is retired rather than destroyed for
			/// the same reason the index data is â€” a render operation may still point at it.
			void NotifyTilesChanged();


		private:
			/// @brief Builds the merged vertex buffer from the member tiles, remapping UVs into
			///        quadrant splat-map space, and computes the batch bounds.
			void BuildVertexBuffer();

			/// @brief Rebuilds the concatenated index buffer from the members' current LOD/stitch
			///        configurations.
			void RebuildIndexBuffer();

		private:
			Page& m_page;
			uint32 m_quadX;
			uint32 m_quadY;

			/// Member tiles in quadrant-row-major order. Slot i occupies vertex range
			/// [i * VerticesPerTile, (i+1) * VerticesPerTile) of the merged vertex buffer.
			std::vector<Tile*> m_tiles;

			/// Stitch key of each member the last time the index buffer was built.
			std::vector<uint32> m_lastStitchKeys;

			std::unique_ptr<VertexData> m_vertexData;
			VertexBufferPtr m_vertexBuffer;

			/// Previous vertex data, kept alive for one rebuild generation for the same reason as
			/// m_retiredIndexData below.
			std::unique_ptr<VertexData> m_retiredVertexData;

			std::unique_ptr<IndexData> m_indexData;


			/// Previous index data, kept alive for one rebuild generation: the render operation
			/// captures the index-data pointer before PreRender (where rebuilds happen) runs.
			std::unique_ptr<IndexData> m_retiredIndexData;

			std::shared_ptr<MaterialInstance> m_materialInstance;

			AABB m_bounds;
			float m_boundingRadius = 0.0f;

			bool m_indexDirty = true;
			bool m_hasGeometry = false;

			/// Scratch buffers reused across index rebuilds (capacity persists).
			std::vector<uint16> m_indexScratch;
			std::vector<uint16> m_tileScratch;
		};
	}
}
