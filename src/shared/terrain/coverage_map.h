#pragma once

#include "base/non_copyable.h"
#include "base/typedefs.h"
#include "graphics/texture.h"

namespace mmo
{
	/// The number of pixels of a coverage map on each side.
	constexpr uint32 CoverageMapSize = 64;

	class CoverageMap final : public NonCopyable
	{
	public:
		explicit CoverageMap(String name);
		~CoverageMap() override = default;

	public:
		void Initialize();

	private:
		String m_name;
		TexturePtr m_texture;
		std::vector<uint32> m_buffer;
	};
}
