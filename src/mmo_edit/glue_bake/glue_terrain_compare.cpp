// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "glue_terrain_compare.h"

#include "glue_terrain_baker.h"

#include "editors/world_editor/page_bake_utils.h"
#include "editors/world_editor/world_editor_instance.h"

#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "deferred_shading/deferred_renderer.h"
#include "graphics/g_buffer.h"
#include "graphics/graphics_device.h"
#include "graphics/material_instance.h"
#include "graphics/render_texture.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/entity.h"
#include "scene_graph/material_instance_serializer.h"
#include "scene_graph/material_manager.h"
#include "scene_graph/mesh_serializer.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "terrain/page.h"
#include "terrain/terrain.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>

namespace mmo
{
	namespace
	{
		constexpr uint32 ImageWidth = 960;
		constexpr uint32 ImageHeight = 540;

		struct Image
		{
			/// RGB, row by row.
			std::vector<uint8> rgb;

			/// Whether geometry covers the pixel (G-buffer albedo alpha).
			std::vector<bool> covered;
		};

		/// Reads an RGBA16F or RGBA8 render target into RGB8 (values clamped, no gamma applied).
		std::vector<uint8> ReadRgb(Texture& texture, const uint32 pixelCount)
		{
			std::vector<uint8> rgb(static_cast<size_t>(pixelCount) * 3);
			const uint32 size = texture.GetPixelDataSize();
			if (size == pixelCount * 8)
			{
				std::vector<uint16> halfs(static_cast<size_t>(pixelCount) * 4);
				texture.CopyPixelDataTo(reinterpret_cast<uint8*>(halfs.data()));
				for (uint32 i = 0; i < pixelCount; ++i)
				{
					for (uint32 c = 0; c < 3; ++c)
					{
						const float value = std::clamp(page_bake::HalfToFloat(halfs[i * 4 + c]), 0.0f, 1.0f);
						rgb[i * 3 + c] = static_cast<uint8>(std::lround(value * 255.0f));
					}
				}
			}
			else if (size == pixelCount * 4)
			{
				std::vector<uint8> rgba(static_cast<size_t>(pixelCount) * 4);
				texture.CopyPixelDataTo(rgba.data());
				for (uint32 i = 0; i < pixelCount; ++i)
				{
					for (uint32 c = 0; c < 3; ++c)
					{
						rgb[i * 3 + c] = rgba[i * 4 + c];
					}
				}
			}
			else
			{
				ELOG("Glue terrain compare: unexpected render target size " << size);
			}

			return rgb;
		}

		/// G-buffer albedo as sRGB RGB8, plus coverage.
		Image ReadAlbedo(GBuffer& gBuffer, const uint32 pixelCount)
		{
			Image image;
			image.rgb.resize(static_cast<size_t>(pixelCount) * 3);
			image.covered.resize(pixelCount);

			RenderTexture& target = gBuffer.GetAlbedoRT();
			if (target.GetPixelDataSize() != pixelCount * 8)
			{
				ELOG("Glue terrain compare: unexpected G-buffer format");
				return image;
			}

			std::vector<uint16> halfs(static_cast<size_t>(pixelCount) * 4);
			target.CopyPixelDataTo(reinterpret_cast<uint8*>(halfs.data()));
			for (uint32 i = 0; i < pixelCount; ++i)
			{
				for (uint32 c = 0; c < 3; ++c)
				{
					const float linear = std::clamp(page_bake::HalfToFloat(halfs[i * 4 + c]), 0.0f, 1.0f);
					image.rgb[i * 3 + c] = static_cast<uint8>(std::lround(std::pow(linear, 1.0f / 2.2f) * 255.0f));
				}
				image.covered[i] = page_bake::HalfToFloat(halfs[i * 4 + 3]) > 0.5f;
			}

			return image;
		}

		bool WritePpm(const std::filesystem::path& path, const std::vector<uint8>& rgb)
		{
			std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
			if (!file)
			{
				return false;
			}

			file << "P6\n" << ImageWidth << " " << ImageHeight << "\n255\n";
			file.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
			return static_cast<bool>(file);
		}

		MeshPtr LoadMeshFile(const String& path)
		{
			std::ifstream file(path, std::ios::in | std::ios::binary);
			if (!file)
			{
				return nullptr;
			}

			io::StreamSource source{ file };
			io::Reader reader{ source };
			auto mesh = std::make_shared<Mesh>(path);
			MeshDeserializer deserializer{ *mesh };
			if (!deserializer.Read(reader))
			{
				return nullptr;
			}

			return mesh;
		}

		std::shared_ptr<MaterialInstance> LoadMaterialInstanceFile(const String& path)
		{
			std::ifstream file(path, std::ios::in | std::ios::binary);
			if (!file)
			{
				return nullptr;
			}

			// Like MaterialManager::Load does for .hmi assets: the parent comes from the file.
			auto material = std::make_shared<MaterialInstance>(path, std::make_shared<Material>("__DEFAULT__"));
			io::StreamSource source{ file };
			io::Reader reader{ source };
			MaterialInstanceDeserializer deserializer{ *material };
			if (!deserializer.Read(reader))
			{
				return nullptr;
			}

			material->Update();
			return material;
		}

