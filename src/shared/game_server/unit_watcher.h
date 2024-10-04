#pragma once

#include <functional>

#include "game_unit_s.h"

namespace mmo
{
	class UnitWatcher
	{
	public:
		typedef std::function<bool(GameUnitS&, bool)> VisibilityChange;

		/// Creates a new instance of the UnitWatcher class and assigns a circle shape.
		explicit UnitWatcher(const Circle& shape, VisibilityChange visibilityChanged);

		/// Default destructor.
		virtual ~UnitWatcher();

		/// Gets the current shape.
		const Circle& GetShape() const { return m_shape; }

		/// Updates the shape.
		void SetShape(const Circle& shape);

		/// Starts watching for units in the specified shape.
		virtual void Start() = 0;

	protected:

		VisibilityChange m_visibilityChanged;

	private:

		Circle m_shape;

		virtual void OnShapeUpdated() = 0;
	};
}
