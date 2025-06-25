#include "minimap.h"
#include "graphics/graphics_device.h"
#include "assets/asset_registry.h"
#include "frame_ui/geometry_helper.h"
#include "graphics/texture_mgr.h"
#include "terrain/terrain.h"
#include "log/default_log_levels.h"

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
			ILOG("[Minimap] Player moved to tile (" << newTileX << ", " << newTileY << ")");
			
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
					uint64 tileKey = static_cast<uint64>(x) + static_cast<uint64>(y) * 65536;
					
					// Only load if not already loaded
					if (m_loadedTextures.find(tileKey) == m_loadedTextures.end())
					{
						if (TexturePtr texture = LoadMinimapTexture(x, y))
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
		const float top = m_playerPosition.z + halfCoverage;    // Top of screen = +Z
		const float bottom = m_playerPosition.z - halfCoverage; // Bottom of screen = -Z

		gx.SetTransformMatrix(World, Matrix4::Identity);
		gx.SetTransformMatrix(View, Matrix4::Identity);
		gx.SetTransformMatrix(Projection, gx.MakeOrthographicMatrix(left, top, right, bottom, 0.1f, 1000.0f));

		// Calculate which tiles are visible in this world area
		const int32 minTileX = static_cast<int32>(std::floor(left / tileSize)) + 32;
		const int32 maxTileX = static_cast<int32>(std::ceil(right / tileSize)) + 32;
		const int32 minTileY = static_cast<int32>(std::floor(bottom / tileSize)) + 32;
		const int32 maxTileY = static_cast<int32>(std::ceil(top / tileSize)) + 32;

		// Render tiles in the visible area
		for (int32 x = minTileX; x <= maxTileX; ++x)
		{
			for (int32 y = minTileY; y <= maxTileY; ++y)
			{
				uint64 tileKey = static_cast<uint64>(x) + (static_cast<uint64>(y) * 65536);
				if (auto it = m_loadedTextures.find(tileKey); it != m_loadedTextures.end() && it->second)
				{
					// Calculate world position for this tile (center of tile)
					const float worldX = (static_cast<float>(x) - 32.0f) * tileSize;
					const float worldY = (static_cast<float>(y) - 32.0f) * tileSize;
					
					// Add a textured quad for this tile
					AddTileQuad(m_geometryBuffer, it->second, worldX, worldY, tileSize);
				}
			}
		}

		m_geometryBuffer.Draw();
		m_minimapRenderTexture->Update();
		gx.RestoreState();
	}

	void Minimap::SetZoomLevel(int32 zoomLevel)
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

	bool Minimap::GetTileCoordinates(const Vector3& worldPosition, int32& outTileX, int32& outTileY) const
	{
		const double tileSize = terrain::constants::PageSize;
		
		// Convert world position to tile coordinates
		outTileX = static_cast<int32>(std::floor(worldPosition.x / tileSize)) + 32;
		outTileY = static_cast<int32>(std::floor(worldPosition.z / tileSize)) + 32; // Using Z as Y for top-down view
		
		return (outTileX >= 0 && outTileX < 64 &&
		        outTileY >= 0 && outTileY < 64);
	}

	TexturePtr Minimap::LoadMinimapTexture(const int32 tileX, const int32 tileY)
	{
		const String filename = GetMinimapTextureFilename(tileX, tileY);

		// Use asset registry to load the texture
		TexturePtr texture = TextureManager::Get().CreateOrRetrieve(filename);		
		return texture;
	}

	void Minimap::UnloadDistantTextures(int32 currentTileX, int32 currentTileY)
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

	void Minimap::AddTileQuad(GeometryBuffer& geometryBuffer, TexturePtr texture, float worldX, float worldY, float tileSize)
	{
		if (!texture)
		{
			return;
		}

		// Since we're using an orthographic projection that maps world coordinates directly,
		// we can define our quad vertices in world space
		const float halfTile = tileSize * 0.5f;
		
		// Define quad vertices in world coordinates (centered on worldX, worldY)
		const float left = worldX - halfTile;
		const float right = worldX + halfTile;
		const float top = worldY + halfTile;
		const float bottom = worldY - halfTile;
		
		// Add textured quad to geometry buffer using world coordinates
		// The orthographic projection will handle the conversion to screen space
		geometryBuffer.SetActiveTexture(texture);
		GeometryHelper::CreateRect(geometryBuffer,
			Color::White,
			Rect(left, top, right, bottom),
			Rect(0.0f, 0.0f, texture->GetWidth(), texture->GetHeight()),
			texture->GetWidth(),
			texture->GetHeight());
	}

	uint16 Minimap::BuildPageIndex(uint8 x, const uint8 y)
	{
		// Pack X and Y coordinates into a 16-bit page index
		// X in lower 8 bits, Y in upper 8 bits
		return static_cast<uint16>(x) | (static_cast<uint16>(y) << 8);
	}
}
