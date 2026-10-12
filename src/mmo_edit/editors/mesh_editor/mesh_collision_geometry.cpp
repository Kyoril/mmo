// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "mesh_collision_geometry.h"

#include "assets/asset_registry.h"
#include "binary_io/reader.h"
#include "binary_io/stream_sink.h"
#include "binary_io/stream_source.h"
#include "binary_io/writer.h"
#include "log/default_log_levels.h"
#include "scene_graph/mesh_manager.h"
#include "scene_graph/render_operation.h"

namespace mmo
{
	namespace
	{
		void ReadVertexDataPositions(const VertexData& vertexData, std::vector<Vector3>& out_vertexPositions)
		{
			// Get shared vertex data
			const auto buffer = vertexData.vertexBufferBinding->GetBuffer(0);

			auto bufferData = static_cast<uint8*>(buffer->Map(LockOptions::ReadOnly));
			ASSERT(bufferData);

			for (size_t i = 0; i < vertexData.vertexCount; ++i)
			{
				const VertexElement* posElement = vertexData.vertexDeclaration->FindElementBySemantic(VertexElementSemantic::Position);
				float* position = nullptr;
				posElement->BaseVertexPointerToElement(bufferData, &position);

				const float x = *position++;
				const float y = *position++;
				const float z = *position++;
				out_vertexPositions.emplace_back(x, y, z);

				bufferData += vertexData.vertexDeclaration->GetVertexSize(0);
			}

			buffer->Unmap();
		}

		void ReadIndexData(const IndexData& indexData, uint32 offset, std::vector<uint32>& out_indices)
		{
			if (indexData.indexBuffer->GetIndexSize() == IndexBufferSize::Index_16)
			{
				const uint16* indices = reinterpret_cast<uint16*>(indexData.indexBuffer->Map(LockOptions::ReadOnly));
				ASSERT(indices);

				for (size_t i = 0; i < indexData.indexCount; ++i)
				{
					out_indices.push_back((*indices++) + offset);
				}
			}
			else
			{
				const uint32* indices = reinterpret_cast<uint32*>(indexData.indexBuffer->Map(LockOptions::ReadOnly));
				ASSERT(indices);

				for (size_t i = 0; i < indexData.indexCount; ++i)
				{
					out_indices.push_back((*indices++) + offset);
				}
			}

			indexData.indexBuffer->Unmap();
		}
	}

	RenderCollisionGeometry GatherRenderCollisionGeometry(Mesh& mesh, const std::vector<uint16>& includedSubMeshes)
	{
		// Skinned/skeletal meshes may keep their (bind pose) vertices in the mesh's shared vertex
		// data, referenced by every submesh; static meshes usually use per-submesh vertex data. The
		// shared pool is gathered once and reused by all submeshes that reference it.
		RenderCollisionGeometry out;

		uint32 sharedVertexOffset = 0;
		bool sharedVerticesGathered = false;

		for (const uint16 i : includedSubMeshes)
		{
			if (i >= mesh.GetSubMeshCount())
			{
				continue;
			}

			SubMesh& sub = mesh.GetSubMesh(i);
			if (!sub.indexData)
			{
				continue;
			}

			uint32 vertexOffset = 0;
			if (sub.useSharedVertices)
			{
				if (!mesh.sharedVertexData)
				{
					continue;
				}

				if (!sharedVerticesGathered)
				{
					sharedVertexOffset = static_cast<uint32>(out.vertices.size());
					ReadVertexDataPositions(*mesh.sharedVertexData, out.vertices);
					sharedVerticesGathered = true;
				}

				vertexOffset = sharedVertexOffset;
			}
			else
			{
				if (!sub.vertexData)
				{
					continue;
				}

				vertexOffset = static_cast<uint32>(out.vertices.size());
				ReadVertexDataPositions(*sub.vertexData, out.vertices);
			}

			// Remember the source submesh of every gathered face so surface types can be resolved
			// from the hit submesh's material.
			const size_t indexCountBefore = out.indices.size();
			ReadIndexData(*sub.indexData, vertexOffset, out.indices);
			out.faceSubMeshes.insert(out.faceSubMeshes.end(), (out.indices.size() - indexCountBefore) / 3, i);
		}

		return out;
	}

