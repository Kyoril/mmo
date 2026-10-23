// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "terrain_lod_baker.h"

#include "page_bake_utils.h"

#include "assets/asset_registry.h"
#include "binary_io/writer.h"
#include "deferred_shading/deferred_renderer.h"
#include "graphics/g_buffer.h"
#include "graphics/global_shader_parameters.h"
#include "graphics/graphics_device.h"
#include "graphics/render_texture.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "stream_sink.h"
#include "terrain/constants.h"
#include "terrain/page.h"
#include "terrain/terrain.h"
#include "terrain_io/page_lod.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mmo
{
	TerrainLodBaker::TerrainLodBaker(Scene& scene, terrain::Terrain& terrain, std::function<bool()> pumpStreaming, const uint32 textureSize)
		: m_scene(scene)
		, m_terrain(terrain)
		, m_pumpStreaming(std::move(pumpStreaming))
		, m_textureSize(textureSize)
	{
		m_renderer = std::make_unique<DeferredRenderer>(GraphicsDevice::Get(), m_scene, m_textureSize, m_textureSize);

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
			page_bake::ScopedVisibleSubtrees terrainOnly(m_scene.GetRootSceneNode(), { m_terrain.GetNode() });

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

	bool TerrainLodBaker::RenderPageColor(const uint32 x, const uint32 z, std::vector<uint8>& srgb, const ColorOptions& options)
	{
		if (x >= m_terrain.GetWidth() || z >= m_terrain.GetHeight() || !AssetRegistry::HasFile(m_terrain.GetPageFilename(x, z)))
		{
			return false;
		}

		terrain::Page* page = m_terrain.GetPage(x, z);
		if (!page)
		{
			return false;
		}

		auto& gx = GraphicsDevice::Get();
		gx.CaptureState();

		const bool lodWasEnabled = m_terrain.IsLodEnabled();
		m_terrain.SetLodEnabled(false);

		bool success = false;
		{
			page_bake::ScopedVisibleSubtrees terrainOnly(m_scene.GetRootSceneNode(), { m_terrain.GetNode() });
			const page_bake::ScopedPageLoad load(*page, x, z, m_pumpStreaming);
			success = load.IsLoaded() && RenderColor(*page, srgb, options);
		}

		m_terrain.SetLodEnabled(lodWasEnabled);
		gx.RestoreState();
		return success;
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
		ILOG("Baking terrain LOD of page " << x << "x" << z);

		const page_bake::ScopedPageLoad load(page, x, z, m_pumpStreaming);
		if (!load.IsLoaded())
		{
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
		ColorOptions colorOptions;
		colorOptions.emissiveRatioInAlpha = true;
		success = success && RenderColor(page, color, colorOptions);
		success = success && page_bake::WriteDxt5Texture(m_terrain.GetPageLodTextureFilename(x, z), std::move(color), m_textureSize);

		return success;
	}

	bool TerrainLodBaker::RenderColor(terrain::Page& page, std::vector<uint8>& srgb, const ColorOptions& options)
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
		// variant lands in the G-buffer instead. Without water the ground under it is rendered.
		// Ocean is left out unless asked for: the ocean renderer covers the distant seabed.
		if (options.water)
		{
			page.SetMinimapWaterMode(true, options.ocean);
		}
		else
		{
			page.SetWaterVisible(false);
		}
		// Twice: freshly loaded tiles build their index buffers in PreRender, which the scene calls
		// after it has already captured their (still empty) render operation, so a page's first
		// frame draws nothing.
		// Bake in daylight: materials fade their emissive with the global NightFactor (the desert
		// glow), and the far material applies that fade itself at runtime. Baking while the editor
		// sky is at night would store no emissive at all.
		GlobalShaderParameters& globals = GlobalShaderParameters::Get();
		float nightFactor = 0.0f;
		const bool hasNightFactor = globals.GetScalar("NightFactor", nightFactor);
		globals.SetScalar("NightFactor", 0.0f);

		auto& gx = GraphicsDevice::Get();
		for (int pass = 0; pass < 2; ++pass)
		{
			// Same per-frame device reset the editor viewport does before it renders.
			gx.Reset();
			gx.SetViewport(0, 0, m_textureSize, m_textureSize, 0.0f, 1.0f);
			m_renderer->Render(m_scene, *m_camera);
		}

		if (hasNightFactor)
		{
			globals.SetScalar("NightFactor", nightFactor);
		}
		if (options.water)
		{
			page.SetMinimapWaterMode(false);
		}
		else
		{
			page.SetWaterVisible(m_terrain.IsWaterVisible());
		}

		GBuffer& gBuffer = m_renderer->GetGBuffer();
		RenderTexture& albedoTarget = gBuffer.GetAlbedoRT();
		RenderTexture& emissiveTarget = gBuffer.GetEmissiveRT();

		const uint32 pixelCount = m_textureSize * m_textureSize;
		if (albedoTarget.GetPixelDataSize() != pixelCount * 8 || emissiveTarget.GetPixelDataSize() != pixelCount * 8)
		{
			ELOG("Terrain LOD bake: unexpected G-buffer format");
			return false;
		}

		std::vector<uint16> albedo(pixelCount * 4);
		std::vector<uint16> emissive(pixelCount * 4);
		albedoTarget.CopyPixelDataTo(reinterpret_cast<uint8*>(albedo.data()));
		emissiveTarget.CopyPixelDataTo(reinterpret_cast<uint8*>(emissive.data()));

		// Stored as is by default: material texture samples are never sRGB-decoded (MaterialNode compiles
		// them with srgb = false), so the G-buffer albedo already holds raw texel values and the far material
		// reads this texture the same way. Gamma-encoding it made every far page brighter and paler; the
		// srgb option keeps that encoding available for callers that want it.
		srgb.resize(pixelCount * 4);
		double emissiveSum = 0.0;
		for (uint32 i = 0; i < pixelCount; ++i)
		{
			float albedoTotal = 0.0f;
			float emittedTotal = 0.0f;
			for (uint32 c = 0; c < 3; ++c)
			{
				const float emitted = std::max(page_bake::HalfToFloat(emissive[i * 4 + c]), 0.0f);
				const float reflected = std::max(page_bake::HalfToFloat(albedo[i * 4 + c]), 0.0f);
				emissiveSum += emitted;
				albedoTotal += reflected;
				emittedTotal += emitted;
				const float linear = std::clamp(options.emissiveRatioInAlpha ? reflected : reflected + emitted, 0.0f, 1.0f);
				const float encoded = options.srgb ? std::pow(linear, 1.0f / 2.2f) : linear;
				srgb[i * 4 + c] = static_cast<uint8>(std::lround(encoded * 255.0f));
			}

			const float emissiveRatio = options.emissiveRatioInAlpha && albedoTotal > 1e-4f ? emittedTotal / albedoTotal : 0.0f;
			srgb[i * 4 + 3] = static_cast<uint8>(std::lround((1.0f - std::clamp(emissiveRatio, 0.0f, 1.0f)) * 255.0f));
		}

		if (!options.srgb)
		{
			DLOG("Terrain colour render of page " << page.GetX() << "x" << page.GetY() << ": mean emissive " << emissiveSum / (pixelCount * 3.0));
		}

		// Terrain holes draw nothing and would bake as black spots, while the far mesh has no holes.
		// The G-buffer albedo alpha is 1 wherever geometry was drawn; give the rest the page's mean colour.
		uint64 sum[4] = {};
		uint32 covered = 0;
		for (uint32 i = 0; i < pixelCount; ++i)
		{
			if (page_bake::HalfToFloat(albedo[i * 4 + 3]) > 0.5f)
			{
				sum[0] += srgb[i * 4 + 0];
				sum[1] += srgb[i * 4 + 1];
				sum[2] += srgb[i * 4 + 2];
				sum[3] += srgb[i * 4 + 3];
				++covered;
			}
		}

		if (covered > 0 && covered < pixelCount)
		{
			for (uint32 i = 0; i < pixelCount; ++i)
			{
				if (page_bake::HalfToFloat(albedo[i * 4 + 3]) <= 0.5f)
				{
					for (uint32 c = 0; c < 4; ++c)
					{
						srgb[i * 4 + c] = static_cast<uint8>(sum[c] / covered);
					}
				}
			}
		}

		return true;
	}
}
