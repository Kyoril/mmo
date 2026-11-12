// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "map_member_dot.h"

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "client_data/project.h"
#include "frame_ui/font.h"
#include "frame_ui/geometry_buffer.h"
#include "game/world_map.h"
#include "graphics/render_texture.h"
#include "graphics/texture.h"
#include "math/radian.h"
#include "math/vector3.h"

#include <map>
#include <optional>
#include <vector>

#include "luabind/luabind.hpp"

struct lua_State;

namespace mmo
{
	/// @brief The world map: the player's whole map as one continuous view that can be dragged and zoomed
	///        from the full map down to single buildings.
	/// @details Draws, into its own render texture, the baked map art (world_map::WorldMapInfo) at the level
	///	         that matches the zoom over the coarsest level, and crossfades into the minimap tiles when
	///	         zoomed in closely. Zone and sub zone names appear as their areas grow large enough on screen
	///	         and fade out when they fill the view. The zone under the cursor is highlighted. On top go the
	///	         party (blue) and guild (green) members and the player's arrow, all at a constant screen size.
	///
	///	         Wheel zoom eases towards its target about the cursor; a left click glides to the zone under the
	///	         cursor (and then to its sub zone), a right click glides out one level.
	class WorldMap final : public NonCopyable
	{
	public:
		/// @param zones Zone names, parents and levels.
		explicit WorldMap(const proto_client::ZoneManager& zones);

		~WorldMap() override;

		void RegisterScriptFunctions(lua_State* luaState);

	public:
		/// @brief Loads the world map of the map the player entered.
		/// @param mapDirectory The map's directory (the folder of its minimap and world map files).
		/// @param mapName The map's display name.
		void NotifyWorldChanged(const String& mapDirectory, const String& mapName);

		void UpdatePlayerPosition(const Vector3& position, const Radian& facing);

		/// @brief Sets the party and guild members to show (members on this map only).
		void UpdateMemberDots(std::vector<MapMemberDot> members);

		/// @brief Advances view animations and texture fade-ins.
		void Update(float elapsedSeconds);

		/// @brief Renders the map into the render texture at the given size in screen pixels.
		/// @param textScale Scale for text and markers (FrameManager::GetTextScale).
		void Render(uint16 width, uint16 height, float textScale);

		/// @brief The render texture the map is drawn into (recreated when the size changes).
		[[nodiscard]] const RenderTexturePtr& GetTexture() const { return m_renderTexture; }

		/// @brief Whether the current map has a baked world map.
		[[nodiscard]] bool HasMap() const { return m_hasMap; }

		/// @brief Frees the cached tiles except the coarsest level, when the map is closed.
		void ReleaseTiles();

		/// @brief Sets the fonts used for the map title, zone and sub zone names.
		void SetFonts(FontPtr titleFont, FontPtr zoneFont, FontPtr subZoneFont);

	public:
		/// @brief Mouse input, in pixels relative to the map's top-left corner.
		void OnMouseDown(int32 button, float x, float y);
		void OnMouseUp(int32 button, float x, float y);
		void OnMouseMoved(float x, float y);
		void OnMouseWheel(int32 delta);
		void OnMouseLeave();

		/// @brief Centres the view on the player at a regional zoom.
		void CenterOnPlayer(bool animate);

		/// @brief Shows the whole map.
		void ShowWholeMap(bool animate);

		/// @brief Zooms in (positive steps) or out about the view centre.
		void ZoomSteps(int32 steps);

	private:
		/// @brief A loaded (or loading) map tile.
		struct CachedTile
		{
			TexturePtr texture;
			float fade = 0.0f;
			float lastUsed = 0.0f;
		};

		struct Label
		{
			String text;
			double x = 0.0;
			double z = 0.0;
			float alpha = 0.0f;
			const Font* font = nullptr;
			uint32 color = 0;
			int32 priority = 0;
		};

		static uint64 TileKey(uint32 kind, uint32 level, uint32 x, uint32 z)
		{
			return (static_cast<uint64>(kind) << 48) | (static_cast<uint64>(level) << 32) | (static_cast<uint64>(x) << 16) | z;
		}

