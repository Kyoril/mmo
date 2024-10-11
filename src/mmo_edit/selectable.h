#pragma once

#include "base/signal.h"
#include "math/vector3.h"
#include "math/quaternion.h"

namespace mmo
{
	class Selectable
	{
	public:
		typedef signal<void(const Selectable&)> PositionChangedSignal;
		typedef signal<void(const Selectable&)> RotationChangedSignal;
		typedef signal<void(const Selectable&)> ScaleChangedSignal;

		PositionChangedSignal positionChanged;
		RotationChangedSignal rotationChanged;
		ScaleChangedSignal scaleChanged;

	public:
		virtual ~Selectable() = default;

	public:
		virtual void Duplicate() = 0;

		/// Translates the selected object.
		/// @param delta The position offset.
		virtual void Translate(const Vector3& delta) = 0;

		/// Rotates the selected object.
		/// @param delta The orientation offset.
		virtual void Rotate(const Quaternion& delta) = 0;

		/// Scales the selected object.
		/// @param delta The scale amount on all three axis.
		virtual void Scale(const Vector3& delta) = 0;

		/// Removes the selected object permanently.
		virtual void Remove() = 0;

		/// Deselects the selected object.
		virtual void Deselect() = 0;

		virtual void SetPosition(const Vector3& position) const = 0;

		virtual void SetOrientation(const Quaternion& orientation) const = 0;

		virtual void SetScale(const Vector3& scale) const = 0;

		/// Returns the position of the selected object.
		/// @returns Position of the selected object in world coordinates.
		virtual Vector3 GetPosition() const = 0;

		/// Returns the orientation of the selected object.
		/// @returns Orientation of the selected object as a quaternion.
		virtual Quaternion GetOrientation() const = 0;

		/// Returns the scale of the selected object.
		/// @returns Scale of the selected object.
		virtual Vector3 GetScale() const = 0;
	};
}