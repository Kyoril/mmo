// Copyright (C) 2019 - 2024, Kyoril. All rights reserved.

#include "model_frame.h"
#include "scene_graph/mesh_manager.h"

namespace mmo
{
	ModelFrame::ModelFrame(const std::string & name)
		: Frame("Model", name)
	{
		// Register default properties and subscribe to their Changed events.
		m_propConnections += AddProperty("ModelFile", "").Changed.connect(this, &ModelFrame::OnModelFileChanged);
		m_propConnections += AddProperty("Zoom", "4.0").Changed.connect(this, &ModelFrame::OnZoomChanged);
		m_propConnections += AddProperty("Yaw", "0").Changed.connect(this, &ModelFrame::OnYawChanged);
		m_propConnections += AddProperty("Animation", "").Changed.connect(this, &ModelFrame::OnAnimationChanged);
	}

	void ModelFrame::SetModelFile(const std::string & filename)
	{
		auto* prop = GetProperty("ModelFile");
		if (prop != nullptr)
		{
			prop->Set(filename);
		}
	}

	void ModelFrame::SetYaw(const float angleDegrees)
	{
		m_yaw = Degree(angleDegrees);
		Invalidate(false);
	}

	void ModelFrame::Yaw(const float angleDegrees)
	{
		m_yaw += Degree(angleDegrees);
		Invalidate(false);
	}

	void ModelFrame::ResetYaw()
	{
		m_yaw = Degree(0);
		Invalidate(false);
	}

	void ModelFrame::SetZoom(const float zoom)
	{
		m_zoom = zoom;
		Invalidate(false);
	}

	void ModelFrame::SetAnimation(const std::string& animation)
	{
		m_animation = animation;
		Invalidate(false);
	}

	void ModelFrame::OnModelFileChanged(const Property& prop)
	{
		// Load the mesh file
		m_mesh = MeshManager::Get().Load(prop.GetValue());

		// Invalidate the frame
		Invalidate(false);
	}

	void ModelFrame::OnYawChanged(const Property& prop)
	{
		SetYaw(std::atof(prop.GetValue().c_str()));
	}

	void ModelFrame::OnZoomChanged(const Property& prop)
	{
		SetZoom(std::atof(prop.GetValue().c_str()));
	}

	void ModelFrame::OnAnimationChanged(const Property& prop)
	{
		SetAnimation(prop.GetValue());
	}
}
