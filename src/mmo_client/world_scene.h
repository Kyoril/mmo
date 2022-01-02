// Copyright (C) 2019 - 2022, Robin Klimonow. All rights reserved.

#pragma once

#include "base/non_copyable.h"

namespace mmo
{
	class Scene;

	/// Contains a world scene which is available for rendering.
	class WorldScene : public NonCopyable
	{
	public:
		/// Creates a new instance of the WorldScene class and initializes it.
		///	@param scene The scene object used to create 3d objects.
		explicit WorldScene(Scene& scene);

		/// Loads a given map into the world scene.
		void LoadMap();

		/// Unloads the world scene.
		void Unload();

		/// Updates the world scene. Should be called once per update cycle.
		///	@param deltaSeconds Amount of seconds since the last update cycle.
		void Update(float deltaSeconds);

		/// Gets the scene instance which this world scene is using to create objects.
		[[nodiscard]] Scene& GetScene() const noexcept { return m_scene; }

	private:
		Scene& m_scene;
		
	};
}
