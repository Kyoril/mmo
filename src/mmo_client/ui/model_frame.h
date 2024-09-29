// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#pragma once

#include "scene_graph/mesh.h"
#include "frame_ui/frame.h"

namespace mmo
{
	/// This class renders a model inside it's content area.
	class ModelFrame final
		: public Frame
	{
	public:
		/// Default constructor.
		explicit ModelFrame(const std::string& name);

	public:
		/// Sets the new model file to display. This is a wrapper around the property
		/// system to make access easier for lua scripts.
		void SetModelFile(const std::string& filename);

	public:
		/// Gets the mesh instance or nullptr if there is no mesh loaded.
		inline MeshPtr GetMesh() const noexcept { return m_mesh; }

	private:
		/// This method is called whenever the ModelFile property is changed.
		void OnModelFileChanged(const Property& prop);

	private:
		/// Contains a list of all property connections.
		scoped_connection_container m_propConnections;
		/// The mesh instance.
		MeshPtr m_mesh;
	};
}