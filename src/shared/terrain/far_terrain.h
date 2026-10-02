// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "graphics/material.h"
#include "graphics/material_instance.h"
#include "graphics/texture.h"
#include "graphics/vertex_index_data.h"
#include "math/aabb.h"
#include "math/vector3.h"
#include "scene_graph/movable_object.h"
#include "scene_graph/renderable.h"

#include "far_terrain_mesh.h"
#include "far_terrain_selection.h"

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace mmo
{
	class Scene;
	class SceneNode;

	namespace terrain
	{
		class Terrain;

		/// @brief The distant-terrain stand-in of one page: a single lit draw of its baked .tlod grid
		///	       textured with its baked colour. Never casts shadows, never collides.
		class FarPage final
			: public MovableObject
			, public Renderable
		{
		public:
			/// @brief Creates the page's GPU resources. Main thread only.
			/// @param name Unique movable object name.
			/// @param vertices far_mesh::VertexCount vertices built by far_mesh::BuildVertices.
			/// @param indexBuffer The shared far page index buffer (far_mesh::IndexCount 16 bit indices).
			/// @param material The page's material instance.
			FarPage(const String& name, const std::vector<far_mesh::Vertex>& vertices, IndexBufferPtr indexBuffer, std::shared_ptr<MaterialInstance> material);

			~FarPage() override;

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
			[[nodiscard]] MaterialPtr GetMaterial() const override { return m_material; }
			[[nodiscard]] bool GetCastsShadows() const override { return MovableObject::IsCastingShadows(); }
			// ~ End Renderable

		private:
			std::unique_ptr<VertexData> m_vertexData;
			VertexBufferPtr m_vertexBuffer;
			std::unique_ptr<IndexData> m_indexData;
			std::shared_ptr<MaterialInstance> m_material;
			AABB m_bounds;
			float m_boundingRadius = 0.0f;
		};

		/// @brief Streams and draws the baked distant-terrain stand-ins of the pages around the viewer.
		/// @details Pages are selected by far_selection::SelectPages. A page's .tlod is read and turned
		///	         into vertices on the streaming thread; the GPU resources are created on the main thread.
		///	         A stand-in is hidden while its full-resolution page is loaded, and destroyed when it
		///	         leaves the area. Pages without baked data are simply left out.
		class FarTerrain final : public NonCopyable
		{
		public:
			/// @brief Posts a function to another thread (or later onto the main thread).
			using PostFunction = std::function<void(std::function<void()>)>;

			/// @brief How far below the page border the skirt hangs, hiding cracks to full-resolution pages.
			static constexpr float SkirtDepth = 25.0f;

			/// @param terrain The streamed terrain whose pages are stood in for. Must outlive this object.
			/// @param material The lit far terrain material; it must expose an "Albedo" texture parameter.
			/// @param postToWorker Runs a function on the streaming thread.
			/// @param postToMain Runs a function on the main thread later.
			FarTerrain(Terrain& terrain, MaterialPtr material, PostFunction postToWorker, PostFunction postToMain);

			~FarTerrain() override;

		public:
			/// @brief Sets the radius in pages (0 disables the distant terrain).
			void SetRadius(uint32 radius);

			[[nodiscard]] uint32 GetRadius() const { return m_radius; }

			/// @brief Selects, streams and shows/hides the stand-ins around the viewer. Main thread, every frame.
			void Update(const Vector3& viewerPosition);

			/// @brief Destroys all stand-ins. Results of streaming still in flight are dropped.
			void Clear();

			/// @brief Number of stand-ins currently drawn.
			[[nodiscard]] uint32 GetVisiblePageCount() const { return m_visiblePageCount; }

			/// @brief Number of stand-ins streamed in and ready to draw, whether currently hidden or not.
			///        Zero for a map without baked data.
			[[nodiscard]] uint32 GetReadyPageCount() const { return m_readyPageCount; }

		private:
			enum class EntryState : uint8
			{
				Loading,
				Ready,
				Missing
			};

			struct Entry
			{
				EntryState state = EntryState::Loading;
				SceneNode* node = nullptr;
				std::unique_ptr<FarPage> page;
			};

			static uint32 Key(const uint32 x, const uint32 z) { return x | (z << 16); }

			void RequestPage(uint32 x, uint32 z);

			void OnPageStreamed(uint32 x, uint32 z, bool success, std::vector<far_mesh::Vertex> vertices);

			void DestroyEntry(Entry& entry);

		private:
			Terrain& m_terrain;
			MaterialPtr m_material;
			PostFunction m_postToWorker;
			PostFunction m_postToMain;
			IndexBufferPtr m_indexBuffer;

			uint32 m_radius = 0;
			uint32 m_visiblePageCount = 0;
			uint32 m_readyPageCount = 0;

			std::unordered_map<uint32, Entry> m_entries;
			std::vector<far_selection::PageCoord> m_selection;

			/// Streaming completions hold a weak reference and drop their result once this is gone.
			std::shared_ptr<char> m_lifetime;
		};
	}
}
