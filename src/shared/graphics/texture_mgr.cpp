// Copyright (C) 2019, Robin Klimonow. All rights reserved.

#include "texture_mgr.h"
#include "graphics_device.h"

#include "assets/asset_registry.h"

#include <stdexcept>


namespace mmo
{
	TextureManager & TextureManager::Get()
	{
		static TextureManager s_textureMgr;
		return s_textureMgr;
	}

	TexturePtr TextureManager::CreateOrRetrieve(const std::string & filename)
	{
		// Try to find the texture by name and see if it is still loaded
		auto it = m_texturesByName.find(filename);
		if (it != m_texturesByName.end())
		{
			return it->second;
		}

		// If the texture was loaded, this could would be unreachable, so we will simply
		// load the texture from here on.

		// Try to load the requested file
		auto file = AssetRegistry::OpenFile(filename);
		if (!file)
		{
			throw std::runtime_error("Failed to open file " + filename);
		}

		// Create a new texture object
		auto texture = GraphicsDevice::Get().CreateTexture();
		texture->Load(file);

		// Add it to the list of textures
		m_texturesByName[filename] = texture;

		// Increase the memory usage and ensure we are still within the memory budget. Since we still hold
		// a shared_ptr on the texture in this function, this should not remove the texture
		m_memoryUsage += texture->GetMemorySize();
		EnsureMemoryBudget();

		// Ensure that the new texture is still references more than once after memory budget adjustments
		ASSERT(texture.use_count() > 1);

		// Return the loaded texture
		return texture;
	}

	void TextureManager::EnsureMemoryBudget()
	{
		if (m_memoryUsage < m_memoryBudget)
			return;

		// TODO: Implement a proper strategy

		auto it = m_texturesByName.begin();
		while (it != m_texturesByName.end() && m_memoryUsage > m_memoryBudget)
		{
			// Use count of 1 means the texture manager is the only one referencing this texture
			// right now, so we can safely erase it now.
			if (it->second.use_count() == 1)
			{
				// Reduce current memory usage
				auto usage = it->second->GetMemorySize();
				m_memoryUsage -= usage;

				// Remove reference
				it = m_texturesByName.erase(it);
			}
		}
	}

	void TextureManager::SetMemoryBudget(size_t newBudget)
	{
		m_memoryBudget = newBudget;

		EnsureMemoryBudget();
	}
}
