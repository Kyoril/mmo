
#pragma once

#include "base/non_copyable.h"
#include "math/vector3.h"
#include "math/radian.h"
#include "graphics/texture.h"
#include "graphics/render_texture.h"
#include "frame_ui/geometry_buffer.h"
#include "base/signal.h"
#include "base/typedefs.h"

#include <map>
#include <memory>

struct lua_State;

namespace mmo
{
	/// @brief This class manages the minimap rendering functionality in the game.
	/// It tracks player position and loads/unloads minimap textures based on tile coordinates.
	class Minimap final : public NonCopyable
	{
	public:
		/// @brief Constructor.
		/// @param minimapSize The size of the minimap render texture (width and height in pixels).
		explicit Minimap(uint32 minimapSize = 256);

		/// @brief Destructor.
		~Minimap();

		void RegisterScriptFunctions(lua_State* luaState);

	public:
		/// @brief Updates the minimap based on the player's current position and orientation.
		/// This method should be called when the player position changes. It calculates the current
		/// player tile and loads/unloads minimap textures as needed.
		/// @param playerPosition The current world position of the player.
		/// @param playerOrientation The current facing direction of the player in radians.
		void UpdatePlayerPosition(const Vector3& playerPosition, const Radian& playerOrientation);

		/// @brief Renders the minimap into the internal render texture.
		/// This method renders visible tiles around the player position using textured quads.
		void RenderMinimap();

		/// @brief Gets the minimap render texture that can be used for UI rendering.
		/// @return The render texture containing the current minimap view.
		RenderTexturePtr GetMinimapTexture() const
		{
			return m_minimapRenderTexture;
		}

		/// @brief Sets the zoom level of the minimap.
		/// @param zoomLevel The zoom level (0 = no zoom, higher values = more zoom).
		void SetZoomLevel(int32 zoomLevel);

		/// @brief Gets the current zoom level.
		/// @return The current zoom level.
		int32 GetZoomLevel() const
		{
			return m_zoomLevel;
		}

		/// @brief Gets the maximum zoom level supported.
		/// @return The maximum zoom level.
		static constexpr int32 GetMaxZoomLevel()
		{
			return 10;
		}

		/// @brief Calculates the zoom factor based on the zoom level.
		/// @return The zoom factor (1.0f = no zoom, 2.0f = 200% zoom).
		float GetZoomFactor() const;

		void NotifyWorldChanged(const String& worldName);

	private:
		/// @brief Calculates the tile coordinates for a given world position.
		/// @param worldPosition The world position to convert.
		/// @param outTileX The output tile X coordinate.
		/// @param outTileY The output tile Y coordinate.
		/// @return True if the position is within valid tile bounds.
		static bool GetTileCoordinates(const Vector3& worldPosition, int32& outTileX, int32& outTileY);

		/// @brief Loads a minimap texture for the specified tile coordinates.
		/// @param tileX The tile X coordinate.
		/// @param tileY The tile Y coordinate.
		/// @return The loaded texture, or nullptr if loading failed.
		TexturePtr LoadMinimapTexture(int32 tileX, int32 tileY) const;

		/// @brief Unloads minimap textures that are no longer needed.
		/// @param currentTileX The current player tile X coordinate.
		/// @param currentTileY The current player tile Y coordinate.
		void UnloadDistantTextures(int32 currentTileX, int32 currentTileY);

		/// @brief Gets the filename for a minimap texture based on tile coordinates.
		/// @param tileX The tile X coordinate.
		/// @param tileY The tile Y coordinate.
		/// @return The filename of the minimap texture.
		String GetMinimapTextureFilename(int32 tileX, int32 tileY) const;

		/// @brief Builds a render quad for a tile at the specified position.
		/// @param geometryBuffer The geometry buffer to add the quad to.
		/// @param texture The texture to use for the quad.
		/// @param worldX The world X position of the tile.
		/// @param worldY The world Y position of the tile.
		/// @param tileSize The size of the tile in world units.
		static void AddTileQuad(GeometryBuffer& geometryBuffer, const TexturePtr& texture, 
		                        float worldX, float worldY, float tileSize);

		static uint16 BuildPageIndex(uint8 x, uint8 y);

	private:
		/// @brief The size of the minimap render texture (width and height).
		uint32 m_minimapSize;

		GeometryBuffer m_geometryBuffer;

		/// @brief The render texture used for rendering the minimap.
		RenderTexturePtr m_minimapRenderTexture;

		/// @brief The current player world position.
		Vector3 m_playerPosition;

		/// @brief The current player orientation.
		Radian m_playerOrientation;

		/// @brief The current player tile coordinates.
		int32 m_currentTileX;
		int32 m_currentTileY;

		/// @brief The previous player tile coordinates (to detect tile changes).
		int32 m_previousTileX;
		int32 m_previousTileY;

		/// @brief The current zoom level (0 = no zoom).
		int32 m_zoomLevel;

		/// @brief Map of loaded minimap textures indexed by tile coordinates.
		/// Key is packed tile coordinates (x + y * 65536).
		std::map<uint64, TexturePtr> m_loadedTextures;

		/// @brief The maximum distance (in tiles) for loading textures around the player.
		static constexpr int32 s_maxLoadDistance = 2;

		String m_worldName;

		/// @brief Whether the minimap has been initialized.
		bool m_initialized;

		TexturePtr m_playerArrowTexture;
		GeometryBuffer m_playerGeom;
	};
}
