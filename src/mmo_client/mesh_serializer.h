#pragma once

#include "base/typedefs.h"

namespace io
{
	class Writer;
}

namespace mmo
{
	class Mesh;
	
	enum class MeshVersion 
	{
		/// Latest version available
		Latest,

		/// First version of the mesh format.
		Version_0_1
	};

	class MeshSerializer
	{
	public:
		void ExportMesh(const Mesh& mesh, io::Writer& writer, MeshVersion version = MeshVersion::Latest);
	};
}