// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "world_map.h"

#include "client_locale.h"

#include "assets/asset_registry.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "client_data/zone_names.h"
#include "frame_ui/geometry_helper.h"
#include "frame_ui/mouse_event_args.h"
#include "graphics/graphics_device.h"
#include "graphics/texture_mgr.h"
#include "log/default_log_levels.h"
#include "math/matrix4.h"
#include "math/quaternion.h"

#include "luabind_lambda.h"
#include "luabind/luabind.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace mmo
{
	namespace
	{
		/// Colour behind the map: the sea of the art, so unbaked areas read as open water.
		constexpr argb_t SeaColor = 0xFF2B3F4A;

		/// Tile kinds in TileKey.
		constexpr uint32 ArtTileKind = 1;
		constexpr uint32 MinimapTileKind = 2;

		/// Tiles loaded per frame at most (texture loads are synchronous).
		constexpr size_t LoadsPerFrame = 4;

		/// Seconds a freshly loaded tile takes to fade in.
		constexpr float TileFadeSeconds = 0.25f;

		/// Seconds a click glide takes.
		constexpr float GlideSeconds = 0.45f;

		/// Zoom factor per mouse wheel notch.
		constexpr double WheelZoomFactor = 1.3;

		/// Pixels the mouse may move between press and release for the press to count as a click.
		constexpr float ClickSlop = 5.0f;

		constexpr argb_t ZoneLabelColor = 0x00F4DDA6;
		constexpr argb_t SubZoneLabelColor = 0x00EDE6D2;
		constexpr argb_t TitleLabelColor = 0x00F8E7BE;

		argb_t WithAlpha(const argb_t rgb, const float alpha)
		{
			const uint32 a = static_cast<uint32>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
			return (a << 24) | (rgb & 0x00FFFFFF);
		}

		bool RectsOverlap(const Rect& a, const Rect& b)
		{
			return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
		}
	}

	WorldMap::WorldMap(const proto_client::ZoneManager& zones)
		: m_zones(zones)
		, m_playerFacing(0.0f)
	{
		m_playerArrowTexture = TextureManager::Get().CreateOrRetrieve("Interface/Icons/fg4_iconsBrown_left_result.htex");
		if (m_playerArrowTexture)
		{
			m_playerArrowTexture->SetTextureAddressMode(TextureAddressMode::Clamp);
		}

		m_whiteTexture = TextureManager::Get().CreateManual("__WorldMapWhite__", 1, 1, PixelFormat::R8G8B8A8, BufferUsage::DynamicWriteOnly);
		if (m_whiteTexture)
		{
			uint32 white = 0xFFFFFFFF;
			m_whiteTexture->LoadRaw(&white, sizeof(white));
		}

		m_dotTexture = TextureManager::Get().CreateOrRetrieve("Interface/Icons/fg4_iconsFlat_bullet_result.htex");
		if (m_dotTexture)
		{
			m_dotTexture->SetTextureAddressMode(TextureAddressMode::Clamp);
		}
	}

	WorldMap::~WorldMap()
	{
		m_tiles.clear();
		m_renderTexture.reset();
	}

	void WorldMap::RegisterScriptFunctions(lua_State* luaState)
	{
		LUABIND_MODULE(luaState,
			luabind::def_lambda("WorldMapHasMap", [this]() { return HasMap(); }),
			luabind::def_lambda("WorldMapGetMapName", [this]() { return m_mapName; }),
			luabind::def_lambda("WorldMapCenterOnPlayer", [this]() { CenterOnPlayer(true); }),
			luabind::def_lambda("WorldMapShowWholeMap", [this]() { ShowWholeMap(true); }),
			luabind::def_lambda("WorldMapZoomIn", [this]() { ZoomSteps(1); }),
			luabind::def_lambda("WorldMapZoomOut", [this]() { ZoomSteps(-1); }),
			luabind::def<std::function<luabind::object(lua_State*)>>("WorldMapGetHoverInfo", [this](lua_State* L) { return GetHoverInfo(L); }),
			luabind::def<std::function<luabind::object(lua_State*)>>("WorldMapGetPlayerInfo", [this](lua_State* L) { return GetPlayerInfo(L); })
		);
	}

	void WorldMap::NotifyWorldChanged(const String& mapDirectory, const String& mapName)
	{
		m_mapName = mapName;
		if (m_mapDirectory == mapDirectory)
		{
			return;
		}

		m_mapDirectory = mapDirectory;
		m_tiles.clear();
		m_missingTiles.clear();
		m_pendingLoads.clear();
		m_members.clear();
		m_highlightMask.reset();
		m_highlightZone = 0;
		m_hoverZone = 0;
		m_viewInitialized = false;
		m_gliding = false;
		m_hasZoomAnchor = false;
		m_hasMap = false;
		m_info = world_map::WorldMapInfo();

		const String filename = world_map::GetInfoFilename(mapDirectory);
		if (!AssetRegistry::HasFile(filename))
		{
			DLOG("No world map for map directory " << mapDirectory);
			return;
		}

		const std::unique_ptr<std::istream> stream = AssetRegistry::OpenFile(filename);
		if (!stream)
		{
			return;
		}

		io::StreamSource source(*stream);
		io::Reader reader(source);
		if (!(reader >> m_info))
		{
			ELOG("Failed to read world map " << filename);
			m_info = world_map::WorldMapInfo();
			return;
		}

		// Labels and the hover highlight work on top level zones: merge every sub zone into its root
		m_rootInfo = m_info;
		m_rootInfo.RemapCells([this](const uint32 zoneId) { return GetRootZone(zoneId); });
		m_rootInfo.RebuildZones();

		m_hasMap = true;
		ILOG("World map of " << mapDirectory << " loaded: " << m_info.zones.size() << " zones, art levels up to " << m_info.maxArtLevel);
	}

	void WorldMap::UpdatePlayerPosition(const Vector3& position, const Radian& facing)
	{
		m_playerPosition = position;
		m_playerFacing = facing;
	}

	void WorldMap::UpdateMemberDots(std::vector<MapMemberDot> members)
	{
		m_members = std::move(members);
	}

	void WorldMap::SetFonts(FontPtr titleFont, FontPtr zoneFont, FontPtr subZoneFont)
	{
		m_titleFont = std::move(titleFont);
		m_zoneFont = std::move(zoneFont);
		m_subZoneFont = std::move(subZoneFont);
	}

	uint32 WorldMap::GetRootZone(uint32 zoneId) const
	{
		// Parent chains are short; the bound guards against cycles in the data
		for (int32 depth = 0; depth < 8 && zoneId != 0; ++depth)
		{
			const proto_client::ZoneEntry* zone = m_zones.getById(zoneId);
			if (!zone || zone->parentzone() == 0 || zone->parentzone() == zoneId)
			{
				return zoneId;
			}

			zoneId = zone->parentzone();
		}

		return zoneId;
	}

	String WorldMap::GetZoneName(const uint32 zoneId) const
	{
		const proto_client::ZoneEntry* zone = m_zones.getById(zoneId);
		return zone ? GetZoneDisplayName(*zone, GetClientLocale()) : String();
	}

	double WorldMap::GetZoneFitZoom(const world_map::ZoneInfo& zone) const
	{
		return m_view.FitUnitsPerPixel(zone.bounds, 1.15);
	}

	void WorldMap::ClampViews()
	{
		if (!m_hasMap)
		{
			return;
		}

		const world_map::WorldRect bounds = m_info.GetBounds();
		world_map::ClampView(m_view, bounds, world_map::MinUnitsPerPixel);
		world_map::ClampView(m_target, bounds, world_map::MinUnitsPerPixel);
	}

	void WorldMap::SetTarget(const double centerX, const double centerZ, const double unitsPerPixel, const bool animate)
	{
		m_target = m_view;
		m_target.centerX = centerX;
		m_target.centerZ = centerZ;
		m_target.unitsPerPixel = unitsPerPixel;
		if (m_hasMap)
		{
			world_map::ClampView(m_target, m_info.GetBounds(), world_map::MinUnitsPerPixel);
		}

		m_hasZoomAnchor = false;
		if (animate)
		{
			m_gliding = true;
			m_glideTime = 0.0f;
			m_glideFrom = m_view;
		}
		else
		{
			m_gliding = false;
			m_view = m_target;
		}
	}

	void WorldMap::GlideTo(const world_map::WorldRect& rect, const bool animate)
	{
		SetTarget(rect.CenterX(), rect.CenterZ(), m_view.FitUnitsPerPixel(rect, 1.15), animate);
	}

	void WorldMap::CenterOnPlayer(const bool animate)
	{
		if (!m_hasMap)
		{
			return;
		}

		// A regional zoom: the player's zone with its neighbourhood, in map art rather than minimap detail
		double unitsPerPixel = 6.0;
		const uint32 zoneId = GetRootZone(m_info.GetZoneAt(m_playerPosition.x, m_playerPosition.z));
		if (const world_map::ZoneInfo* zone = m_rootInfo.FindZone(zoneId))
		{
			unitsPerPixel = std::clamp(GetZoneFitZoom(*zone) * 2.0, 4.0, 12.0);
		}

		SetTarget(m_playerPosition.x, m_playerPosition.z, unitsPerPixel, animate);
	}

	void WorldMap::ShowWholeMap(const bool animate)
	{
		if (m_hasMap)
		{
			GlideTo(m_info.GetBounds(), animate);
		}
	}

	void WorldMap::ZoomSteps(const int32 steps)
	{
		const double unitsPerPixel = m_target.unitsPerPixel * std::pow(1.0 / (WheelZoomFactor * WheelZoomFactor), steps);
		SetTarget(m_target.centerX, m_target.centerZ, unitsPerPixel, true);
	}

	void WorldMap::OnMouseDown(const int32 button, const float x, const float y)
	{
		m_dragging = true;
		m_dragMoved = false;
		m_dragButton = button;
		m_dragStartX = x;
		m_dragStartY = y;
		m_lastMouseX = x;
		m_lastMouseY = y;
	}

	void WorldMap::OnMouseUp(const int32 button, const float x, const float y)
	{
		const bool click = m_dragging && !m_dragMoved && button == m_dragButton;
		m_dragging = false;
		if (!click || !m_hasMap)
		{
			return;
		}

		const double worldX = m_view.ScreenToWorldX(x);
		const double worldZ = m_view.ScreenToWorldZ(y);
		const uint32 cellZone = m_info.GetZoneAt(worldX, worldZ);
		const uint32 rootZone = GetRootZone(cellZone);
		const world_map::ZoneInfo* root = m_rootInfo.FindZone(rootZone);

		if (button == static_cast<int32>(MouseButton::Left))
		{
			// Into the zone, then into its sub zone, then closer about the cursor
			if (root && m_view.unitsPerPixel > GetZoneFitZoom(*root) * 1.15)
			{
				GlideTo(root->bounds, true);
				return;
			}

			const world_map::ZoneInfo* subZone = cellZone != rootZone ? m_info.FindZone(cellZone) : nullptr;
			if (subZone && m_view.unitsPerPixel > GetZoneFitZoom(*subZone) * 1.15)
			{
				GlideTo(subZone->bounds, true);
				return;
			}

			world_map::View zoomed = m_view;
			zoomed.ZoomAt(x, y, m_view.unitsPerPixel / 2.5);
			SetTarget(zoomed.centerX, zoomed.centerZ, zoomed.unitsPerPixel, true);
		}
		else if (button == static_cast<int32>(MouseButton::Right))
		{
			// Out to the zone, then to the whole map
			if (root && m_view.unitsPerPixel < GetZoneFitZoom(*root) * 0.87)
			{
				GlideTo(root->bounds, true);
				return;
			}

			ShowWholeMap(true);
		}
	}

	void WorldMap::OnMouseMoved(const float x, const float y)
	{
		m_mouseInside = true;
		if (m_dragging)
		{
			if (std::abs(x - m_dragStartX) > ClickSlop || std::abs(y - m_dragStartY) > ClickSlop)
			{
				m_dragMoved = true;
			}

			if (m_dragMoved && m_dragButton == static_cast<int32>(MouseButton::Left))
			{
				m_view.centerX -= (x - m_lastMouseX) * m_view.unitsPerPixel;
				m_view.centerZ -= (y - m_lastMouseY) * m_view.unitsPerPixel;
				m_gliding = false;
				m_hasZoomAnchor = false;
				const double targetZoom = m_target.unitsPerPixel;
				m_target = m_view;
				m_target.unitsPerPixel = targetZoom;
				ClampViews();
			}
		}

		m_lastMouseX = x;
		m_lastMouseY = y;
	}

	void WorldMap::OnMouseWheel(const int32 delta)
	{
		if (!m_hasMap || delta == 0)
		{
			return;
		}

		// Ease towards the new zoom, keeping the world point under the cursor in place
		m_gliding = false;
		const double sx = m_mouseInside ? m_lastMouseX : m_view.width * 0.5;
		const double sy = m_mouseInside ? m_lastMouseY : m_view.height * 0.5;
		if (!m_hasZoomAnchor || std::abs(sx - m_anchorScreenX) > 1.0 || std::abs(sy - m_anchorScreenY) > 1.0)
		{
			m_anchorScreenX = sx;
			m_anchorScreenY = sy;
			m_anchorWorldX = m_view.ScreenToWorldX(sx);
			m_anchorWorldZ = m_view.ScreenToWorldZ(sy);
		}
		m_hasZoomAnchor = true;

		m_target.unitsPerPixel = m_target.unitsPerPixel * std::pow(1.0 / WheelZoomFactor, delta);
		m_target.unitsPerPixel = std::max(m_target.unitsPerPixel, world_map::MinUnitsPerPixel);
		m_target.centerX = m_anchorWorldX - (m_anchorScreenX - m_view.width * 0.5) * m_target.unitsPerPixel;
		m_target.centerZ = m_anchorWorldZ - (m_anchorScreenY - m_view.height * 0.5) * m_target.unitsPerPixel;
		world_map::ClampView(m_target, m_info.GetBounds(), world_map::MinUnitsPerPixel);
	}

	void WorldMap::OnMouseLeave()
	{
		m_mouseInside = false;
		m_hoverZone = 0;
	}

	void WorldMap::Update(const float elapsedSeconds)
	{
		m_time += elapsedSeconds;
		if (!m_hasMap || !m_viewInitialized)
		{
			return;
		}

		if (m_gliding)
		{
			m_glideTime += elapsedSeconds;
			const float s = world_map::SmoothStep(0.0f, 1.0f, std::min(m_glideTime / GlideSeconds, 1.0f));
			const double logFrom = std::log(m_glideFrom.unitsPerPixel);
			const double logTo = std::log(m_target.unitsPerPixel);
			m_view.unitsPerPixel = std::exp(logFrom + (logTo - logFrom) * s);

			// Move the centre with the zoom (not with time), which reads as zooming about one point
			const double z0 = 1.0 / m_glideFrom.unitsPerPixel;
			const double z1 = 1.0 / m_target.unitsPerPixel;
			const double z = 1.0 / m_view.unitsPerPixel;
			const double w = std::abs(z1 - z0) > 1e-9 ? (z - z0) / (z1 - z0) : s;
			m_view.centerX = m_glideFrom.centerX + (m_target.centerX - m_glideFrom.centerX) * w;
			m_view.centerZ = m_glideFrom.centerZ + (m_target.centerZ - m_glideFrom.centerZ) * w;

			if (s >= 1.0f)
			{
				m_gliding = false;
				m_view = m_target;
			}
		}
		else
		{
			// Exponential ease in log space
			const double k = 1.0 - std::exp(-static_cast<double>(elapsedSeconds) * 14.0);
			const double logView = std::log(m_view.unitsPerPixel);
			const double logTarget = std::log(m_target.unitsPerPixel);
			m_view.unitsPerPixel = std::exp(logView + (logTarget - logView) * k);
			if (std::abs(logTarget - logView) < 0.0005)
			{
				m_view.unitsPerPixel = m_target.unitsPerPixel;
			}

			if (m_hasZoomAnchor)
			{
				m_view.centerX = m_anchorWorldX - (m_anchorScreenX - m_view.width * 0.5) * m_view.unitsPerPixel;
				m_view.centerZ = m_anchorWorldZ - (m_anchorScreenY - m_view.height * 0.5) * m_view.unitsPerPixel;
				if (m_view.unitsPerPixel == m_target.unitsPerPixel)
				{
					m_hasZoomAnchor = false;
				}
			}
			else
			{
				m_view.centerX += (m_target.centerX - m_view.centerX) * k;
				m_view.centerZ += (m_target.centerZ - m_view.centerZ) * k;
			}
		}

		ClampViews();

		for (auto& [key, tile] : m_tiles)
		{
			if (tile.texture && tile.fade < 1.0f)
			{
				tile.fade = std::min(1.0f, tile.fade + elapsedSeconds / TileFadeSeconds);
			}
		}

		// Hovered zone, faded in and out
		if (m_mouseInside && !m_dragMoved)
		{
			m_hoverZone = GetRootZone(m_info.GetZoneAt(m_view.ScreenToWorldX(m_lastMouseX), m_view.ScreenToWorldZ(m_lastMouseY)));
		}

		if (m_hoverZone != 0 && m_hoverZone != m_highlightZone)
		{
			if (m_highlightAlpha <= 0.01f || m_highlightZone == 0)
			{
				BuildHighlightMask(m_hoverZone);
			}
			else
			{
				m_highlightAlpha = std::max(0.0f, m_highlightAlpha - elapsedSeconds * 8.0f);
			}
		}
		else
		{
			const float goal = m_hoverZone != 0 ? 1.0f : 0.0f;
			const float step = elapsedSeconds * 6.0f;
			m_highlightAlpha = goal > m_highlightAlpha ? std::min(goal, m_highlightAlpha + step) : std::max(goal, m_highlightAlpha - step);
		}
	}

	void WorldMap::BuildHighlightMask(const uint32 rootZoneId)
	{
		m_highlightZone = rootZoneId;
		m_highlightAlpha = 0.0f;

		const uint32 cellsX = m_rootInfo.GetCellsX();
		const uint32 cellsZ = m_rootInfo.GetCellsZ();
		if (cellsX == 0 || cellsZ == 0 || cellsX > 4096 || cellsZ > 4096)
		{
			return;
		}

		if (!m_highlightMask)
		{
			m_highlightMask = TextureManager::Get().CreateManual("__WorldMapHighlight__", static_cast<uint16>(cellsX), static_cast<uint16>(cellsZ),
				PixelFormat::R8G8B8A8, BufferUsage::DynamicWriteOnly);
			if (!m_highlightMask)
			{
				return;
			}

			m_highlightMask->SetTextureAddressMode(TextureAddressMode::Clamp);
		}

		const std::vector<uint32>& cells = m_rootInfo.GetCells();
		std::vector<uint32> pixels(cells.size());
		for (size_t i = 0; i < cells.size(); ++i)
		{
			pixels[i] = cells[i] == rootZoneId ? 0xFFFFFFFFu : 0x00FFFFFFu;
		}

		m_highlightMask->LoadRaw(pixels.data(), pixels.size() * sizeof(uint32));
	}

	WorldMap::CachedTile* WorldMap::GetTile(const uint64 key, const String& filename, const bool request, const float priority)
	{
		if (const auto it = m_tiles.find(key); it != m_tiles.end())
		{
			it->second.lastUsed = m_time;
			return &it->second;
		}

		if (request && !m_missingTiles.contains(key))
		{
			const auto pending = std::find_if(m_pendingLoads.begin(), m_pendingLoads.end(), [key](const PendingLoad& load) { return load.key == key; });
			if (pending == m_pendingLoads.end())
			{
				m_pendingLoads.push_back({ key, filename, priority });
			}
		}

		return nullptr;
	}

	void WorldMap::LoadQueuedTiles()
	{
		std::sort(m_pendingLoads.begin(), m_pendingLoads.end(), [](const PendingLoad& a, const PendingLoad& b) { return a.priority < b.priority; });

		size_t loaded = 0;
		for (const PendingLoad& load : m_pendingLoads)
		{
			if (loaded >= LoadsPerFrame)
			{
				break;
			}

			if (!AssetRegistry::HasFile(load.filename))
			{
				m_missingTiles[load.key] = true;
				continue;
			}

			TexturePtr texture = TextureManager::Get().CreateOrRetrieve(load.filename);
			++loaded;
			if (!texture)
			{
				m_missingTiles[load.key] = true;
				continue;
			}

			texture->SetTextureAddressMode(TextureAddressMode::Clamp);
			CachedTile& tile = m_tiles[load.key];
			tile.texture = std::move(texture);
			tile.fade = 0.0f;
			tile.lastUsed = m_time;
		}

		// Whatever is still visible next frame asks again, with fresh priorities
		m_pendingLoads.clear();
	}

	void WorldMap::EvictTiles()
	{
		constexpr size_t MaxCachedTiles = 320;
		if (m_tiles.size() <= MaxCachedTiles)
		{
			return;
		}

		for (auto it = m_tiles.begin(); it != m_tiles.end();)
		{
			const uint32 level = static_cast<uint32>((it->first >> 32) & 0xFFFF);
			const bool baseLevel = (it->first >> 48) == ArtTileKind && level == m_info.maxArtLevel;
			if (!baseLevel && it->second.lastUsed < m_time - 3.0f)
			{
				it = m_tiles.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void WorldMap::ReleaseTiles()
	{
		for (auto it = m_tiles.begin(); it != m_tiles.end();)
		{
			const uint32 level = static_cast<uint32>((it->first >> 32) & 0xFFFF);
			if ((it->first >> 48) == ArtTileKind && level == m_info.maxArtLevel)
			{
				++it;
			}
			else
			{
				it = m_tiles.erase(it);
			}
		}

		m_dragging = false;
		m_mouseInside = false;
		m_hoverZone = 0;
	}

	void WorldMap::AddWorldQuad(const TexturePtr& texture, const world_map::WorldRect& rect, const float alpha)
	{
		const float left = static_cast<float>(rect.minX);
		const float right = static_cast<float>(rect.maxX);
		const float top = static_cast<float>(rect.minZ);
		const float bottom = static_cast<float>(rect.maxZ);
		const uint32 color = Color(1.0f, 1.0f, 1.0f, std::clamp(alpha, 0.0f, 1.0f)).GetABGR();

		m_buffer.SetActiveTexture(texture);
		const GeometryBuffer::Vertex vertices[6]{
			{ { left, bottom, 0.0f }, color, { 0.0f, 1.0f } },
			{ { left, top, 0.0f }, color, { 0.0f, 0.0f } },
			{ { right, top, 0.0f }, color, { 1.0f, 0.0f } },
			{ { right, top, 0.0f }, color, { 1.0f, 0.0f } },
			{ { right, bottom, 0.0f }, color, { 1.0f, 1.0f } },
			{ { left, bottom, 0.0f }, color, { 0.0f, 1.0f } }
		};
		m_buffer.AppendGeometry(vertices, 6);
	}

	void WorldMap::DrawArtLevel(const uint32 level, const bool request, const float alpha)
	{
		const world_map::WorldRect visible = m_view.GetVisibleRect();
		const double span = world_map::PageSize * static_cast<double>(1u << level);
		const double origin = world_map::GridOrigin();
		const int32 tilesPerSide = static_cast<int32>(world_map::MaxPages >> level);

		const int32 minX = std::max(0, static_cast<int32>(std::floor((visible.minX - origin) / span)));
		const int32 maxX = std::min(tilesPerSide - 1, static_cast<int32>(std::floor((visible.maxX - origin) / span)));
		const int32 minZ = std::max(0, static_cast<int32>(std::floor((visible.minZ - origin) / span)));
		const int32 maxZ = std::min(tilesPerSide - 1, static_cast<int32>(std::floor((visible.maxZ - origin) / span)));

		for (int32 z = minZ; z <= maxZ; ++z)
		{
			for (int32 x = minX; x <= maxX; ++x)
			{
				if (!m_info.HasArtTile(level, x, z))
				{
					continue;
				}

				world_map::WorldRect rect;
				rect.minX = origin + x * span;
				rect.minZ = origin + z * span;
				rect.maxX = rect.minX + span;
				rect.maxZ = rect.minZ + span;

				const double dx = rect.CenterX() - m_view.centerX;
				const double dz = rect.CenterZ() - m_view.centerZ;
				const float priority = static_cast<float>(level) * 1000.0f + static_cast<float>(std::sqrt(dx * dx + dz * dz) / span);

				const uint64 key = TileKey(ArtTileKind, level, x, z);
				if (CachedTile* tile = GetTile(key, world_map::GetArtTileFilename(m_mapDirectory, level, x, z), request, priority); tile && tile->texture)
				{
					AddWorldQuad(tile->texture, rect, alpha * tile->fade);
				}
			}
		}
	}

	void WorldMap::DrawMinimapTiles(const float alpha)
	{
		const world_map::WorldRect visible = m_view.GetVisibleRect();
		const double origin = world_map::GridOrigin();
		const int32 maxPage = static_cast<int32>(world_map::MaxPages) - 1;

		const int32 minX = std::max(0, static_cast<int32>(std::floor((visible.minX - origin) / world_map::PageSize)));
		const int32 maxX = std::min(maxPage, static_cast<int32>(std::floor((visible.maxX - origin) / world_map::PageSize)));
		const int32 minZ = std::max(0, static_cast<int32>(std::floor((visible.minZ - origin) / world_map::PageSize)));
		const int32 maxZ = std::min(maxPage, static_cast<int32>(std::floor((visible.maxZ - origin) / world_map::PageSize)));

		for (int32 z = minZ; z <= maxZ; ++z)
		{
			for (int32 x = minX; x <= maxX; ++x)
			{
				if (!m_info.HasPage(x, z))
				{
					continue;
				}

				world_map::WorldRect rect;
				rect.minX = origin + x * world_map::PageSize;
				rect.minZ = origin + z * world_map::PageSize;
				rect.maxX = rect.minX + world_map::PageSize;
				rect.maxZ = rect.minZ + world_map::PageSize;

				// Minimap tiles load before art tiles (priority below level 1's)
				const double dx = rect.CenterX() - m_view.centerX;
				const double dz = rect.CenterZ() - m_view.centerZ;
				const float priority = static_cast<float>(std::sqrt(dx * dx + dz * dz) / world_map::PageSize);

				const uint64 key = TileKey(MinimapTileKind, 0, x, z);
				if (CachedTile* tile = GetTile(key, world_map::GetMinimapTileFilename(m_mapDirectory, x, z), true, priority); tile && tile->texture)
				{
					AddWorldQuad(tile->texture, rect, alpha * tile->fade);
				}
			}
		}
	}

	void WorldMap::DrawZoneHighlight()
	{
		if (!m_highlightMask || m_highlightZone == 0 || m_highlightAlpha <= 0.0f)
		{
			return;
		}

		const world_map::ZoneInfo* zone = m_rootInfo.FindZone(m_highlightZone);
		if (!zone)
		{
			return;
		}

		// Once the zone fills the view, highlighting it says nothing anymore
		const double zonePixels = std::max(zone->bounds.Width(), zone->bounds.Height()) / m_view.unitsPerPixel;
		const float fill = static_cast<float>(zonePixels / std::max(m_view.width, m_view.height));
		const float alpha = m_highlightAlpha * 0.2f * (1.0f - world_map::SmoothStep(1.3f, 2.6f, fill));
		if (alpha <= 0.0f)
		{
			return;
		}

		const world_map::WorldRect bounds = m_info.GetBounds();
		const float left = static_cast<float>(bounds.minX);
		const float right = static_cast<float>(bounds.maxX);
		const float top = static_cast<float>(bounds.minZ);
		const float bottom = static_cast<float>(bounds.maxZ);
		const uint32 color = Color(1.0f, 0.86f, 0.55f, alpha).GetABGR();

		m_buffer.SetActiveTexture(m_highlightMask);
		const GeometryBuffer::Vertex vertices[6]{
			{ { left, bottom, 0.0f }, color, { 0.0f, 1.0f } },
			{ { left, top, 0.0f }, color, { 0.0f, 0.0f } },
			{ { right, top, 0.0f }, color, { 1.0f, 0.0f } },
			{ { right, top, 0.0f }, color, { 1.0f, 0.0f } },
			{ { right, bottom, 0.0f }, color, { 1.0f, 1.0f } },
			{ { left, bottom, 0.0f }, color, { 0.0f, 1.0f } }
		};
		m_buffer.AppendGeometry(vertices, 6);
	}

	void WorldMap::DrawLabels(const float textScale)
	{
		std::vector<Label> labels;
		const double viewExtent = std::max(m_view.width, m_view.height);
		const double wholeMapZoom = m_view.FitUnitsPerPixel(m_info.GetBounds(), 1.1);

		// The map's name while (nearly) all of it is in view
		const float titleAlpha = world_map::SmoothStep(0.45f, 0.8f, static_cast<float>(m_view.unitsPerPixel / wholeMapZoom));
		if (m_titleFont && titleAlpha > 0.01f && !m_mapName.empty())
		{
			const world_map::WorldRect bounds = m_info.GetBounds();
			labels.push_back({ m_mapName, bounds.CenterX(), bounds.minZ + bounds.Height() * 0.06, titleAlpha, m_titleFont.get(), TitleLabelColor, 3000000 });
		}

		// Zones grow in as they get large on screen, and fade once they fill the view
		const auto addZoneLabels = [&](const std::vector<world_map::ZoneInfo>& zones, const bool subZones)
		{
			const Font* font = subZones ? m_subZoneFont.get() : m_zoneFont.get();
			if (!font)
			{
				return;
			}

			for (const world_map::ZoneInfo& zone : zones)
			{
				const uint32 root = GetRootZone(zone.zoneId);
				if (subZones == (root == zone.zoneId))
				{
					continue;
				}

				const float pixels = static_cast<float>(std::max(zone.bounds.Width(), zone.bounds.Height()) / m_view.unitsPerPixel);
				const float extent = static_cast<float>(viewExtent);
				float alpha = world_map::SmoothStep(110.0f * textScale, 220.0f * textScale, pixels)
					* (1.0f - world_map::SmoothStep(extent * 1.2f, extent * 2.2f, pixels));

				if (subZones)
				{
					// Sub zones appear once their zone is the focus
					if (const world_map::ZoneInfo* parent = m_rootInfo.FindZone(root))
					{
						const float parentPixels = static_cast<float>(std::max(parent->bounds.Width(), parent->bounds.Height()) / m_view.unitsPerPixel);
						alpha *= world_map::SmoothStep(extent * 0.6f, extent * 1.0f, parentPixels);
					}
				}

				if (alpha <= 0.01f)
				{
					continue;
				}

				String name = GetZoneName(zone.zoneId);
				if (name.empty())
				{
					continue;
				}

				labels.push_back({ std::move(name), zone.labelX, zone.labelZ, alpha, font,
					subZones ? SubZoneLabelColor : ZoneLabelColor,
					(subZones ? 0 : 2000000) + static_cast<int32>(std::min<uint32>(zone.cellCount, 1000000)) });
			}
		};

		addZoneLabels(m_rootInfo.zones, false);
		addZoneLabels(m_info.zones, true);

		std::stable_sort(labels.begin(), labels.end(), [](const Label& a, const Label& b) { return a.priority > b.priority; });

		// Place the most important labels first; one that would overlap a placed label is left out
		std::vector<Rect> placed;
		const float shadow = std::max(1.0f, 2.0f * textScale);
		for (const Label& label : labels)
		{
			Font& font = *const_cast<Font*>(label.font);
			const float width = font.GetTextWidth(label.text, textScale);
			const float height = font.GetHeight(textScale);
			const float sx = static_cast<float>(m_view.WorldToScreenX(label.x)) - width * 0.5f;
			const float sy = static_cast<float>(m_view.WorldToScreenZ(label.z)) - height * 0.5f;
			const Rect rect(sx - 6.0f, sy - 2.0f, sx + width + 6.0f, sy + height + 2.0f);
			// Only labels that fit the view entirely: a name cut off at the edge reads as a different word
			if (rect.left < 0.0f || rect.top < 0.0f || rect.right > m_view.width || rect.bottom > m_view.height)
			{
				continue;
			}

			if (std::any_of(placed.begin(), placed.end(), [&rect](const Rect& other) { return RectsOverlap(rect, other); }))
			{
				continue;
			}
			placed.push_back(rect);

			// A dark rim keeps the ink readable on any part of the art
			const argb_t rim = WithAlpha(0x00140F0A, label.alpha * 0.85f);
			for (const Point offset : { Point(-shadow, 0.0f), Point(shadow, 0.0f), Point(0.0f, -shadow), Point(0.0f, shadow), Point(shadow, shadow) })
			{
				font.DrawText(label.text, Point(sx + offset.x, sy + offset.y), m_buffer, textScale, rim);
			}
			font.DrawText(label.text, Point(sx, sy), m_buffer, textScale, WithAlpha(label.color, label.alpha));
		}
	}

	void WorldMap::DrawMarkers(const float textScale)
	{
		if (m_dotTexture)
		{
			m_buffer.SetActiveTexture(m_dotTexture);
			for (const bool guildPass : { true, false })
			{
				for (const MapMemberDot& member : m_members)
				{
					if (member.guild != guildPass)
					{
						continue;
					}

					const float sx = static_cast<float>(m_view.WorldToScreenX(member.position.x));
					const float sy = static_cast<float>(m_view.WorldToScreenZ(member.position.z));
					const float half = (member.guild ? 13.0f : 16.0f) * textScale;
					const argb_t color = member.guild ? 0xFF4DF266 : 0xFF66A0FF;
					GeometryHelper::CreateRect(m_buffer, 0xC0000000, Rect(sx - half * 1.2f, sy - half * 1.2f, sx + half * 1.2f, sy + half * 1.2f), Rect(0.0f, 0.0f, 1.0f, 1.0f), 1, 1);
					GeometryHelper::CreateRect(m_buffer, color, Rect(sx - half, sy - half, sx + half, sy + half), Rect(0.0f, 0.0f, 1.0f, 1.0f), 1, 1);
				}
			}
		}
	}

	void WorldMap::Render(const uint16 width, const uint16 height, const float textScale)
	{
		if (width == 0 || height == 0)
		{
			return;
		}

		GraphicsDevice& gx = GraphicsDevice::Get();
		if (!m_renderTexture || width != m_textureWidth || height != m_textureHeight)
		{
			m_renderTexture = gx.CreateRenderTexture("WorldMap" + std::to_string(++m_textureGeneration), width, height,
				RenderTextureFlags::HasColorBuffer | RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView,
				R8G8B8A8, D32F);
			m_textureWidth = width;
			m_textureHeight = height;
			if (!m_renderTexture)
			{
				return;
			}
		}

		// Keep the zoom when the window is resized: only the visible area changes
		m_view.width = width;
		m_view.height = height;
		m_target.width = width;
		m_target.height = height;
		if (m_hasMap && !m_viewInitialized)
		{
			m_viewInitialized = true;
			m_view.unitsPerPixel = m_view.FitUnitsPerPixel(m_info.GetBounds(), 1.1);
			CenterOnPlayer(false);
		}
		ClampViews();

		gx.CaptureState();
		m_renderTexture->Activate();
		m_renderTexture->Clear(ClearFlags::All);

		const world_map::WorldRect visible = m_view.GetVisibleRect();
		gx.SetTransformMatrix(World, Matrix4::Identity);
		gx.SetTransformMatrix(View, Matrix4::Identity);
		gx.SetTransformMatrix(Projection, gx.MakeOrthographicMatrix(
			static_cast<float>(visible.minX), static_cast<float>(visible.minZ),
			static_cast<float>(visible.maxX), static_cast<float>(visible.maxZ), 0.1f, 1000.0f));

		m_buffer.Reset();
		m_buffer.SetActiveTexture(m_whiteTexture);
		GeometryHelper::CreateRect(m_buffer, SeaColor, Rect(static_cast<float>(visible.minX), static_cast<float>(visible.minZ), static_cast<float>(visible.maxX), static_cast<float>(visible.maxZ)), Rect(0.0f, 0.0f, 1.0f, 1.0f), 1, 1);

		if (m_hasMap)
		{
			// The coarsest level is always there underneath, so loading never shows holes. Coarser levels
			// that are still cached cover the finer one while it loads.
			const uint32 selected = world_map::SelectArtLevel(m_view.unitsPerPixel, m_info.maxArtLevel);
			const float minimapBlend = world_map::MinimapBlend(m_view.unitsPerPixel);
			DrawArtLevel(m_info.maxArtLevel, true, 1.0f);
			if (minimapBlend < 1.0f)
			{
				for (uint32 level = std::min(selected + 2, m_info.maxArtLevel); level > selected; --level)
				{
					if (level < m_info.maxArtLevel)
					{
						DrawArtLevel(level, false, 1.0f);
					}
				}
				if (selected < m_info.maxArtLevel)
				{
					DrawArtLevel(selected, true, 1.0f);
				}
			}

			if (minimapBlend > 0.001f)
			{
				DrawMinimapTiles(minimapBlend);
			}

			DrawZoneHighlight();
		}

		gx.SetTextureFilter(TextureFilter::Trilinear);
		m_buffer.Draw();

		// Labels and markers in screen pixels, at a constant size
		gx.SetTransformMatrix(Projection, gx.MakeOrthographicMatrix(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 100.0f));
		m_buffer.Reset();
		if (m_hasMap)
		{
			DrawLabels(textScale);
			DrawMarkers(textScale);
		}
		gx.SetTextureFilter(TextureFilter::Bilinear);
		m_buffer.Draw();

		// The player's arrow, rotated with the facing
		if (m_hasMap && m_playerArrowTexture)
		{
			const float half = 30.0f * textScale;
			m_buffer.Reset();
			m_buffer.SetActiveTexture(m_playerArrowTexture);
			GeometryHelper::CreateRect(m_buffer, Color::White, Rect(-half, -half, half, half), Rect(1.0f, 1.0f, 0.0f, 0.0f), 1, 1);

			Matrix4 world = Matrix4::Identity;
			world = world * Matrix4::GetTrans(Vector3(static_cast<float>(m_view.WorldToScreenX(m_playerPosition.x)), static_cast<float>(m_view.WorldToScreenZ(m_playerPosition.z)), 0.0f));
			world = world * Quaternion(m_playerFacing, Vector3::NegativeUnitZ);
			gx.SetTransformMatrix(World, world);
			m_buffer.Draw();
			gx.SetTransformMatrix(World, Matrix4::Identity);
		}

		m_renderTexture->Update();
		gx.RestoreState();

		LoadQueuedTiles();
		EvictTiles();
	}

	luabind::object WorldMap::GetHoverInfo(lua_State* L) const
	{
		if (!m_hasMap || !m_mouseInside || !m_viewInitialized)
		{
			return luabind::object();
		}

		const double worldX = m_view.ScreenToWorldX(m_lastMouseX);
		const double worldZ = m_view.ScreenToWorldZ(m_lastMouseY);

		luabind::object result = luabind::newtable(L);
		const uint32 cellZone = m_info.GetZoneAt(worldX, worldZ);
		const uint32 rootZone = GetRootZone(cellZone);
		if (rootZone != 0)
		{
			result["zone"] = GetZoneName(rootZone);
			if (cellZone != rootZone)
			{
				result["subZone"] = GetZoneName(cellZone);
			}

			if (const proto_client::ZoneEntry* entry = m_zones.getById(rootZone); entry && entry->level() > 0)
			{
				result["level"] = entry->level();
			}

			// Coordinates in percent of the zone's extent, as players quote them
			if (const world_map::ZoneInfo* zone = m_rootInfo.FindZone(rootZone))
			{
				result["x"] = (worldX - zone->bounds.minX) / std::max(1.0, zone->bounds.Width()) * 100.0;
				result["y"] = (worldZ - zone->bounds.minZ) / std::max(1.0, zone->bounds.Height()) * 100.0;
			}
		}

		// Members under the cursor
		luabind::object members = luabind::newtable(L);
		int32 index = 1;
		const double hitRadius = 18.0 * m_view.unitsPerPixel;
		for (const MapMemberDot& member : m_members)
		{
			const double dx = member.position.x - worldX;
			const double dz = member.position.z - worldZ;
			if (dx * dx + dz * dz <= hitRadius * hitRadius)
			{
				luabind::object entry = luabind::newtable(L);
				entry["name"] = member.name;
				entry["type"] = std::string(member.guild ? "guild" : "party");
				members[index++] = entry;
			}
		}
		result["members"] = members;

		return result;
	}

	luabind::object WorldMap::GetPlayerInfo(lua_State* L) const
	{
		if (!m_hasMap)
		{
			return luabind::object();
		}

		luabind::object result = luabind::newtable(L);
		const uint32 cellZone = m_info.GetZoneAt(m_playerPosition.x, m_playerPosition.z);
		const uint32 rootZone = GetRootZone(cellZone);
		if (rootZone != 0)
		{
			result["zone"] = GetZoneName(rootZone);
			if (const world_map::ZoneInfo* zone = m_rootInfo.FindZone(rootZone))
			{
				result["x"] = (m_playerPosition.x - zone->bounds.minX) / std::max(1.0, zone->bounds.Width()) * 100.0;
				result["y"] = (m_playerPosition.z - zone->bounds.minZ) / std::max(1.0, zone->bounds.Height()) * 100.0;
			}
		}

		return result;
	}
}
