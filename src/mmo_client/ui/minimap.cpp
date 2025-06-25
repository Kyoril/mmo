
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

		// Calculate zoom factor and visible tile range
		const float zoomFactor = GetZoomFactor();
		constexpr float tileSize = terrain::constants::PageSize;
		const float scaledTileSize = tileSize * zoomFactor;
		
		// Calculate how many tiles to render based on minimap size and zoom
		const int32 visibleTileRange = static_cast<int32>((m_minimapSize * 0.5f) / scaledTileSize) + 1;

		gx.SetTransformMatrix(World, Matrix4::Identity);
		gx.SetTransformMatrix(View, Matrix4::Identity);
		gx.SetTransformMatrix(Projection, gx.MakeOrthographicMatrix(0.0f, 0.0f, scaledTileSize, scaledTileSize, 0.1f, 1000.0f));

		// Render tiles around player position
		for (int32 x = m_currentTileX - visibleTileRange; x <= m_currentTileX + visibleTileRange; ++x)
		{
			for (int32 y = m_currentTileY - visibleTileRange; y <= m_currentTileY + visibleTileRange; ++y)
			{
				uint64 tileKey = static_cast<uint64>(x) + (static_cast<uint64>(y) * 65536);
				if (auto it = m_loadedTextures.find(tileKey); it != m_loadedTextures.end() && it->second)
				{
					// Calculate world position for this tile
					float worldX = static_cast<float>(x - 32) * tileSize;
					float worldY = static_cast<float>(y - 32) * tileSize;
					
					// Add a textured quad for this tile
					AddTileQuad(m_geometryBuffer, it->second, worldX, worldY, scaledTileSize);
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
		
		// Basic bounds checking - for now assume tiles can be in reasonable range
		// In a real implementation, you might want to check against actual terrain bounds
		const int32 maxTileRange = 1000; // Arbitrary large range
		
		return (outTileX >= -maxTileRange && outTileX <= maxTileRange &&
		        outTileY >= -maxTileRange && outTileY <= maxTileRange);
	}

	TexturePtr Minimap::LoadMinimapTexture(const int32 tileX, const int32 tileY)
	{
		const String filename = GetMinimapTextureFilename(tileX, tileY);
		DLOG("[Minimap] Loading (" << tileX << ", " << tileY << "): " << filename);
		
		// Use asset registry to load the texture
		TexturePtr texture = TextureManager::Get().CreateOrRetrieve(filename);
		if (!texture)
		{
			// If specific tile texture doesn't exist, could fall back to a default/empty tile texture
			DLOG("Minimap texture not found: " << filename);
		}
		
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

		// Calculate screen coordinates relative to player position
		float relativeX = worldX - m_playerPosition.x;
		float relativeY = worldY - m_playerPosition.z; // Using Z as Y for top-down view
		
		// Center on minimap
		float centerX = m_minimapSize * 0.5f;
		float centerY = m_minimapSize * 0.5f;
		
		// Convert to screen coordinates
		float screenX = centerX + relativeX;
		float screenY = centerY - relativeY; // Flip Y for screen coordinates
		
		// Define quad vertices (screen coordinates)
		float left = screenX - tileSize * 0.5f;
		float right = screenX + tileSize * 0.5f;
		float top = screenY - tileSize * 0.5f;
		float bottom = screenY + tileSize * 0.5f;
		
		// Only render if quad is within minimap bounds (with some tolerance)
		const float tolerance = tileSize;
		if (right < -tolerance || left > m_minimapSize + tolerance ||
		    bottom < -tolerance || top > m_minimapSize + tolerance)
		{
			return;
		}
		
		// Add textured quad to geometry buffer
		// Texture coordinates: (0,0) top-left, (1,1) bottom-right
		geometryBuffer.SetActiveTexture(texture);
		GeometryHelper::CreateRect(geometryBuffer,
			Color::White,
			Rect(left, top, right, bottom),
			Rect(0.0f, 0.0f, 1.0, 1.0f),
			texture->GetWidth(),
			texture->GetHeight());
	}
}
