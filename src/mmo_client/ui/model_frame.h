// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "scene_graph/mesh.h"
#include "frame_ui/frame.h"
#include "scene_graph/scene.h"

namespace mmo
{
	/// This class renders a model inside it's content area.
	class ModelFrame
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

		void SetAutoRender(bool value);

		void SetZoom(float zoom);

		float GetZoom() const { return m_zoom; }

		float GetOffsetX() const { return m_offset.x; }

		float GetOffsetY() const { return m_offset.y; }

		float GetOffsetZ() const { return m_offset.z; }

		void SetOffsetX(const float value) { SetProperty("OffsetX", std::to_string(value)); }

		void SetOffsetY(const float value) { SetProperty("OffsetY", std::to_string(value)); }

		void SetOffsetZ(const float value) { SetProperty("OffsetZ", std::to_string(value)); }

		float GetYaw() const { return m_yaw.GetValueDegrees(); }

		/// Sets the name of the skeleton animation to play. Also rebinds the animation state of
		///	an entity which already exists, so the new animation takes effect right away instead
		///	of only the next time the model file changes.
		/// @param animation Name of the animation state, or an empty string to play none.
		void SetAnimation(const std::string& animation);

		const std::string& GetAnimation() const { return m_animation; }

		void Update(float elapsed) override;

		void InvalidateModel();

		bool ShouldAutoUpdateModel();

		bool ShouldRenderModel();

	public:
		/// Gets the mesh instance or nullptr if there is no mesh loaded.
		inline MeshPtr GetMesh() const { return m_mesh; }

		Scene& GetScene() { return m_scene; }

		Entity* GetEntity() const { return m_entity; }

		Camera* GetCamera() const { return m_camera; }

	protected:
		/// This method is called whenever the ModelFile property is changed. Overriding classes
		/// which keep state bound to the current entity (e.g. attached item meshes) must release
		/// it before calling the base implementation, which destroys the entity.
		virtual void OnModelFileChanged(const Property& prop);

		void OnYawChanged(const Property& prop);

		void OnZoomChanged(const Property& prop);

		void OnAnimationChanged(const Property& prop);

		void OnOffsetChanged(const Property& prop);

		void OnAutoRenderChanged(const Property& prop);

		/// Binds m_animationState to the animation named by m_animation on the current entity,
		///	disabling whatever was bound before. Leaves m_animationState at nullptr when there is
		///	no entity, no animation name, or the entity's skeleton does not provide that name.
		void RebindAnimationState();

	protected:
		/// Contains a list of all property connections.
		scoped_connection_container m_propConnections;

		Scene m_scene;

		/// The mesh instance.
		MeshPtr m_mesh;

		Entity* m_entity = nullptr;

		Degree m_yaw = Degree(0.0f);

		float m_zoom = 4.0f;

		Vector3 m_offset = Vector3::UnitY;

		String m_animation;

		SceneNode* m_entityNode = nullptr;

		SceneNode* m_cameraAnchorNode = nullptr;

		SceneNode* m_cameraNode = nullptr;

		Camera* m_camera = nullptr;

		AnimationState* m_animationState = nullptr;

		bool m_autoUpdateModel = true;

		bool m_modelInvalidated = false;
	};
}
