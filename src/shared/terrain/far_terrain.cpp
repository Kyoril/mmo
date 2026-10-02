// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "far_terrain.h"

#include "page.h"
#include "terrain.h"
#include "tile.h"

#include "assets/asset_registry.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "graphics/graphics_device.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/render_operation.h"
#include "scene_graph/render_queue.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "terrain_io/page_lod.h"

#include <limits>

namespace mmo
{
	namespace terrain
	{
		namespace
		{
			const String FarPageMovableType = "FarTerrainPage";
		}

		FarPage::FarPage(const String& name, const std::vector<far_mesh::Vertex>& vertices, IndexBufferPtr indexBuffer, std::shared_ptr<MaterialInstance> material)
			: MovableObject(name)
			, m_material(std::move(material))
		{
			ASSERT(vertices.size() == far_mesh::VertexCount);

			// Drawn with the terrain, lit in the G-buffer pass. Far beyond the shadow cascades, so it
			// never casts; it is a visual stand-in only, so scene queries (picking, collision) skip it.
			SetRenderQueueGroup(TerrainGeometry);
			SetCastShadows(false);
			SetQueryFlags(0);

			m_vertexData = std::make_unique<VertexData>();
			m_vertexData->vertexStart = 0;
			m_vertexData->vertexCount = far_mesh::VertexCount;

			// Same layout as terrain tiles: the generated vertex shaders expect position, colour,
			// normal, binormal, tangent and the texture coordinates.
			VertexDeclaration* decl = m_vertexData->vertexDeclaration;
			uint32 offset = 0;
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Position).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::ColorArgb, VertexElementSemantic::Diffuse).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Normal).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Binormal).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float3, VertexElementSemantic::Tangent).GetSize();
			offset += decl->AddElement(0, offset, VertexElementType::Float2, VertexElementSemantic::TextureCoordinate).GetSize();

			std::vector<TileVertex> gpuVertices(vertices.size());

			Vector3 minimum(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
			Vector3 maximum(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

			for (size_t i = 0; i < vertices.size(); ++i)
			{
				const far_mesh::Vertex& source = vertices[i];
				TileVertex& target = gpuVertices[i];
				target.position = source.position;
				target.color = 0xffffffff;
				target.normal = source.normal;
				target.tangent = Vector3::UnitX;
				target.binormal = Vector3::UnitZ;
				target.u = source.u;
				target.v = source.v;

				minimum = TakeMinimum(minimum, source.position);
				maximum = TakeMaximum(maximum, source.position);
			}

			m_bounds = AABB(minimum, maximum);
			m_boundingRadius = m_bounds.GetExtents().GetLength();

			m_vertexBuffer = GraphicsDevice::Get().CreateVertexBuffer(m_vertexData->vertexCount, decl->GetVertexSize(0), BufferUsage::StaticWriteOnly, gpuVertices.data());
			m_vertexData->vertexBufferBinding->SetBinding(0, m_vertexBuffer);

			m_indexData = std::make_unique<IndexData>();
			m_indexData->indexBuffer = std::move(indexBuffer);
			m_indexData->indexCount = far_mesh::IndexCount;
			m_indexData->indexStart = 0;
		}

		FarPage::~FarPage() = default;

		const String& FarPage::GetMovableType() const
		{
			return FarPageMovableType;
		}

		void FarPage::VisitRenderables(Renderable::Visitor& visitor, bool debugRenderables)
		{
			visitor.Visit(*this, 0, false);
		}

		void FarPage::PopulateRenderQueue(RenderQueue& queue)
		{
			queue.AddRenderable(*this, m_renderQueueId);
		}

		void FarPage::PrepareRenderOperation(RenderOperation& operation)
		{
			operation.vertexData = m_vertexData.get();
			operation.indexData = m_indexData.get();
			operation.topology = TopologyType::TriangleList;
			operation.material = m_material;
		}

		const Matrix4& FarPage::GetWorldTransform() const
		{
			return GetParentNodeFullTransform();
		}

		float FarPage::GetSquaredViewDepth(const Camera& camera) const
		{
			const Vector3 center = (GetParentNodeFullTransform() * GetBoundingBox()).GetCenter();
			return (camera.GetDerivedPosition() - center).GetSquaredLength();
		}

		FarTerrain::FarTerrain(Terrain& terrain, MaterialPtr material, PostFunction postToWorker, PostFunction postToMain)
			: m_terrain(terrain)
			, m_material(std::move(material))
			, m_postToWorker(std::move(postToWorker))
			, m_postToMain(std::move(postToMain))
			, m_lifetime(std::make_shared<char>(0))
		{
			ASSERT(m_material);

			std::vector<uint16> indices;
			far_mesh::BuildIndices(indices);
			m_indexBuffer = GraphicsDevice::Get().CreateIndexBuffer(indices.size(), IndexBufferSize::Index_16, BufferUsage::StaticWriteOnly, indices.data());
		}

		FarTerrain::~FarTerrain()
		{
			Clear();
		}

		void FarTerrain::SetRadius(const uint32 radius)
		{
			m_radius = radius;
			if (m_radius == 0)
			{
				Clear();
			}
		}

		void FarTerrain::Update(const Vector3& viewerPosition)
		{
			m_visiblePageCount = 0;
			if (m_radius == 0)
			{
				return;
			}

			far_selection::SelectPages(viewerPosition.x, viewerPosition.z, m_radius, m_selection);

			// Drop stand-ins that left the area. Pages still streaming are dropped too: their
			// completion finds no entry and discards its result.
			for (auto it = m_entries.begin(); it != m_entries.end();)
			{
				const uint32 x = it->first & 0xffff;
				const uint32 z = it->first >> 16;
				const bool wanted = std::find(m_selection.begin(), m_selection.end(), far_selection::PageCoord{ x, z }) != m_selection.end();
				if (wanted)
				{
					++it;
					continue;
				}

				DestroyEntry(it->second);
				it = m_entries.erase(it);
			}

			for (const auto& coord : m_selection)
			{
				const auto it = m_entries.find(Key(coord.x, coord.z));
				if (it == m_entries.end())
				{
					RequestPage(coord.x, coord.z);
					continue;
				}

				Entry& entry = it->second;
				if (entry.state != EntryState::Ready)
				{
					continue;
				}

				// The full-resolution page replaces the stand-in once it is completely loaded. Until then
				// (tiles are excluded from rendering while a page loads) the stand-in keeps the area covered.
				const Page* page = m_terrain.GetPage(coord.x, coord.z);
				const bool replaced = page != nullptr && page->IsLoaded();
				entry.page->SetVisible(!replaced);
				if (!replaced)
				{
					++m_visiblePageCount;
				}
			}
		}

		void FarTerrain::Clear()
		{
			for (auto& [key, entry] : m_entries)
			{
				DestroyEntry(entry);
			}
			m_entries.clear();

			// Invalidate every streaming completion still in flight.
			m_lifetime = std::make_shared<char>(0);
		}

		void FarTerrain::RequestPage(const uint32 x, const uint32 z)
		{
			Entry& entry = m_entries[Key(x, z)];
			entry.state = EntryState::Loading;

			const String filename = m_terrain.GetPageLodFilename(x, z);
			std::weak_ptr<char> lifetime = m_lifetime;

			// The worker job must not touch members: this object may be gone by the time it runs.
			m_postToWorker([this, lifetime, postToMain = m_postToMain, filename, x, z]()
			{
				std::vector<far_mesh::Vertex> vertices;
				bool success = false;

				if (AssetRegistry::HasFile(filename))
				{
					if (const std::unique_ptr<std::istream> file = AssetRegistry::OpenFile(filename))
					{
						io::StreamSource source(*file);
						io::Reader reader(source);

						terrain_io::PageLodData lod;
						if (terrain_io::LoadPageLod(reader, lod))
						{
							far_mesh::BuildVertices(lod.heights.data(), lod.normals.data(), SkirtDepth, vertices);
							success = true;
						}
						else
						{
							WLOG("Failed to read distant terrain data " << filename);
						}
					}
				}

				postToMain([this, lifetime, x, z, success, vertices = std::move(vertices)]() mutable
				{
					if (lifetime.expired())
					{
						return;
					}

					OnPageStreamed(x, z, success, std::move(vertices));
				});
			});
		}

		void FarTerrain::OnPageStreamed(const uint32 x, const uint32 z, const bool success, std::vector<far_mesh::Vertex> vertices)
		{
			const auto it = m_entries.find(Key(x, z));
			if (it == m_entries.end() || it->second.state != EntryState::Loading)
			{
				// Left the area (and possibly re-entered with a newer request) while streaming.
				return;
			}

			Entry& entry = it->second;
			if (!success)
			{
				entry.state = EntryState::Missing;
				return;
			}

			const String name = "FarPage_" + std::to_string(x) + "_" + std::to_string(z);

			auto material = std::make_shared<MaterialInstance>(name, m_material);
			const String textureName = m_terrain.GetPageLodTextureFilename(x, z);
			if (AssetRegistry::HasFile(textureName))
			{
				if (const TexturePtr texture = TextureManager::Get().CreateOrRetrieve(textureName))
				{
					texture->SetTextureAddressMode(TextureAddressMode::Clamp);
					material->SetTextureParameter("Albedo", texture);
				}
			}

			entry.page = std::make_unique<FarPage>(name, vertices, m_indexBuffer, std::move(material));

			const float pageSize = static_cast<float>(constants::PageSize);
			const Vector3 origin(
				(static_cast<float>(x) - static_cast<float>(constants::MaxPages / 2)) * pageSize,
				0.0f,
				(static_cast<float>(z) - static_cast<float>(constants::MaxPages / 2)) * pageSize);

			entry.node = m_terrain.GetNode()->CreateChildSceneNode(origin);
			entry.node->AttachObject(*entry.page);

			// Hidden until the next Update decides whether the full-resolution page already covers it.
			entry.page->SetVisible(false);
			entry.state = EntryState::Ready;
		}

		void FarTerrain::DestroyEntry(Entry& entry)
		{
			if (entry.page)
			{
				entry.page->DetachFromParent();
				entry.page.reset();
			}

			if (entry.node)
			{
				m_terrain.GetScene().DestroySceneNode(*entry.node);
				entry.node = nullptr;
			}
		}
	}
}
