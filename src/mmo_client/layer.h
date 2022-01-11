#pragma once

#include "base/non_copyable.h"

namespace mmo
{
	/// @brief This class represents a screen layer used for getting stuff rendered on screen.
	class Layer : public NonCopyable
	{
	public:
		/// @brief Default constructor.
		Layer() = default;

		/// @brief Virtual default destructor because of inheritance.
		virtual ~Layer() = default;

	public:
		/// @brief Executed when the layer should be updated.
		/// @param deltaSeconds Amount of seconds passed since the last update iteration.
		virtual void OnUpdate(float deltaSeconds) {}

		/// @brief Executed when the layer should be rendered.
		virtual void OnRender() = 0;

	private:
		bool m_visible { true };
	};
}