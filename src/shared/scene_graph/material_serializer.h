#pragma once

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
	

	class MaterialSerializer
	{
	public:
		void Export(const Material& material, io::Writer& writer, MaterialVersion version = material_version::Latest);
	};
}