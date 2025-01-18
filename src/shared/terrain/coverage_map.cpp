
#include "coverage_map.h"
#include "graphics/texture_mgr.h"
#include "graphics/texture.h"

namespace mmo
{
	CoverageMap::CoverageMap(std::string name)
		: m_name(std::move(name))
	{
	}

	void CoverageMap::Initialize()
	{
		ASSERT(!m_texture);

		m_texture = TextureManager::Get().CreateManual(m_name, CoverageMapSize, CoverageMapSize, R8G8B8A8, BufferUsage::StaticWriteOnly);
		ASSERT(m_texture);

		m_buffer.resize(CoverageMapSize * CoverageMapSize, 0x000000FF);
		m_texture->LoadRaw(m_buffer.data(), CoverageMapSize * CoverageMapSize * 4);
	}
}
