#pragma once

#include "base/typedefs.h"

#include "mesh/magic.h"

namespace io
{
	class Writer;
}

namespace mmo
{
	class Mesh;
	
	class MeshSerializer
	{
	public:
		void ExportMesh(const Mesh& mesh, io::Writer& writer, mesh::VersionId version = mesh::Latest);
	};
}