	std::vector<uint16> AllSubMeshesWithIndexData(Mesh& mesh)
	{
		std::vector<uint16> ids;
		for (uint16 i = 0; i < mesh.GetSubMeshCount(); ++i)
		{
			if (mesh.GetSubMesh(i).indexData)
			{
				ids.push_back(i);
			}
		}
		return ids;
	}

	CollisionBakeResult RebuildMeshCollision(Mesh& mesh, CollisionRecipe& recipe, RenderCollisionGeometry* out_renderGeometry, const bool buildTree)
	{
		SanitizeCollisionRecipe(recipe, mesh.GetSubMeshCount());

		RenderCollisionGeometry render;
		if (recipe.useRenderGeometry)
		{
			render = GatherRenderCollisionGeometry(mesh, recipe.includedSubMeshes);
		}

		CollisionBakeResult result = BakeCollision(render.vertices, render.indices, render.faceSubMeshes, recipe.shapes);

		if (buildTree)
		{
			AABBTree& tree = mesh.GetCollisionTree();
			tree.Clear();

			// Building from empty input would still allocate a node pool and serialize as bogus collision.
			if (!result.indices.empty())
			{
				tree.Build(result.vertices, result.indices, result.faceSubMeshes);
			}
		}

		if (out_renderGeometry)
		{
			*out_renderGeometry = std::move(render);
		}

		return result;
	}

	bool LoadMeshCollisionRecipe(const String& assetPath, CollisionRecipe& out_recipe, collision_recipe_read::Type& out_result)
	{
		const auto file = AssetRegistry::OpenFile(assetPath);
		if (!file)
		{
			return false;
		}

		io::StreamSource source{ *file };
		io::Reader reader{ source };
		out_result = ReadMeshCollisionRecipe(reader, out_recipe);
		return true;
	}

	bool SaveMeshWithRecipe(const MeshPtr& mesh, const String& assetPath, const CollisionRecipe* recipe)
	{
		const auto file = AssetRegistry::CreateNewFile(assetPath);
		if (!file)
		{
			ELOG("Failed to open mesh file " << assetPath << " for writing!");
			return false;
		}

		io::StreamSink sink{ *file };
		io::Writer writer{ sink };
		MeshSerializer serializer;
		serializer.Serialize(mesh, writer, mesh_version::Latest, recipe);
		return true;
	}

	bool RebakeMeshCollisionFile(const String& assetPath)
	{
		CollisionRecipe recipe;
		collision_recipe_read::Type result = collision_recipe_read::Absent;
		if (!LoadMeshCollisionRecipe(assetPath, recipe, result))
		{
			ELOG("Rebake collision: cannot open " << assetPath);
			return false;
		}

		if (result != collision_recipe_read::Read)
		{
			ELOG("Rebake collision: " << assetPath << (result == collision_recipe_read::Absent ? " has no collision recipe" : " has a corrupt collision recipe"));
			return false;
		}

		const MeshPtr mesh = MeshManager::Get().Load(assetPath);
		if (!mesh)
		{
			ELOG("Rebake collision: cannot load mesh " << assetPath);
			return false;
		}

		const CollisionBakeResult bake = RebuildMeshCollision(*mesh, recipe);
		if (!SaveMeshWithRecipe(mesh, assetPath, &recipe))
		{
			return false;
		}

		ILOG("Rebake collision: " << assetPath << " - " << bake.indices.size() / 3 << " faces, " << bake.cutFaces << " cut, " << recipe.shapes.size() << " shapes");
		return true;
	}
}
