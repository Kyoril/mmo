// Copyright (C) 2019 - 2025, Kyoril. All rights reserved.

#pragma once

#include "base/macros.h"
#include "base/typedefs.h"

namespace mmo
{
	class Scene;
	class SceneNode;
	class ManualRenderObject;

	/// @brief Manages an object which renders the axis of the scene by using three lines,
	///	       where the X axis line is red, the Y axis line is green and the Z axis line is
	///		   blue.
	class AxisDisplay final
	{
	public:
		/// @brief Creates a new instance of the AxisDisplay class and initializes it.
		/// @param scene The scene in which the objects will be placed.
		/// @param name A unique name for the scene objects.
		AxisDisplay(Scene& scene, const String& name);

		/// @brief Removes all added objects from the scene.
		~AxisDisplay();

	public:
		/// @brief Gets the scene node of the axis object, which can be used to move the object.
		/// @return The scene node of the axis object.
		[[nodiscard]] SceneNode& GetSceneNode() const { ASSERT(m_sceneNode); return *m_sceneNode; }

		/// @brief Determines whether the object is visible in the scene.
		[[nodiscard]] bool IsVisible() const;

		/// @brief Sets whether the object should be rendered.
		/// @param visible If set to true, the object will be visible.
		void SetVisible(bool visible);


	private:
		/// @brief Setup the manual render object.
		void SetupManualRenderObject() const;
		
	private:
		Scene& m_scene;
		SceneNode* m_sceneNode { nullptr };
		ManualRenderObject* m_renderObject { nullptr };
		bool m_visible { true };
	};
}
