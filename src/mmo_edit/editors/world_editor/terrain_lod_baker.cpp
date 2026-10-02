// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "terrain_lod_baker.h"

#include "assets/asset_registry.h"
#include "binary_io/writer.h"
#include "deferred_shading/deferred_renderer.h"
#include "graphics/g_buffer.h"
#include "graphics/graphics_device.h"
#include "graphics/render_texture.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/movable_object.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "stb_dxt.h"
#include "stream_sink.h"
#include "terrain/constants.h"
#include "terrain/page.h"
#include "terrain/terrain.h"
#include "terrain_io/page_lod.h"
#include "tex_v1_0/header_save.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace mmo
{
	namespace
	{
		/// IEEE 754 half to float (the G-buffer targets are RGBA16F).
		float HalfToFloat(const uint16 half)
		{
			const uint32 sign = (half & 0x8000u) << 16;
			const uint32 exponent = (half >> 10) & 0x1fu;
			const uint32 mantissa = half & 0x3ffu;

			uint32 bits;
			if (exponent == 0)
			{
				if (mantissa == 0)
				{
					bits = sign;
				}
				else
				{
					// Subnormal: renormalise.
					uint32 m = mantissa;
					int32 e = -1;
					do
					{
						++e;
						m <<= 1;
					} while ((m & 0x400u) == 0);

					bits = sign | (static_cast<uint32>(127 - 15 - e) << 23) | ((m & 0x3ffu) << 13);
				}
			}
			else if (exponent == 0x1f)
			{
				bits = sign | 0x7f800000u | (mantissa << 13);
			}
			else
			{
				bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
			}

			float result;
			std::memcpy(&result, &bits, sizeof(result));
			return result;
		}

		/// Hides every movable object outside the terrain's scene graph and restores it on destruction.
		class ScopedTerrainOnlyScene final
		{
		public:
			ScopedTerrainOnlyScene(SceneNode& root, const SceneNode* terrainNode)
			{
				Hide(root, terrainNode);
			}

			~ScopedTerrainOnlyScene()
			{
				for (const auto& [object, visible] : m_hidden)
				{
					object->SetVisible(visible);
				}
			}

		private:
			void Hide(SceneNode& node, const SceneNode* terrainNode)
			{
				if (&node == terrainNode)
				{
					return;
				}

				for (uint32 i = 0; i < node.GetNumAttachedObjects(); ++i)
				{
					// Cameras are never drawn; leave the bake camera (and everyone else's) alone.
					MovableObject* object = node.GetAttachedObject(i);
					if (object && object->IsVisible() && !dynamic_cast<Camera*>(object))
					{
						m_hidden.emplace_back(object, true);
						object->SetVisible(false);
					}
				}

				for (uint32 i = 0; i < node.GetNumChildren(); ++i)
				{
					if (auto* child = dynamic_cast<SceneNode*>(node.GetChild(i)))
					{
						Hide(*child, terrainNode);
					}
				}
			}

			std::vector<std::pair<MovableObject*, bool>> m_hidden;
		};

		/// Box-filters an RGBA8 image to half its size.
		std::vector<uint8> Downsample(const std::vector<uint8>& source, const uint32 size)
		{
			const uint32 half = size / 2;
			std::vector<uint8> result(half * half * 4);
			for (uint32 y = 0; y < half; ++y)
			{
				for (uint32 x = 0; x < half; ++x)
				{
					for (uint32 c = 0; c < 4; ++c)
					{
						const uint32 sum =
							source[((y * 2) * size + x * 2) * 4 + c] +
							source[((y * 2) * size + x * 2 + 1) * 4 + c] +
							source[((y * 2 + 1) * size + x * 2) * 4 + c] +
							source[((y * 2 + 1) * size + x * 2 + 1) * 4 + c];
						result[(y * half + x) * 4 + c] = static_cast<uint8>((sum + 2) / 4);
					}
				}
			}

			return result;
		}

		/// Writes an RGBA8 image as a DXT1 .htex with a mip chain down to 4x4.
		bool WriteDxt1Texture(const String& filename, std::vector<uint8> rgba, uint32 size)
		{
			const auto file = AssetRegistry::CreateNewFile(filename);
			if (!file)
			{
				ELOG("Failed to create " << filename);
				return false;
			}

			io::StreamSink sink(*file);

			tex::v1_0::Header header(tex::Version_1_0);
			header.width = static_cast<uint16>(size);
			header.height = static_cast<uint16>(size);
			header.format = tex::v1_0::DXT1;
			header.hasMips = true;

			tex::v1_0::HeaderSaver saver(sink, header);

			for (uint32 mip = 0; mip < header.mipmapOffsets.size() && size >= 4; ++mip)
			{
				std::vector<uint8> compressed(size * size / 2);
				rygCompress(compressed.data(), rgba.data(), static_cast<int>(size), static_cast<int>(size), 0);

				header.mipmapOffsets[mip] = static_cast<uint32>(sink.Position());
				header.mipmapLengths[mip] = static_cast<uint32>(compressed.size());
				sink.Write(reinterpret_cast<const char*>(compressed.data()), compressed.size());

				rgba = Downsample(rgba, size);
				size /= 2;
			}

			saver.finish();
			file->flush();
			return true;
		}
	}

	TerrainLodBaker::TerrainLodBaker(Scene& scene, terrain::Terrain& terrain)
		: m_scene(scene)
		, m_terrain(terrain)
	{
		m_renderer = std::make_unique<DeferredRenderer>(GraphicsDevice::Get(), m_scene, TextureSize, TextureSize);

		// Top-down orthographic camera covering exactly one page, set up like the minimap camera:
		// image right = +X, image up = -Z, so texel (u, v) maps to page-local (x, z).
		m_camera = m_scene.CreateCamera("TerrainLodBakeCamera");
		m_camera->SetProjectionType(ProjectionType::Orthographic);
		m_camera->SetOrthoWindow(static_cast<float>(terrain::constants::PageSize), static_cast<float>(terrain::constants::PageSize));
		m_camera->SetAspectRatio(1.0f);
		m_camera->SetNearClipDistance(1.0f);

		m_cameraNode = m_scene.GetRootSceneNode().CreateChildSceneNode("TerrainLodBakeCameraNode");
		m_cameraNode->AttachObject(*m_camera);
		m_cameraNode->SetOrientation(Quaternion(Degree(-90.0f), Vector3::UnitX));
	}

	TerrainLodBaker::~TerrainLodBaker()
	{
		m_cameraNode->DetachAllObjects();
		m_scene.DestroyCamera(*m_camera);
		m_scene.DestroySceneNode(*m_cameraNode);
	}

	TerrainLodBaker::Stats TerrainLodBaker::BakeAll(const bool onlyStale)
	{
		Stats stats;

		auto& gx = GraphicsDevice::Get();
		gx.CaptureState();

		// Bake the full-resolution surface: tile LOD would pick coarser levels for the page corners.
		const bool lodWasEnabled = m_terrain.IsLodEnabled();
		m_terrain.SetLodEnabled(false);

		{
			ScopedTerrainOnlyScene terrainOnly(m_scene.GetRootSceneNode(), m_terrain.GetNode());

			for (uint32 z = 0; z < m_terrain.GetHeight(); ++z)
			{
				for (uint32 x = 0; x < m_terrain.GetWidth(); ++x)
				{
					if (!AssetRegistry::HasFile(m_terrain.GetPageFilename(x, z)))
					{
						continue;
					}

					terrain::Page* page = m_terrain.GetPage(x, z);
					if (!page)
					{
						continue;
					}

					if (onlyStale && !page->IsChanged() && IsUpToDate(x, z))
					{
						++stats.upToDate;
						continue;
					}

					if (BakePage(*page, x, z))
					{
						++stats.baked;
					}
					else
					{
						++stats.failed;
					}
				}
			}
		}

		m_terrain.SetLodEnabled(lodWasEnabled);
		gx.RestoreState();

		ILOG("Terrain LOD bake finished: " << stats.baked << " baked, " << stats.upToDate << " up to date, " << stats.failed << " failed");
		return stats;
	}

	bool TerrainLodBaker::IsUpToDate(const uint32 x, const uint32 z) const
	{
		const auto source = AssetRegistry::GetLastWriteTime(m_terrain.GetPageFilename(x, z));
		const auto lod = AssetRegistry::GetLastWriteTime(m_terrain.GetPageLodFilename(x, z));
		const auto texture = AssetRegistry::GetLastWriteTime(m_terrain.GetPageLodTextureFilename(x, z));
		return source && lod && texture && *lod >= *source && *texture >= *source;
	}

	bool TerrainLodBaker::BakePage(terrain::Page& page, const uint32 x, const uint32 z)
	{
		const bool wasPrepared = page.IsPrepared();
		const bool wasLoaded = page.IsLoaded();
		ILOG("Baking terrain LOD of page " << x << "x" << z);

		if (!wasPrepared && !page.Prepare())
		{
			WLOG("Terrain LOD bake: failed to prepare page " << x << "x" << z);
			return false;
		}

		// Load() creates one tile per call.
		while (page.IsLoadable() && !page.Load())
		{
		}

		if (!page.IsLoaded())
		{
			WLOG("Terrain LOD bake: failed to load page " << x << "x" << z);
			if (!wasPrepared)
			{
				page.Destroy();
			}
			return false;
		}

		bool success = true;

		terrain_io::PageLodData lod;
		page.BuildLod(lod);
		if (const auto file = AssetRegistry::CreateNewFile(m_terrain.GetPageLodFilename(x, z)))
		{
			io::StreamSink sink(*file);
			io::Writer writer(sink);
			success = terrain_io::SavePageLod(writer, lod);
			file->flush();
		}
		else
		{
			ELOG("Terrain LOD bake: failed to create " << m_terrain.GetPageLodFilename(x, z));
			success = false;
		}

		std::vector<uint8> color;
		success = success && RenderColor(page, color);
		success = success && WriteDxt1Texture(m_terrain.GetPageLodTextureFilename(x, z), std::move(color), TextureSize);

		// Leave the page the way the editor's streaming had it.
		if (!wasLoaded)
		{
			page.Unload();
		}

		if (!wasPrepared)
		{
			page.Destroy();
		}

		return success;
	}

	bool TerrainLodBaker::RenderColor(terrain::Page& page, std::vector<uint8>& srgb)
	{
		const AABB& bounds = page.GetBoundingBox();
		const Vector3 pageOrigin = page.GetSceneNode()->GetDerivedPosition();
		const float halfPage = static_cast<float>(terrain::constants::PageSize) * 0.5f;

		// Water surfaces can rise above the terrain's bounding box.
		const float top = std::max(bounds.max.y, 0.0f) + 50.0f;
		m_cameraNode->SetPosition(Vector3(pageOrigin.x + halfPage, top, pageOrigin.z + halfPage));
		m_camera->SetFarClipDistance(top - std::min(bounds.min.y, 0.0f) + 50.0f);
		m_camera->InvalidateFrustum();
		m_camera->InvalidateView();

		// The page water is translucent and samples scene depth/refraction; the opaque minimap
		// variant lands in the G-buffer instead.
		page.SetMinimapWaterMode(true);
		// Twice: freshly loaded tiles build their index buffers in PreRender, which the scene calls
		// after it has already captured their (still empty) render operation, so a page's first
		// frame draws nothing.
		auto& gx = GraphicsDevice::Get();
		for (int pass = 0; pass < 2; ++pass)
		{
			// Same per-frame device reset the editor viewport does before it renders.
			gx.Reset();
			gx.SetViewport(0, 0, TextureSize, TextureSize, 0.0f, 1.0f);
			m_renderer->Render(m_scene, *m_camera);
		}
		page.SetMinimapWaterMode(false);

		GBuffer& gBuffer = m_renderer->GetGBuffer();
		RenderTexture& albedoTarget = gBuffer.GetAlbedoRT();
		RenderTexture& emissiveTarget = gBuffer.GetEmissiveRT();

		constexpr uint32 pixelCount = TextureSize * TextureSize;
		if (albedoTarget.GetPixelDataSize() != pixelCount * 8 || emissiveTarget.GetPixelDataSize() != pixelCount * 8)
		{
			ELOG("Terrain LOD bake: unexpected G-buffer format");
			return false;
		}

		std::vector<uint16> albedo(pixelCount * 4);
		std::vector<uint16> emissive(pixelCount * 4);
		albedoTarget.CopyPixelDataTo(reinterpret_cast<uint8*>(albedo.data()));
		emissiveTarget.CopyPixelDataTo(reinterpret_cast<uint8*>(emissive.data()));

		// Linear base colour to sRGB: the far material samples its albedo as a colour texture.
		srgb.resize(pixelCount * 4);
		for (uint32 i = 0; i < pixelCount; ++i)
		{
			for (uint32 c = 0; c < 3; ++c)
			{
				const float linear = std::clamp(HalfToFloat(albedo[i * 4 + c]) + HalfToFloat(emissive[i * 4 + c]), 0.0f, 1.0f);
				srgb[i * 4 + c] = static_cast<uint8>(std::lround(std::pow(linear, 1.0f / 2.2f) * 255.0f));
			}

			srgb[i * 4 + 3] = 255;
		}

		// Terrain holes draw nothing and would bake as black spots, while the far mesh has no holes.
		// The G-buffer albedo alpha is 1 wherever geometry was drawn; give the rest the page's mean colour.
		uint64 sum[3] = {};
		uint32 covered = 0;
		for (uint32 i = 0; i < pixelCount; ++i)
		{
			if (HalfToFloat(albedo[i * 4 + 3]) > 0.5f)
			{
				sum[0] += srgb[i * 4 + 0];
				sum[1] += srgb[i * 4 + 1];
				sum[2] += srgb[i * 4 + 2];
				++covered;
			}
		}

		if (covered > 0 && covered < pixelCount)
		{
			for (uint32 i = 0; i < pixelCount; ++i)
			{
				if (HalfToFloat(albedo[i * 4 + 3]) <= 0.5f)
				{
					for (uint32 c = 0; c < 3; ++c)
					{
						srgb[i * 4 + c] = static_cast<uint8>(sum[c] / covered);
					}
				}
			}
		}

		return true;
	}
}
