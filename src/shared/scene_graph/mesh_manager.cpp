// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "mesh_manager.h"

#include "mesh_serializer.h"
#include "assets/asset_registry.h"
#include "binary_io/reader.h"
#include "binary_io/stream_source.h"
#include "base/macros.h"
#include "log/default_log_levels.h"
#include "graphics/graphics_device.h"


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
		if (!filePtr || !*filePtr)
		{
			ELOG("Unable to load mesh file " << filename);
			return nullptr;
		}

		// Create readers
		io::StreamSource source{ *filePtr };
		io::Reader reader{ source };
		
		// Create the resulting mesh
		auto mesh = std::make_shared<Mesh>(filename);

		MeshDeserializer deserializer { *mesh };
		if (!deserializer.Read(reader))
		{
			ELOG("Failed to load mesh!");
			return nullptr;
		}
		
		// Store the mesh in the cache
		m_meshes[filename] = mesh;

		// And return the mesh pointer
		return mesh;
	}

	MeshPtr MeshManager::CreateManual(const std::string& name)
	{
		const auto it = m_meshes.find(name);
		ASSERT(it == m_meshes.end());

		auto mesh = std::make_shared<Mesh>(name);
		m_meshes[name] = mesh;

		return mesh;
	}
}
