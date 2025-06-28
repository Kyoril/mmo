#include "minimap.h"
#include "graphics/graphics_device.h"
#include "assets/asset_registry.h"
#include "frame_ui/geometry_helper.h"
#include "graphics/texture_mgr.h"
#include "terrain/terrain.h"
#include "log/default_log_levels.h"

#include "luabind_lambda.h"

namespace mmo
{
	Minimap::Minimap(uint32 minimapSize)
		: m_minimapSize(minimapSize)
		, m_minimapRenderTexture(nullptr)
		, m_playerPosition(0.0f, 0.0f, 0.0f)
		, m_playerOrientation(0.0f)
		, m_currentTileX(0)
		, m_currentTileY(0)
		, m_previousTileX(-1)
		, m_previousTileY(-1)
		, m_zoomLevel(0)
		, m_initialized(false)
	{
		// Create the render texture for minimap rendering
		GraphicsDevice& gx = GraphicsDevice::Get();
		m_minimapRenderTexture = gx.CreateRenderTexture("Minimap", m_minimapSize, m_minimapSize, RenderTextureFlags::HasColorBuffer | RenderTextureFlags::HasDepthBuffer | RenderTextureFlags::ShaderResourceView,
			R8G8B8A8, D32F);

		m_playerArrowTexture = TextureManager::Get().CreateOrRetrieve("Interface/Icons/fg4_iconsBrown_left_result.htex");
		if (m_playerArrowTexture)
		{
			m_playerArrowTexture->SetTextureAddressMode(TextureAddressMode::Clamp);
			m_playerGeom.SetActiveTexture(m_playerArrowTexture);
			GeometryHelper::CreateRect(m_playerGeom, Color::White,
				Rect(-16.0f, -16.0f, 16.0f, 16.0f),
				Rect(1.0f, 1.0f, 0.0f, 0.0f), 
				1, 1);
		}

		m_partyMemberTexture = TextureManager::Get().CreateOrRetrieve("Interface/Icons/fg4_iconsFlat_bullet_result.htex");
		if (m_partyMemberTexture)
		{
			m_partyMemberTexture->SetTextureAddressMode(TextureAddressMode::Clamp);
		}

		if (m_minimapRenderTexture)
		{
			m_initialized = true;
			ILOG("Minimap initialized with size " << m_minimapSize << "x" << m_minimapSize);
		}
		else
		{
			ELOG("Failed to create minimap render texture");
		}
	}

	Minimap::~Minimap()
	{
		// Clear loaded textures
		m_loadedTextures.clear();
		m_minimapRenderTexture.reset();
		ILOG("Minimap destroyed");
	}

	void Minimap::RegisterScriptFunctions(lua_State* luaState)
	{
		luabind::module(luaState)
		[
			luabind::def_lambda("GetMinimapZoomLevel", [this]() { return GetZoomLevel(); }),
			luabind::def_lambda("GetMinimapMinZoomLevel", [this]() { return 0; }),
			luabind::def_lambda("GetMinimapMaxZoomLevel", [this]() { return GetMaxZoomLevel(); }),
			luabind::def_lambda("SetMinimapZoomLevel", [this](int32 zoomLevel) { SetZoomLevel(zoomLevel); })
		];
	}

	void Minimap::UpdatePlayerPosition(const Vector3& playerPosition, const Radian& playerOrientation)
	{
		if (!m_initialized)
		{
			return;
		}

		m_playerPosition = playerPosition;
		m_playerOrientation = playerOrientation;

		// Calculate current tile coordinates
		int32 newTileX, newTileY;
		if (!GetTileCoordinates(playerPosition, newTileX, newTileY))
		{
			// Player is outside valid terrain bounds
			return;
		}

		// Check if we've moved to a different tile
		if (const bool tileChanged = newTileX != m_currentTileX || newTileY != m_currentTileY)
		{
			// Update tile coordinates
			m_previousTileX = m_currentTileX;
			m_previousTileY = m_currentTileY;
			m_currentTileX = newTileX;
			m_currentTileY = newTileY;

			// Load textures for nearby tiles
			for (int32 x = newTileX - s_maxLoadDistance; x <= newTileX + s_maxLoadDistance; ++x)
			{
				for (int32 y = newTileY - s_maxLoadDistance; y <= newTileY + s_maxLoadDistance; ++y)
				{
					const uint64 tileKey = static_cast<uint64>(x) + static_cast<uint64>(y) * 65536;
					
					// Only load if not already loaded
					if (!m_loadedTextures.contains(tileKey))
					{
						if (const TexturePtr texture = LoadMinimapTexture(x, y))
						{
							m_loadedTextures[tileKey] = texture;
						}
					}
				}
			}

			// Unload distant textures to free memory
			UnloadDistantTextures(newTileX, newTileY);
		}
	}