		TexturePtr LoadTextureFile(const String& path)
		{
			std::unique_ptr<std::istream> file = std::make_unique<std::ifstream>(path, std::ios::in | std::ios::binary);
			if (!*file)
			{
				return nullptr;
			}

			TexturePtr texture = GraphicsDevice::Get().CreateTexture();
			texture->Load(file);
			return texture;
		}

		/// Renders twice (tiles build their index buffers in their first PreRender) and reads back.
		void Render(DeferredRenderer& renderer, Scene& scene, Camera& camera, std::vector<uint8>& lit, Image& albedo)
		{
			auto& gx = GraphicsDevice::Get();
			for (int pass = 0; pass < 2; ++pass)
			{
				gx.Reset();
				gx.SetViewport(0, 0, ImageWidth, ImageHeight, 0.0f, 1.0f);
				renderer.Render(scene, camera);
			}

			constexpr uint32 pixelCount = ImageWidth * ImageHeight;
			lit = ReadRgb(*renderer.GetFinalRenderTarget(), pixelCount);
			albedo = ReadAlbedo(renderer.GetGBuffer(), pixelCount);
		}
	}

	bool CompareGlueTerrainBake(WorldEditorInstance& world, const std::vector<PagePosition>& pages, const std::vector<GlueBakeTerrainTile>& tiles,
		const std::vector<GlueBakeMesh>& water, const std::vector<GlueTerrainCompareView>& views, const String& artDir)
	{
		terrain::Terrain* terrain = world.GetTerrain();
		if (!terrain || views.empty())
		{
			return false;
		}

		std::error_code error;
		std::filesystem::create_directories(artDir, error);

		Scene& scene = world.GetScene();
		auto& gx = GraphicsDevice::Get();
		gx.CaptureState();

		const bool lodWasEnabled = terrain->IsLodEnabled();
		terrain->SetLodEnabled(false);
		const bool foliageWasVisible = world.SetFoliageVisible(false);

		bool success = true;
		{
			auto renderer = std::make_unique<DeferredRenderer>(gx, scene, ImageWidth, ImageHeight);
			Camera* camera = scene.CreateCamera("GlueTerrainCompareCamera");
			camera->SetAspectRatio(static_cast<float>(ImageWidth) / static_cast<float>(ImageHeight));
			camera->SetFOVy(Degree(45.0f));
			camera->SetNearClipDistance(0.1f);
			camera->SetFarClipDistance(1000.0f);
			SceneNode* cameraNode = scene.GetRootSceneNode().CreateChildSceneNode("GlueTerrainCompareCameraNode");
			cameraNode->AttachObject(*camera);

			// The baked tiles, loaded from their files with the material set up the way the runtime will.
			SceneNode* bakeNode = scene.GetRootSceneNode().CreateChildSceneNode("GlueTerrainCompareBake");
			std::vector<Entity*> entities;
			for (size_t i = 0; i < tiles.size(); ++i)
			{
				const GlueBakeTerrainTile& tile = tiles[i];
				const MeshPtr mesh = LoadMeshFile(tile.mesh);
				if (!mesh)
				{
					ELOG("Glue terrain compare: cannot read " << tile.mesh);
					success = false;
					continue;
				}

				std::shared_ptr<MaterialInstance> material;
				if (!tile.splatting.empty())
				{
					const TexturePtr splatting = LoadTextureFile(tile.splatting);
					const MaterialPtr parent = MaterialManager::Get().Load(tile.material);
					if (!splatting || !parent)
					{
						ELOG("Glue terrain compare: cannot read " << tile.splatting << " or " << tile.material);
						success = false;
						continue;
					}

					// What terrain::Tile does with its coverage texture.
					splatting->SetTextureAddressMode(TextureAddressMode::Clamp);
					splatting->SetFilter(TextureFilter::Anisotropic);
					material = std::make_shared<MaterialInstance>("GlueTerrainCompareMaterial" + std::to_string(i), parent);
					material->SetTextureParameter("Splatting", splatting);
				}
				else
				{
					// The written material instance, with the albedo texture loaded from its file: the instance
					// names it by its file system path, which the texture manager cannot open.
					const String albedoPath = tile.material.substr(0, tile.material.size() - 4) + ".htex";
					const TexturePtr albedo = LoadTextureFile(albedoPath);
					material = LoadMaterialInstanceFile(tile.material);
					if (!albedo || !material)
					{
						ELOG("Glue terrain compare: cannot read " << tile.material << " or " << albedoPath);
						success = false;
						continue;
					}

					// What FarTerrain does with its page colour.
					albedo->SetTextureAddressMode(TextureAddressMode::Clamp);
					material->SetTextureParameter("Albedo", albedo);
				}

				Entity* entity = scene.CreateEntity("GlueTerrainCompareEntity" + std::to_string(i), mesh);
				entity->SetMaterial(material);
				bakeNode->AttachObject(*entity);
				entities.push_back(entity);
			}

			// Water keeps the surface material stored in its mesh and casts no shadow, like the live water.
			for (size_t i = 0; i < water.size(); ++i)
			{
				const MeshPtr mesh = LoadMeshFile(water[i].mesh);
				if (!mesh)
				{
					ELOG("Glue terrain compare: cannot read " << water[i].mesh);
					success = false;
					continue;
				}

				Entity* entity = scene.CreateEntity("GlueTerrainCompareWater" + std::to_string(i), mesh);
				entity->SetCastShadows(false);
				bakeNode->AttachObject(*entity);
				entities.push_back(entity);
			}

			// Keep the live pages loaded for every view.
			std::vector<std::unique_ptr<page_bake::ScopedPageLoad>> loads;
			for (const PagePosition& position : pages)
			{
				terrain::Page* page = terrain->GetPage(static_cast<uint32>(position.x()), static_cast<uint32>(position.y()));
				if (page)
				{
					loads.push_back(std::make_unique<page_bake::ScopedPageLoad>(*page, static_cast<uint32>(position.x()), static_cast<uint32>(position.y()),
						[&world]() { return world.PumpStreaming(); }));
				}
			}

			for (const GlueTerrainCompareView& view : views)
			{
				cameraNode->SetPosition(view.pose.position);
				cameraNode->LookAt(view.pose.lookAt, TransformSpace::World, Vector3::NegativeUnitZ);
				camera->InvalidateFrustum();
				camera->InvalidateView();

				std::vector<uint8> liveLit;
				std::vector<uint8> bakedLit;
				Image liveAlbedo;
				Image bakedAlbedo;
				{
					page_bake::ScopedVisibleSubtrees terrainOnly(scene.GetRootSceneNode(), { terrain->GetNode() }, true);
					Render(*renderer, scene, *camera, liveLit, liveAlbedo);
				}
				{
					page_bake::ScopedVisibleSubtrees bakeOnly(scene.GetRootSceneNode(), { bakeNode }, true);
					Render(*renderer, scene, *camera, bakedLit, bakedAlbedo);
				}

				const std::filesystem::path dir(artDir);
				success = WritePpm(dir / (view.name + "_live.ppm"), liveLit) && success;
				success = WritePpm(dir / (view.name + "_baked.ppm"), bakedLit) && success;
				success = WritePpm(dir / (view.name + "_live_albedo.ppm"), liveAlbedo.rgb) && success;
				success = WritePpm(dir / (view.name + "_baked_albedo.ppm"), bakedAlbedo.rgb) && success;

				uint64 both = 0;
				uint64 onlyLive = 0;
				uint64 onlyBaked = 0;
				uint64 sum = 0;
				uint32 maximum = 0;
				uint64 litSum = 0;
				for (size_t p = 0; p < liveAlbedo.covered.size(); ++p)
				{
					if (liveAlbedo.covered[p] && bakedAlbedo.covered[p])
					{
						++both;
						for (uint32 c = 0; c < 3; ++c)
						{
							const uint32 difference = static_cast<uint32>(std::abs(static_cast<int32>(liveAlbedo.rgb[p * 3 + c]) - static_cast<int32>(bakedAlbedo.rgb[p * 3 + c])));
							sum += difference;
							maximum = std::max(maximum, difference);
							litSum += static_cast<uint32>(std::abs(static_cast<int32>(liveLit[p * 3 + c]) - static_cast<int32>(bakedLit[p * 3 + c])));
						}
					}
					else if (liveAlbedo.covered[p])
					{
						++onlyLive;
					}
					else if (bakedAlbedo.covered[p])
					{
						++onlyBaked;
					}
				}

				const double meanAlbedo = both ? static_cast<double>(sum) / static_cast<double>(both * 3) : 0.0;
				const double meanLit = both ? static_cast<double>(litSum) / static_cast<double>(both * 3) : 0.0;
				ILOG("Glue terrain compare " << view.name << ": covered by both " << both << " px, only live " << onlyLive << " px, only baked " << onlyBaked
					<< " px; albedo mean abs diff " << meanAlbedo << " (max " << maximum << ") of 255; lit mean abs diff " << meanLit);
			}

			loads.clear();

			for (Entity* entity : entities)
			{
				entity->DetachFromParent();
				scene.DestroyEntity(*entity);
			}
			scene.DestroySceneNode(*bakeNode);

			cameraNode->DetachAllObjects();
			scene.DestroyCamera(*camera);
			scene.DestroySceneNode(*cameraNode);
		}

		world.SetFoliageVisible(foliageWasVisible);
		terrain->SetLodEnabled(lodWasEnabled);
		gx.RestoreState();
		return success;
	}
}
