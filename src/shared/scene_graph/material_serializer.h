#pragma once
#include "base/typedefs.h"

namespace io
{
	class Writer;
}

namespace mmo
{
	class Material;

	namespace material_version
	{
		enum Type
		{
			Latest = -1,

			Version_0_1 = 0x0100
		};	
	}

	typedef material_version::Type MaterialVersion;
	
	
	struct MaterialAttributes
	{
		uint8 twoSided;
		uint8 castShadows;
		uint8 receiveShadows;
		uint8 materialType;
	};

	class MaterialSerializer
	{
	public:
		void Export(const Material& material, io::Writer& writer, MaterialVersion version = material_version::Latest);
	};
}