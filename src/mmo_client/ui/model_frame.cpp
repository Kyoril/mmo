// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#include "model_frame.h"
#include "scene_graph/mesh_manager.h"

#include "base/filesystem.h"

namespace mmo
{
	ModelFrame::ModelFrame(const std::string & name)
		: Frame("Model", name)
	{
		m_entityNode = m_scene.GetRootSceneNode().CreateChildSceneNode(Vector3::Zero, Quaternion(Degree(GetYaw()), Vector3::UnitY));
		m_entity = nullptr;
		m_animationState = nullptr;

		m_cameraAnchorNode = m_scene.GetRootSceneNode().CreateChildSceneNode(Vector3::UnitY, Quaternion(Degree(-120), Vector3::UnitY));
		m_cameraNode = m_cameraAnchorNode->CreateChildSceneNode(Vector3::UnitZ * GetZoom());
		m_camera = m_scene.CreateCamera("Camera");
		m_cameraNode->AttachObject(*m_camera);

		// Register default properties and subscribe to their Changed events.
		m_propConnections += AddProperty("ModelFile", "").Changed.connect(this, &ModelFrame::OnModelFileChanged);
		m_propConnections += AddProperty("Zoom", "4.0").Changed.connect(this, &ModelFrame::OnZoomChanged);
		m_propConnections += AddProperty("Yaw", "0").Changed.connect(this, &ModelFrame::OnYawChanged);
		m_propConnections += AddProperty("Animation", "").Changed.connect(this, &ModelFrame::OnAnimationChanged);
		m_propConnections += AddProperty("OffsetX", "0").Changed.connect(this, &ModelFrame::OnOffsetChanged);
		m_propConnections += AddProperty("OffsetY", "1").Changed.connect(this, &ModelFrame::OnOffsetChanged);
		m_propConnections += AddProperty("OffsetZ", "0").Changed.connect(this, &ModelFrame::OnOffsetChanged);
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

	void ModelFrame::Update(const float elapsed)
	{
		Frame::Update(elapsed);

		m_entityNode->SetOrientation(Quaternion(Degree(GetYaw()), Vector3::UnitY));

		// Interpolate zoom
		if (std::fabsf(m_cameraNode->GetPosition().z - m_zoom) > FLT_EPSILON)
		{
			const float diff = m_zoom - m_cameraNode->GetPosition().z;
			const float step = diff * elapsed * 3.0f;
			m_cameraNode->Translate(Vector3::UnitZ * step, TransformSpace::Local);
		}

		// Interpolate offset
		if (!m_cameraAnchorNode->GetPosition().IsNearlyEqual(m_offset))
		{
			const Vector3 diff = m_offset - m_cameraAnchorNode->GetPosition();
			const Vector3 step = diff * elapsed * 3.0f;
			m_cameraAnchorNode->Translate(step, TransformSpace::World);
		}

		if (m_animationState)
		{
			m_animationState->AddTime(elapsed);
		}
	}

	void ModelFrame::OnModelFileChanged(const Property& prop)
	{
		// Load the mesh file
		m_mesh = MeshManager::Get().Load(prop.GetValue());
		if (m_entity)
		{
			m_entityNode->DetachObject(*m_entity);
			m_scene.DestroyEntity(*m_entity);
			m_entity = nullptr;
			m_animationState = nullptr;
		}

		if (m_mesh)
		{
			m_entity = m_scene.CreateEntity("Preview", m_mesh);
			m_entityNode->AttachObject(*m_entity);

			if (!GetAnimation().empty())
			{
				m_animationState = m_entity->GetAnimationState(GetAnimation());
				if (m_animationState)
				{
					m_animationState->SetWeight(1.0f);
					m_animationState->SetLoop(true);
					m_animationState->SetEnabled(true);
				}
			}
		}
		else
		{
			ELOG("Unable to load mesh " << prop.GetValue());
		}
		
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

	void ModelFrame::OnOffsetChanged(const Property& prop)
	{
		m_offset.x = std::atof(GetProperty("OffsetX")->GetValue().c_str());
		m_offset.y = std::atof(GetProperty("OffsetY")->GetValue().c_str());
		m_offset.z = std::atof(GetProperty("OffsetZ")->GetValue().c_str());
	}
}
