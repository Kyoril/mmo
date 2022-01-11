// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "mesh.h"

#include "base/non_copyable.h"
#include "base/utilities.h"


namespace mmo
{
	/// This class manages meshes. It loads them from files if required and caches
	/// them so that a mesh isn't loaded multiple times.
	class MeshManager final
		: public NonCopyable
	{
	private:
		MeshManager() = default;

	public:
		~MeshManager() = default;

	public:
		static MeshManager& Get();

	public:
		/// Loads a mesh from file or retrieves it from the cache.
		MeshPtr Load(const std::string& filename);

		/// Creates a mesh manually.
		MeshPtr CreateManual(const std::string& name);

	private:
		/// A map of loaded meshes. Used to cache. Case-insensitive file names.
		std::map<std::string, MeshPtr, StrCaseIComp> m_meshes;
	};
}