	void Minimap::RenderMinimap()
	{
		if (!m_initialized || !m_minimapRenderTexture)
		{
			return;
		}

		m_geometryBuffer.Reset();

		// Set render target to minimap texture
		GraphicsDevice& gx = GraphicsDevice::Get();
		gx.CaptureState();
		m_minimapRenderTexture->Activate();
		m_minimapRenderTexture->Clear(ClearFlags::All);

		// Calculate zoom factor and world coverage
		const float zoomFactor = GetZoomFactor();
		constexpr float tileSize = terrain::constants::PageSize;
		
		// Calculate world area that the minimap covers based on zoom
		// Base coverage is the minimap size in world units, scaled by zoom
		const float worldCoverage = static_cast<float>(m_minimapSize) / zoomFactor;
		const float halfCoverage = worldCoverage * 0.5f;

		// Set up orthographic projection that maps from world coordinates to screen coordinates
		// Left/Right: player position +/- half coverage in X
		// Top/Bottom: player position +/- half coverage in Z (using Z as Y for top-down view)
		const float left = m_playerPosition.x - halfCoverage;
		const float right = m_playerPosition.x + halfCoverage;
		const float top = m_playerPosition.z - halfCoverage;
		const float bottom = m_playerPosition.z + halfCoverage;

		gx.SetTransformMatrix(World, Matrix4::Identity);
		gx.SetTransformMatrix(View, Matrix4::Identity);
		gx.SetTransformMatrix(Projection, gx.MakeOrthographicMatrix(left, top, right, bottom, 0.1f, 1000.0f));

		// Calculate which tiles are visible in this world area
		const int32 minTileX = static_cast<int32>(std::floor(left / tileSize)) + 32;
		const int32 maxTileX = static_cast<int32>(std::ceil(right / tileSize)) + 32;
		const int32 minTileY = static_cast<int32>(std::floor(top / tileSize)) + 32;
		const int32 maxTileY = static_cast<int32>(std::ceil(bottom / tileSize)) + 32;

		// Render tiles in the visible area
		for (int32 x = minTileX; x <= maxTileX; ++x)
		{
			for (int32 y = minTileY; y <= maxTileY; ++y)
			{
				uint64 tileKey = static_cast<uint64>(x) + static_cast<uint64>(y) * 65536;
				if (auto it = m_loadedTextures.find(tileKey); it != m_loadedTextures.end() && it->second)
				{
					// Calculate world position for this tile (center of tile)
					const float worldX = x * tileSize - 32 * tileSize; // Offset by 32 tiles to center around (0,0)
					const float worldY = y * tileSize - 32 * tileSize; // Offset by 32 tiles to center around (0,0)
					
					// Add a textured quad for this tile
					AddTileQuad(m_geometryBuffer, it->second, worldX, worldY, tileSize);
				}
			}
		}

		m_geometryBuffer.Draw();

		if (m_playerArrowTexture)
		{
			// Apply player rotation (TODO)
			Matrix4 world = Matrix4::Identity;
			world = world * Matrix4::GetTrans(Vector3(m_playerPosition.x, m_playerPosition.z, 0.0f));
			world = world * Quaternion(m_playerOrientation, Vector3::NegativeUnitZ);
			world = world * Matrix4::GetScale(Vector3(1.0f / GetZoomFactor(), 1.0f / GetZoomFactor(), 1.0f));
			gx.SetTransformMatrix(World, world);
			m_playerGeom.Draw();
		}
		
		m_minimapRenderTexture->Update();
		gx.RestoreState();
	}

	void Minimap::SetZoomLevel(const int32 zoomLevel)
	{
		m_zoomLevel = std::clamp(zoomLevel, 0, GetMaxZoomLevel());
	}

	float Minimap::GetZoomFactor() const
	{
		// Each zoom level increases by 10%: 1.0, 1.1, 1.21, 1.331, etc.
		// Formula: 1.0 * (1.1 ^ zoomLevel)
		return std::pow(1.1f, static_cast<float>(m_zoomLevel));
	}

	void Minimap::NotifyWorldChanged(const String& worldName)
	{
		if (m_worldName == worldName)
		{
			return;
		}

		// Flush!
		m_worldName = worldName;
		m_currentTileX = -1;
		m_currentTileY = -1;
		m_loadedTextures.clear();

		UpdatePlayerPosition(m_playerPosition, m_playerOrientation);
	}

