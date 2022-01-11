// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

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