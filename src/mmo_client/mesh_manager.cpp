// Copyright (C) 2020, Robin Klimonow. All rights reserved.

#include "mesh_manager.h"

#include "assets/asset_registry.h"
#include "mesh/pre_header.h"
#include "mesh/pre_header_load.h"
#include "mesh_v1_0/header.h"
#include "mesh_v1_0/header_load.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "base/macros.h"
#include "log/default_log_levels.h"


namespace mmo
{
	MeshManager & MeshManager::Get()
	{
		static MeshManager instance;
		return instance;
	}

	MeshPtr MeshManager::Load(const std::string & filename)
	{
		// Try to find the mesh first before trying to load it again
		const auto it = m_meshes.find(filename);
		if (it != m_meshes.end())
		{
			return it->second;
		}

		// Try to load the file from the registry
		const auto filePtr = AssetRegistry::OpenFile(filename);
		ASSERT(filePtr && "Unable to load mesh file");

		// Create readers
		io::StreamSource source{ *filePtr };
		io::Reader reader{ source };

		// Load mesh pre header to determine file format version etc.
		mesh::PreHeader preHeader;
		if (!mesh::LoadPreHeader(preHeader, reader))
		{
			ELOG("Failed to load mesh pre header of file " << filename);
			return nullptr;
		}

		// Create the resulting mesh
		auto mesh = std::make_shared<Mesh>();

		// Depending on the format version, load the real mesh data now
		switch (preHeader.version)
		{
		case mesh::Version_1_0:
			{
				mesh::v1_0::Header header;
				if (!mesh::v1_0::LoadHeader(header, reader))
				{
					ELOG("Failed to load mesh header of file " << filename);
					return nullptr;
				}

				
			}
			break;
		}

		// Store the mesh in the cache
		m_meshes[filename] = mesh;

		// And return the mesh pointer
		return mesh;
	}
}