	bool Minimap::GetTileCoordinates(const Vector3& worldPosition, int32& outTileX, int32& outTileY)
	{
		const double tileSize = terrain::constants::PageSize;
		
		// Convert world position to tile coordinates
		outTileX = static_cast<int32>(std::floor(worldPosition.x / tileSize)) + 32;
		outTileY = static_cast<int32>(std::floor(worldPosition.z / tileSize)) + 32; // Using Z as Y for top-down view
		
		return (outTileX >= 0 && outTileX < 64 &&
		        outTileY >= 0 && outTileY < 64);
	}

	TexturePtr Minimap::LoadMinimapTexture(const int32 tileX, const int32 tileY) const
	{
		const String filename = GetMinimapTextureFilename(tileX, tileY);

		// Use asset registry to load the texture
		TexturePtr texture = TextureManager::Get().CreateOrRetrieve(filename);
		if (texture)
		{
			texture->SetTextureAddressMode(TextureAddressMode::Clamp);
		}
		return texture;
	}

	void Minimap::UnloadDistantTextures(const int32 currentTileX, const int32 currentTileY)
	{
		const int32 unloadDistance = s_maxLoadDistance + 2; // Keep a buffer beyond max load distance
		
		auto it = m_loadedTextures.begin();
		while (it != m_loadedTextures.end())
		{
			uint64 tileKey = it->first;
			int32 tileX = static_cast<int32>(tileKey & 0xFFFF);
			int32 tileY = static_cast<int32>(tileKey >> 16);
			
			// Calculate distance from current tile
			int32 deltaX = std::abs(tileX - currentTileX);
			int32 deltaY = std::abs(tileY - currentTileY);
			int32 maxDistance = std::max(deltaX, deltaY);
			
			if (maxDistance > unloadDistance)
			{
				DLOG("Unloading distant minimap texture for tile (" << tileX << ", " << tileY << ")");
				it = m_loadedTextures.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	String Minimap::GetMinimapTextureFilename(const int32 tileX, const int32 tileY) const
	{
		// Generate filename with encoded coordinates
		const uint16 pageIndex = BuildPageIndex(static_cast<uint8>(tileX), static_cast<uint8>(tileY));
		return "Textures/Minimaps/" + m_worldName + "/" + std::to_string(pageIndex) + ".htex";
	}

	void Minimap::AddTileQuad(GeometryBuffer& geometryBuffer, const TexturePtr& texture, const float worldX, const float worldY, const float tileSize)
	{
		if (!texture)
		{
			return;
		}

		// Define quad vertices in world coordinates (centered on worldX, worldY)
		const float left = worldX;
		const float right = worldX + tileSize;
		const float top = worldY;
		const float bottom = worldY + tileSize;
		
		// Set the active texture
		geometryBuffer.SetActiveTexture(texture);
		
		const GeometryBuffer::Vertex vertices[6]{
			// First triangle: bottom-left, top-left, top-right
			{ { left,	bottom,	0.0f }, Color::White.GetABGR(), { 0.0f, 1.0f } },  // World (-X,-Z) -> UV left,bottom
			{ { left,	top,	0.0f }, Color::White.GetABGR(), { 0.0f, 0.0f } },  // World (-X,+Z) -> UV left,top
			{ { right,	top,	0.0f }, Color::White.GetABGR(), { 1.0f, 0.0f } },  // World (+X,+Z) -> UV right,top
			// Second triangle: top-right, bottom-right, bottom-left
			{ { right,	top,	0.0f }, Color::White.GetABGR(), { 1.0f, 0.0f } },  // World (+X,+Z) -> UV right,top
			{ { right,	bottom,	0.0f }, Color::White.GetABGR(), { 1.0f, 1.0f } },  // World (+X,-Z) -> UV right,bottom
			{ { left,	bottom,	0.0f }, Color::White.GetABGR(), { 0.0f, 1.0f } }   // World (-X,-Z) -> UV left,bottom
		};
		
		// Append the custom vertices
		geometryBuffer.AppendGeometry(vertices, 6);
	}

	uint16 Minimap::BuildPageIndex(const uint8 x, const uint8 y)
	{
		// Pack X and Y coordinates into a 16-bit page index
		// X in lower 8 bits, Y in upper 8 bits
		return (static_cast<uint16>(x) << 8) | static_cast<uint16>(y);
	}
}
