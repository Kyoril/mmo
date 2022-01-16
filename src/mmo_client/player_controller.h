// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "frame_ui/mouse_event_args.h"
#include "game/game_object_c.h"

namespace mmo
{
	class Scene;
	class SceneNode;
	class Camera;

	/// @brief This class controls a player entity.
	class PlayerController final
	{
	public:
		PlayerController(Scene& scene);

		~PlayerController();

	public:
		void Update(float deltaSeconds);
		
		void OnMouseDown(MouseButton button, int32 x, int32 y);

		void OnMouseUp(MouseButton button, int32 x, int32 y);

		void OnMouseMove(int32 x, int32 y);

		void OnMouseWheel(int32 delta);
		
		void OnKeyDown(int32 key);
		
		void OnKeyUp(int32 key);
		
		void SetControlledObject(const std::shared_ptr<GameObjectC>& controlledObject);

		[[nodiscard]] const std::shared_ptr<GameObjectC>& GetControlledObject() const noexcept { return m_controlledObject; }

		[[nodiscard]] Camera& GetCamera() const { ASSERT(m_defaultCamera); return *m_defaultCamera;}

	private:
		void SetupCamera();

		void ResetControls();

	private:
		Scene& m_scene;
		Camera* m_defaultCamera { nullptr };
		SceneNode* m_cameraAnchorNode { nullptr };
		SceneNode* m_cameraNode { nullptr };
		std::shared_ptr<GameObjectC> m_controlledObject;
		Vector3 m_movementVector;
		bool m_leftButtonDown { false };
		bool m_rightButtonDown { false };
		Point m_lastMousePosition {};
		Radian m_rotation;
	};
}
