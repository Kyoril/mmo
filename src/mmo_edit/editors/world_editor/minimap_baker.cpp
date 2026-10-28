// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "minimap_baker.h"

#include "page_bake_utils.h"

#include "assets/asset_registry.h"
#include "deferred_shading/deferred_renderer.h"
#include "graphics/g_buffer.h"
#include "graphics/graphics_device.h"
#include "graphics/render_texture.h"
#include "log/default_log_levels.h"
#include "scene_graph/camera.h"
#include "scene_graph/movable_object.h"
#include "scene_graph/scene.h"
#include "scene_graph/scene_node.h"
#include "terrain/constants.h"
#include "terrain/minimap_shading.h"
#include "terrain/page.h"
#include "terrain/terrain.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mmo
{
	namespace
	{
		using namespace terrain::minimap_shading;

		void CollectVisibleObjects(const SceneNode& node, std::vector<MovableObject*>& out)
		{
			for (uint32 i = 0; i < node.GetNumAttachedObjects(); ++i)
			{
				MovableObject* object = node.GetAttachedObject(i);
				if (object && object->IsVisible() && !dynamic_cast<Camera*>(object))
				{
					out.push_back(object);
				}
			}

			for (uint32 i = 0; i < node.GetNumChildren(); ++i)
			{
				if (const auto* child = dynamic_cast<const SceneNode*>(node.GetChild(i)))
				{
					CollectVisibleObjects(*child, out);
				}
			}
		}

		/// Whether two G-buffer texels differ, i.e. something other than the terrain was drawn there.
		bool Differs(const std::vector<uint16>& a, const std::vector<uint16>& b, const size_t texel)
		{
			for (size_t c = 0; c < 4; ++c)
			{
				if (std::abs(page_bake::HalfToFloat(a[texel * 4 + c]) - page_bake::HalfToFloat(b[texel * 4 + c])) > 0.004f)
				{
					return true;
				}
			}

			return false;
		}
	}

	MinimapBaker::MinimapBaker(Scene& scene, terrain::Terrain& terrain, String worldName, ModelProvider prepareModels, std::function<bool()> pumpStreaming)
		: m_scene(scene)
		, m_terrain(terrain)
		, m_worldName(std::move(worldName))
		, m_prepareModels(std::move(prepareModels))
		, m_pumpStreaming(std::move(pumpStreaming))
	{
		m_renderer = std::make_unique<DeferredRenderer>(GraphicsDevice::Get(), m_scene, RenderSize, RenderSize);

		// Top-down orthographic camera covering exactly one page: image right = +X, image up = -Z
		// (north), so texel (u, v) maps to page-local (x, z).
		m_camera = m_scene.CreateCamera("MinimapBakeCamera");
		m_camera->SetProjectionType(ProjectionType::Orthographic);
		m_camera->SetOrthoWindow(static_cast<float>(terrain::constants::PageSize), static_cast<float>(terrain::constants::PageSize));
		m_camera->SetAspectRatio(1.0f);
		m_camera->SetNearClipDistance(1.0f);

		m_cameraNode = m_scene.GetRootSceneNode().CreateChildSceneNode("MinimapBakeCameraNode");
		m_cameraNode->AttachObject(*m_camera);
		m_cameraNode->SetOrientation(Quaternion(Degree(-90.0f), Vector3::UnitX));
	}

	MinimapBaker::~MinimapBaker()
	{
		m_cameraNode->DetachAllObjects();
		m_scene.DestroyCamera(*m_camera);
		m_scene.DestroySceneNode(*m_cameraNode);
	}

	String MinimapBaker::GetTileFilename(const String& worldName, const uint32 x, const uint32 z)
	{
		// Page index: X in the high byte, Z in the low byte (what the client's Minimap expects).
		const uint16 pageIndex = static_cast<uint16>(((x & 0xff) << 8) | (z & 0xff));
		return "Textures/Minimaps/" + worldName + "/" + std::to_string(pageIndex) + ".htex";
	}

	std::vector<MinimapBaker::PageCoord> MinimapBaker::CollectPages(const bool onlyStale, const Region& region, Stats& stats)
	{
		std::vector<PageCoord> pages;
		for (uint32 z = 0; z < m_terrain.GetHeight(); ++z)
		{
			for (uint32 x = 0; x < m_terrain.GetWidth(); ++x)
			{
				if (!region.Contains(x, z) || !AssetRegistry::HasFile(m_terrain.GetPageFilename(x, z)))
				{
					continue;
				}

				const terrain::Page* page = m_terrain.GetPage(x, z);
				if (!page)
				{
					continue;
				}

				if (onlyStale && !page->IsChanged() && IsUpToDate(x, z))
				{
					++stats.upToDate;
					continue;
				}

				pages.push_back({ x, z });
			}
		}

		return pages;
	}

	bool MinimapBaker::BakeSinglePage(const uint32 x, const uint32 z)
	{
		terrain::Page* page = m_terrain.GetPage(x, z);
		if (!page)
		{
			return false;
		}

		auto& gx = GraphicsDevice::Get();
		gx.CaptureState();

		// Bake the full-resolution surface: tile LOD would pick coarser levels for the page corners.
		const bool lodWasEnabled = m_terrain.IsLodEnabled();
		m_terrain.SetLodEnabled(false);

		const bool baked = BakePage(*page, x, z);

		m_terrain.SetLodEnabled(lodWasEnabled);
		gx.RestoreState();
		return baked;
	}

	MinimapBaker::Stats MinimapBaker::BakeAll(const bool onlyStale, const Region& region)
	{
		Stats stats;
		for (const PageCoord& page : CollectPages(onlyStale, region, stats))
		{
			if (BakeSinglePage(page.x, page.z))
			{
				++stats.baked;
			}
			else
			{
				++stats.failed;
			}
		}

		ILOG("Minimap bake finished: " << stats.baked << " baked, " << stats.upToDate << " up to date, " << stats.failed << " failed");
		return stats;
	}

	bool MinimapBaker::IsUpToDate(const uint32 x, const uint32 z) const
	{
		const auto source = AssetRegistry::GetLastWriteTime(m_terrain.GetPageFilename(x, z));
		const auto tile = AssetRegistry::GetLastWriteTime(GetTileFilename(m_worldName, x, z));
		return source && tile && *tile >= *source;
	}

	void MinimapBaker::SetModelsVisible(const bool visible)
	{
		for (MovableObject* object : m_modelObjects)
		{
			object->SetVisible(visible);
		}
	}

	bool MinimapBaker::BakePage(terrain::Page& page, const uint32 x, const uint32 z)
	{
		ILOG("Baking minimap of page " << x << "x" << z);

		const std::vector<const SceneNode*> modelNodes = m_prepareModels ? m_prepareModels(x, z) : std::vector<const SceneNode*>{};

		const page_bake::ScopedPageLoad load(page, x, z, m_pumpStreaming);
		if (!load.IsLoaded())
		{
			return false;
		}

		// Everything but terrain and world models goes: editor helpers, static meshes, units, foliage.
		// Per page, because the model provider loads and unloads entities between pages.
		std::vector<const SceneNode*> keep = modelNodes;
		keep.push_back(m_terrain.GetNode());
		const page_bake::ScopedVisibleSubtrees minimapScene(m_scene.GetRootSceneNode(), std::move(keep));

		m_modelObjects.clear();
		for (const SceneNode* node : modelNodes)
		{
			if (node)
			{
				CollectVisibleObjects(*node, m_modelObjects);
			}
		}

		// Water is painted from the page data, never rendered.
		page.SetWaterVisible(false);

		GBufferCopy terrainOnly;
		GBufferCopy withModels;

		SetModelsVisible(false);
		bool success = Render(page, terrainOnly);
		SetModelsVisible(true);
		success = success && Render(page, withModels);

		page.SetWaterVisible(true);

		if (!success)
		{
			return false;
		}

		std::vector<uint8> srgb;
		Compose(page, withModels, terrainOnly, srgb);

		std::vector<uint8> output = srgb;
		for (uint32 size = RenderSize; size > OutputSize; size /= 2)
		{
			output = page_bake::Downsample(output, size);
		}

		return page_bake::WriteDxt1Texture(GetTileFilename(m_worldName, x, z), std::move(output), OutputSize);
	}

	bool MinimapBaker::Render(terrain::Page& page, GBufferCopy& out)
	{
		const AABB& bounds = page.GetBoundingBox();
		const Vector3 pageOrigin = page.GetSceneNode()->GetDerivedPosition();
		const float halfPage = static_cast<float>(terrain::constants::PageSize) * 0.5f;

		// Generous: world models (towers, ships) rise well above the terrain's bounding box.
		const float top = std::max(bounds.max.y, 0.0f) + 400.0f;
		m_cameraNode->SetPosition(Vector3(pageOrigin.x + halfPage, top, pageOrigin.z + halfPage));
		m_camera->SetFarClipDistance(top - std::min(bounds.min.y, 0.0f) + 100.0f);
		m_camera->InvalidateFrustum();
		m_camera->InvalidateView();

		// Twice: freshly loaded tiles build their index buffers in PreRender, which the scene calls
		// after it has already captured their (still empty) render operation, so a page's first
		// frame draws nothing.
		auto& gx = GraphicsDevice::Get();
		for (int pass = 0; pass < 2; ++pass)
		{
			// Same per-frame device reset the editor viewport does before it renders.
			gx.Reset();
			gx.SetViewport(0, 0, RenderSize, RenderSize, 0.0f, 1.0f);
			m_renderer->Render(m_scene, *m_camera);
		}

		GBuffer& gBuffer = m_renderer->GetGBuffer();
		RenderTexture& albedoTarget = gBuffer.GetAlbedoRT();
		RenderTexture& normalTarget = gBuffer.GetNormalRT();
		RenderTexture& emissiveTarget = gBuffer.GetEmissiveRT();

		constexpr uint32 pixelCount = RenderSize * RenderSize;
		if (albedoTarget.GetPixelDataSize() != pixelCount * 8 || normalTarget.GetPixelDataSize() != pixelCount * 8 || emissiveTarget.GetPixelDataSize() != pixelCount * 8)
		{
			ELOG("Minimap bake: unexpected G-buffer format");
			return false;
		}

		out.albedo.resize(pixelCount * 4);
		out.normal.resize(pixelCount * 4);
		out.emissive.resize(pixelCount * 4);
		albedoTarget.CopyPixelDataTo(reinterpret_cast<uint8*>(out.albedo.data()));
		normalTarget.CopyPixelDataTo(reinterpret_cast<uint8*>(out.normal.data()));
		emissiveTarget.CopyPixelDataTo(reinterpret_cast<uint8*>(out.emissive.data()));
		return true;
	}

	void MinimapBaker::Compose(const terrain::Page& page, const GBufferCopy& withModels, const GBufferCopy& terrainOnly, std::vector<uint8>& srgb) const
	{
		constexpr uint32 size = RenderSize;
		constexpr size_t pixelCount = static_cast<size_t>(size) * size;
		const float metresPerPixel = static_cast<float>(terrain::constants::PageSize) / static_cast<float>(size);
		const Vector3 pageOrigin = page.GetSceneNode()->GetDerivedPosition();
		const terrain::water_lookup::PageWaterView water = page.GetWaterView();

		std::vector<Rgb> land(pixelCount);
		std::vector<float> coverage(pixelCount, 0.0f);
		std::vector<float> depth(pixelCount, 0.0f);
		std::vector<terrain::WaterType> types(pixelCount, terrain::WaterType::None);
		std::vector<uint8> hole(pixelCount, 0);

		const auto half = [](const std::vector<uint16>& data, const size_t texel, const size_t channel)
		{
			return page_bake::HalfToFloat(data[texel * 4 + channel]);
		};

		for (uint32 py = 0; py < size; ++py)
		{
			for (uint32 px = 0; px < size; ++px)
			{
				const size_t i = static_cast<size_t>(py) * size + px;

				if (half(withModels.albedo, i, 3) <= 0.5f)
				{
					// Terrain hole (or nothing drawn): filled with the page's mean colour below.
					hole[i] = 1;
					continue;
				}

				// Relief lighting from the world-space G-buffer normal, plus whatever glows.
				const float light = ReliefLight(
					half(withModels.normal, i, 0) * 2.0f - 1.0f,
					half(withModels.normal, i, 1) * 2.0f - 1.0f,
					half(withModels.normal, i, 2) * 2.0f - 1.0f);
				land[i] = Rgb{
					half(withModels.albedo, i, 0) * light + half(withModels.emissive, i, 0),
					half(withModels.albedo, i, 1) * light + half(withModels.emissive, i, 1),
					half(withModels.albedo, i, 2) * light + half(withModels.emissive, i, 2) };

				if (water.quadMasks == nullptr)
				{
					continue;
				}

				const float localX = (static_cast<float>(px) + 0.5f) * metresPerPixel;
				const float localZ = (static_cast<float>(py) + 0.5f) * metresPerPixel;

				const float presence = WaterPresence(water, localX, localZ);
				if (presence <= 0.4f)
				{
					continue;
				}

				// World models (bridges, docks, ships) sit on top of the water.
				if (Differs(withModels.albedo, terrainOnly.albedo, i) || Differs(withModels.normal, terrainOnly.normal, i))
				{
					continue;
				}

				const float surface = WaterSurfaceHeightNearShore(water, localX, localZ);
				const float waterDepth = surface - page.GetSmoothHeightAt(localX, localZ);

				coverage[i] = Smoothstep(0.42f, 0.58f, presence) * Smoothstep(-0.12f, 0.08f, waterDepth);
				depth[i] = waterDepth;

				const uint32 tileX = std::min(static_cast<uint32>(localX / static_cast<float>(terrain::constants::TileSize)), terrain::constants::TilesPerPage - 1);
				const uint32 tileZ = std::min(static_cast<uint32>(localZ / static_cast<float>(terrain::constants::TileSize)), terrain::constants::TilesPerPage - 1);
				types[i] = terrain::water_lookup::TypeAtTile(water, tileX, tileZ);
			}
		}

		// Distances to the shoreline from both sides, for foam on the water and wet banks on land.
		std::vector<uint8> isWater(pixelCount);
		std::vector<uint8> isDry(pixelCount);
		bool anyWater = false;
		for (size_t i = 0; i < pixelCount; ++i)
		{
			isWater[i] = coverage[i] > 0.5f ? 1 : 0;
			isDry[i] = isWater[i] ? 0 : 1;
			anyWater = anyWater || isWater[i];
		}

		std::vector<float> toWater;
		std::vector<float> toLand;
		std::vector<int32> nearestWater;
		if (anyWater)
		{
			toWater = ChamferDistance(isWater, size, &nearestWater);
			toLand = ChamferDistance(isDry, size);
		}

		srgb.resize(pixelCount * 4);
		uint64 sum[3] = {};
		uint32 covered = 0;

		for (uint32 py = 0; py < size; ++py)
		{
			for (uint32 px = 0; px < size; ++px)
			{
				const size_t i = static_cast<size_t>(py) * size + px;
				if (hole[i])
				{
					continue;
				}

				Rgb color = land[i];

				if (anyWater)
				{
					// Land within reach of a liquid gets its bank treatment (wet sand, scorched rock).
					const int32 nearest = nearestWater[i];
					if (nearest >= 0 && !isWater[i])
					{
						color = ShadeBank(color, LookFor(types[static_cast<size_t>(nearest)]), toWater[i] * metresPerPixel);
					}

					if (coverage[i] > 0.0f)
					{
						const terrain::WaterType type = types[i];
						const float worldX = pageOrigin.x + (static_cast<float>(px) + 0.5f) * metresPerPixel;
						const float worldZ = pageOrigin.z + (static_cast<float>(py) + 0.5f) * metresPerPixel;
						const float shoreDistance = toLand[i] == std::numeric_limits<float>::max() ? 1.0e4f : toLand[i] * metresPerPixel;
						const Rgb liquid = ShadeWater(LookFor(type), land[i], type, depth[i], shoreDistance, worldX, worldZ);
						color = Lerp(color, liquid, coverage[i]);
					}
				}

				srgb[i * 4 + 0] = static_cast<uint8>(std::lround(LinearToSrgb(color.r) * 255.0f));
				srgb[i * 4 + 1] = static_cast<uint8>(std::lround(LinearToSrgb(color.g) * 255.0f));
				srgb[i * 4 + 2] = static_cast<uint8>(std::lround(LinearToSrgb(color.b) * 255.0f));
				srgb[i * 4 + 3] = 255;

				sum[0] += srgb[i * 4 + 0];
				sum[1] += srgb[i * 4 + 1];
				sum[2] += srgb[i * 4 + 2];
				++covered;
			}
		}

		// Holes draw nothing; give them the page's mean colour instead of black spots.
		for (size_t i = 0; i < pixelCount; ++i)
		{
			if (!hole[i])
			{
				continue;
			}

			for (uint32 c = 0; c < 3; ++c)
			{
				srgb[i * 4 + c] = covered > 0 ? static_cast<uint8>(sum[c] / covered) : 0;
			}

			srgb[i * 4 + 3] = 255;
		}
	}
}
