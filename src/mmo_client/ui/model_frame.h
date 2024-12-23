// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

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

		void SetYaw(float angleDegrees);

		void Yaw(float angleDegrees);

		void ResetYaw();

		void SetZoom(float zoom);

		float GetZoom() const { return m_zoom; }

		float GetYaw() const { return m_yaw.GetValueDegrees(); }

		void SetAnimation(const std::string& animation);

		const std::string& GetAnimation() const { return m_animation; }

	public:
		/// Gets the mesh instance or nullptr if there is no mesh loaded.
		inline MeshPtr GetMesh() const noexcept { return m_mesh; }

	private:
		/// This method is called whenever the ModelFile property is changed.
		void OnModelFileChanged(const Property& prop);

		void OnYawChanged(const Property& prop);

		void OnZoomChanged(const Property& prop);

		void OnAnimationChanged(const Property& prop);

	private:
		/// Contains a list of all property connections.
		scoped_connection_container m_propConnections;

		/// The mesh instance.
		MeshPtr m_mesh;

		Degree m_yaw = Degree(0.0f);

		float m_zoom = 4.0f;

		String m_animation;
	};
}