		/// @brief Gets a cached tile, queueing its load if it is not loaded and @p request is set.
		CachedTile* GetTile(uint64 key, const String& filename, bool request, float priority);

		/// @brief Loads up to a budget of queued tiles, nearest to the view centre first.
		void LoadQueuedTiles();

		/// @brief Drops tiles that were not drawn for a while once the cache grows large.
		void EvictTiles();

		void DrawArtLevel(uint32 level, bool request, float alpha);
		void DrawMinimapTiles(float alpha);
		void DrawZoneHighlight();
		void DrawLabels(float textScale);
		void DrawMarkers(float textScale);

		/// @brief Adds a world-space textured quad to the geometry buffer.
		void AddWorldQuad(const TexturePtr& texture, const world_map::WorldRect& rect, float alpha);

		/// @brief The root (top level) zone of a zone.
		[[nodiscard]] uint32 GetRootZone(uint32 zoneId) const;

		[[nodiscard]] String GetZoneName(uint32 zoneId) const;

		/// @brief Rebuilds the hover highlight mask for a root zone.
		void BuildHighlightMask(uint32 rootZoneId);

		/// @brief Starts a glide (or jumps) to a world rectangle.
		void GlideTo(const world_map::WorldRect& rect, bool animate);

		void SetTarget(double centerX, double centerZ, double unitsPerPixel, bool animate);

		/// @brief The zoom at which the given zone (root or sub zone) fills the view.
		[[nodiscard]] double GetZoneFitZoom(const world_map::ZoneInfo& zone) const;

		void ClampViews();

		luabind::object GetHoverInfo(lua_State* L) const;

		luabind::object GetPlayerInfo(lua_State* L) const;

	private:
		const proto_client::ZoneManager& m_zones;

		String m_mapDirectory;
		String m_mapName;
		bool m_hasMap = false;

		world_map::WorldMapInfo m_info;

		/// @brief The cell grid with every cell set to its root zone, and the root zones' extents.
		world_map::WorldMapInfo m_rootInfo;

		/// @brief Current and target view. The target is where the view eases to.
		world_map::View m_view;
		world_map::View m_target;
		bool m_viewInitialized = false;

		/// @brief Zoom anchor: while easing a wheel zoom, this world point stays under the cursor.
		bool m_hasZoomAnchor = false;
		double m_anchorWorldX = 0.0;
		double m_anchorWorldZ = 0.0;
		double m_anchorScreenX = 0.0;
		double m_anchorScreenY = 0.0;

		/// @brief Glide state (click zoom): log-space zoom and centre interpolation.
		bool m_gliding = false;
		float m_glideTime = 0.0f;
		world_map::View m_glideFrom;

		/// @brief Mouse state.
		bool m_dragging = false;
		bool m_dragMoved = false;
		int32 m_dragButton = 0;
		float m_dragStartX = 0.0f;
		float m_dragStartY = 0.0f;
		float m_lastMouseX = 0.0f;
		float m_lastMouseY = 0.0f;
		bool m_mouseInside = false;

		Vector3 m_playerPosition;
		Radian m_playerFacing;
		std::vector<MapMemberDot> m_members;

		std::map<uint64, CachedTile> m_tiles;
		std::map<uint64, bool> m_missingTiles;

		struct PendingLoad
		{
			uint64 key;
			String filename;
			float priority;
		};
		std::vector<PendingLoad> m_pendingLoads;
		float m_time = 0.0f;

		RenderTexturePtr m_renderTexture;
		uint16 m_textureWidth = 0;
		uint16 m_textureHeight = 0;
		uint32 m_textureGeneration = 0;

		GeometryBuffer m_buffer;

		TexturePtr m_highlightMask;
		uint32 m_highlightZone = 0;
		float m_highlightAlpha = 0.0f;
		uint32 m_hoverZone = 0;

		TexturePtr m_playerArrowTexture;
		TexturePtr m_dotTexture;

		/// @brief 1 x 1 white texture for solid quads (the sea behind the art).
		TexturePtr m_whiteTexture;

		FontPtr m_titleFont;
		FontPtr m_zoneFont;
		FontPtr m_subZoneFont;
	};
}
