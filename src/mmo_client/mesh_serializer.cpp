
#include "mesh_serializer.h"
#include "mesh.h"

#include "base/macros.h"
#include "binary_io/writer.h"

namespace mmo
{
	void MeshSerializer::ExportMesh(const Mesh& mesh, io::Writer& writer, MeshVersion version)
	{
		ASSERT(!mesh.GetBounds().IsNull());

	}
}
